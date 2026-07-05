/* SPDX-License-Identifier: MIT */
#include "christie_control.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define CHRISTIE_TIMEOUT 2000  // milliseconds

ChristieContext* christie_connect(const char* host, int port) {
    if (!host) {
        fprintf(stderr, "[CHRISTIE] Error: NULL host address\n");
        return NULL;
    }
    
    ChristieContext* ctx = (ChristieContext*)malloc(sizeof(ChristieContext));
    if (!ctx) return NULL;
    
    ctx->host = (char*)malloc(strlen(host) + 1);
    if (!ctx->host) {
        free(ctx);
        return NULL;
    }
    
    strcpy(ctx->host, host);
    ctx->port = port;
    ctx->socket_fd = -1;
    ctx->connected = 0;
    
    // Create TCP socket
    ctx->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->socket_fd < 0) {
        fprintf(stderr, "[CHRISTIE] Error creating socket\n");
        free(ctx->host);
        free(ctx);
        return NULL;
    }
    
    // Tight socket timeout favors control-loop responsiveness over waiting on
    // long projector responses.
    // Implication: transient network jitter may surface as recoverable errors.
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = CHRISTIE_TIMEOUT * 1000;
    setsockopt(ctx->socket_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(ctx->socket_fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
    
    // Connect to Christie
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "[CHRISTIE] Invalid IP address: %s\n", host);
        close(ctx->socket_fd);
        free(ctx->host);
        free(ctx);
        return NULL;
    }
    
    if (connect(ctx->socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "[CHRISTIE] Connection failed to %s:%d\n", host, port);
        close(ctx->socket_fd);
        free(ctx->host);
        free(ctx);
        return NULL;
    }
    
    ctx->connected = 1;
    fprintf(stderr, "[CHRISTIE] Connected to %s:%d\n", host, port);
    return ctx;
}

static int christie_send_command(ChristieContext* ctx, const char* command, char* response) {
    if (!ctx || !ctx->connected || ctx->socket_fd < 0) {
        fprintf(stderr, "[CHRISTIE] Error: Not connected\n");
        return -1;
    }
    
    if (!command) {
        fprintf(stderr, "[CHRISTIE] Error: NULL command\n");
        return -1;
    }
    
    // Send command
    if (send(ctx->socket_fd, command, strlen(command), 0) < 0) {
        fprintf(stderr, "[CHRISTIE] Send failed: %s\n", command);
        return -1;
    }
    
    // Receive path is optional so fire-and-forget actuator commands do not
    // block on response parsing unless caller requires feedback.
    if (response) {
        memset(response, 0, 128);
        ssize_t bytes_received = recv(ctx->socket_fd, response, 127, 0);
        if (bytes_received < 0) {
            fprintf(stderr, "[CHRISTIE] Receive timeout\n");
            return -1;
        }
        fprintf(stderr, "[CHRISTIE] Response: %s\n", response);
    }
    
    return 0;
}

int christie_shutter(ChristieContext* ctx, int state) {
    if (!ctx) return -1;
    
    char command[64];
    int shutter_code = state ? 1 : 0;  // 1 = closed, 0 = open
    snprintf(command, sizeof(command), "(SHU %d)\r\n", shutter_code);
    
    fprintf(stderr, "[CHRISTIE] Shutter %s\n", state ? "CLOSED" : "OPEN");
    return christie_send_command(ctx, command, NULL);
}

int christie_laser_power(ChristieContext* ctx, int level) {
    if (!ctx) return -1;
    
    // Clamp to projector command domain to prevent malformed payloads and
    // enforce predictable optics behavior from untrusted caller input.
    if (level < 0) level = 0;
    if (level > 1000) level = 1000;
    
    char command[64];
    snprintf(command, sizeof(command), "(LPLV %03d)\r\n", level);
    
    fprintf(stderr, "[CHRISTIE] Laser power set to %d (0-1000)\n", level);
    return christie_send_command(ctx, command, NULL);
}

int christie_query(ChristieContext* ctx, const char* command, char* response) {
    if (!ctx || !response) return -1;
    
    char query_cmd[64];
    snprintf(query_cmd, sizeof(query_cmd), "(%s ?)\r\n", command);
    
    fprintf(stderr, "[CHRISTIE] Query: %s\n", command);
    return christie_send_command(ctx, query_cmd, response);
}

int christie_is_powered_on(ChristieContext* ctx) {
    if (!ctx) return -1;
    
    char response[128];
    if (christie_query(ctx, "PWR", response) < 0) {
        return -1;
    }
    
    // Parser strategy prefers strict token parse first, then permissive fallback
    // for firmware variants that include additional wrappers.
    int power_state = 0;
    if (sscanf(response, "PWR %d", &power_state) == 1) {
        fprintf(stderr, "[CHRISTIE] Power state: %d\n", power_state);
        return power_state;
    }
    
    // Try alternative response format
    if (strstr(response, "1") != NULL) {
        return 1;  // Powered on
    }
    
    return 0;  // Powered off or unparseable
}

int christie_is_ready(ChristieContext* ctx) {
    if (!ctx) return -1;
    
    char response[128];
    
    // Check lamp/ready status
    if (christie_query(ctx, "RDSM", response) < 0) {
        return -1;
    }
    
    // Readiness semantics are firmware-defined; we treat explicit numeric parse
    // as authoritative and string-contains fallback as best-effort.
    int ready_state = 0;
    if (sscanf(response, "RDSM %d", &ready_state) == 1) {
        fprintf(stderr, "[CHRISTIE] Ready state: %d\n", ready_state);
        return ready_state;
    }
    
    if (strstr(response, "1") != NULL) {
        return 1;  // Ready
    }
    
    return 0;  // Not ready
}

int christie_get_temperature(ChristieContext* ctx, float* temp_out) {
    if (!ctx || !temp_out) return -1;
    
    char response[128];
    if (christie_query(ctx, "TPMG", response) < 0) {
        return -1;
    }
    
    // Try to extract temperature value
    float temp = 0.0f;
    if (sscanf(response, "%f", &temp) == 1) {
        *temp_out = temp;
        fprintf(stderr, "[CHRISTIE] Temperature: %.1f°C\n", temp);
        return 0;
    }
    
    return -1;
}

int christie_power_on(ChristieContext* ctx) {
    if (!ctx) return -1;
    
    char command[64];
    snprintf(command, sizeof(command), "(PWR 1)\r\n");
    
    fprintf(stderr, "[CHRISTIE] Power ON command\n");
    return christie_send_command(ctx, command, NULL);
}

int christie_power_off(ChristieContext* ctx) {
    if (!ctx) return -1;
    
    char command[64];
    snprintf(command, sizeof(command), "(PWR 0)\r\n");
    
    fprintf(stderr, "[CHRISTIE] Power OFF command\n");
    return christie_send_command(ctx, command, NULL);
}

void christie_disconnect(ChristieContext* ctx) {
    if (!ctx) return;
    
    if (ctx->socket_fd >= 0) {
        close(ctx->socket_fd);
        fprintf(stderr, "[CHRISTIE] Disconnected\n");
    }
    
    if (ctx->host) free(ctx->host);
    free(ctx);
}
