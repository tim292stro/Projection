/* SPDX-License-Identifier: MIT */
#include "mqtt_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef PROJECTION_USE_PAHO_MQTT
#include <MQTTClient.h>
#endif

/*
 * NOTE: This unit currently provides a broker-compatible API surface with
 * stubbed transport semantics.
 *
 * Why this exists:
 * - Keeps theater-control orchestration code testable on hosts that do not
 *   have production MQTT dependencies or broker access.
 * - Preserves command/topic contracts so migration to a real client can happen
 *   behind the same function boundary.
 *
 * Operational implication:
 * - A successful return code means "accepted by stub layer", not guaranteed
 *   broker delivery.
 *
 * Production target:
 * - Integrate full Paho MQTT C client: https://github.com/eclipse/paho.mqtt.c
 * - Ubuntu package baseline: apt install libmosquitto-dev
 */

#define MQTT_BROKER_TIMEOUT 5  // seconds
#define MQTT_CONTENT_ADD_TOPIC "content/add"
#define MQTT_CONTENT_ADD_RESPONSE_TOPIC "content/add/response"

#ifdef PROJECTION_USE_PAHO_MQTT
#define MQTT_QOS 1

static void paho_connection_lost(void* context, char* cause) {
    MQTTContext* ctx = (MQTTContext*)context;
    if (!ctx) return;
    ctx->connected = 0;
    fprintf(stderr, "[MQTT] Connection lost: %s\n", cause ? cause : "unknown");
}

static int paho_message_arrived(void* context, char* topic_name, int topic_len, MQTTClient_message* message) {
    MQTTContext* ctx = (MQTTContext*)context;
    if (!ctx || !message) {
        return 1;
    }

    const char* topic = topic_name ? topic_name : "";
    size_t topic_size = topic_name ? strlen(topic_name) : 0;
    if (topic_len > 0 && (size_t)topic_len < topic_size) {
        topic_size = (size_t)topic_len;
    }

    if (topic_size == strlen(MQTT_CONTENT_ADD_TOPIC) &&
        strncmp(topic, MQTT_CONTENT_ADD_TOPIC, topic_size) == 0 &&
        ctx->content_add_callback) {
        char* payload_copy = (char*)malloc((size_t)message->payloadlen + 1);
        if (!payload_copy) {
            MQTTClient_freeMessage(&message);
            MQTTClient_free(topic_name);
            return 1;
        }

        memcpy(payload_copy, message->payload, (size_t)message->payloadlen);
        payload_copy[message->payloadlen] = '\0';
        ctx->content_add_callback(payload_copy, ctx->content_add_user_data);
        free(payload_copy);
    }

    MQTTClient_freeMessage(&message);
    MQTTClient_free(topic_name);
    return 1;
}
#endif

MQTTContext* mqtt_init(const char* broker_address, int broker_port) {
    if (!broker_address) {
        fprintf(stderr, "[MQTT] Error: NULL broker address\n");
        return NULL;
    }
    
    MQTTContext* ctx = (MQTTContext*)malloc(sizeof(MQTTContext));
    if (!ctx) return NULL;
    
    ctx->broker_address = (char*)malloc(strlen(broker_address) + 1);
    if (!ctx->broker_address) {
        free(ctx);
        return NULL;
    }
    
    strcpy(ctx->broker_address, broker_address);
    ctx->broker_port = broker_port;
    ctx->client_id = (char*)malloc(32);
    if (!ctx->client_id) {
        free(ctx->broker_address);
        free(ctx);
        return NULL;
    }
    
    snprintf(ctx->client_id, 32, "projection_engine_%d", getpid());
    ctx->connected = 0;
    ctx->using_paho = 0;
    ctx->client_handle = NULL;
    ctx->content_add_callback = NULL;
    ctx->content_add_user_data = NULL;
    
    fprintf(stderr, "[MQTT] Initialized: broker=%s:%d, client_id=%s\n",
            ctx->broker_address, ctx->broker_port, ctx->client_id);
    
#ifdef PROJECTION_USE_PAHO_MQTT
    char broker_uri[256] = {0};
    snprintf(broker_uri, sizeof(broker_uri), "tcp://%s:%d", ctx->broker_address, ctx->broker_port);

    MQTTClient client;
    if (MQTTClient_create(&client, broker_uri, ctx->client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL) != MQTTCLIENT_SUCCESS) {
        fprintf(stderr, "[MQTT] Error: failed to create Paho client\n");
        free(ctx->client_id);
        free(ctx->broker_address);
        free(ctx);
        return NULL;
    }

    MQTTClient_setCallbacks(client, ctx, paho_connection_lost, paho_message_arrived, NULL);

    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.connectTimeout = MQTT_BROKER_TIMEOUT;

    int rc = MQTTClient_connect(client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        fprintf(stderr, "[MQTT] Error: connect failed (rc=%d)\n", rc);
        MQTTClient_destroy(&client);
        free(ctx->client_id);
        free(ctx->broker_address);
        free(ctx);
        return NULL;
    }

    ctx->client_handle = client;
    ctx->using_paho = 1;
    ctx->connected = 1;
    fprintf(stderr, "[MQTT] Connected via Paho transport\n");
#else
    // Fallback mode keeps local integration paths runnable when Paho is not built.
    ctx->connected = 1;
    fprintf(stderr, "[MQTT] Running in fallback non-broker mode (Paho not compiled)\n");
#endif
    
    return ctx;
}

int mqtt_mute_cp950a(MQTTContext* ctx) {
    if (!ctx || !ctx->connected) {
        fprintf(stderr, "[MQTT] Error: Not connected to broker\n");
        return -1;
    }
    
    const char* payload = "{\"device\":\"dolby_cp950a\",\"action\":\"mute\"}";
    int result = mqtt_publish_command(ctx, "theater/hardware/cmd", payload);
    
    if (result == 0) {
        fprintf(stderr, "[MQTT] Mute command sent to CP950A\n");
    }
    
    return result;
}

int mqtt_unmute_cp950a(MQTTContext* ctx) {
    if (!ctx || !ctx->connected) {
        fprintf(stderr, "[MQTT] Error: Not connected to broker\n");
        return -1;
    }
    
    const char* payload = "{\"device\":\"dolby_cp950a\",\"action\":\"unmute\"}";
    int result = mqtt_publish_command(ctx, "theater/hardware/cmd", payload);
    
    if (result == 0) {
        fprintf(stderr, "[MQTT] Unmute command sent to CP950A\n");
    }
    
    return result;
}

int mqtt_publish_command(MQTTContext* ctx, const char* device, const char* payload) {
    if (!ctx || !ctx->connected) {
        fprintf(stderr, "[MQTT] Error: Not connected to broker\n");
        return -1;
    }
    
    if (!device || !payload) {
        fprintf(stderr, "[MQTT] Error: NULL device or payload\n");
        return -1;
    }
    
    fprintf(stderr, "[MQTT] Publishing to %s: %s\n", device, payload);
    
#ifdef PROJECTION_USE_PAHO_MQTT
    if (ctx->using_paho && ctx->client_handle) {
        MQTTClient client = (MQTTClient)ctx->client_handle;
        MQTTClient_deliveryToken token;
        int rc = MQTTClient_publish(client, device, (int)strlen(payload), (void*)payload, MQTT_QOS, 0, &token);
        if (rc != MQTTCLIENT_SUCCESS) {
            fprintf(stderr, "[MQTT] Publish failed rc=%d topic=%s\n", rc, device);
            return -1;
        }
        rc = MQTTClient_waitForCompletion(client, token, 5000L);
        if (rc != MQTTCLIENT_SUCCESS) {
            fprintf(stderr, "[MQTT] Publish completion failed rc=%d topic=%s\n", rc, device);
            return -1;
        }
        return 0;
    }
#endif

    return 0;  // Fallback mode
}

int mqtt_update_marquee(MQTTContext* ctx, const char* title, const char* format, 
                       int duration_sec, const char* start_time) {
    if (!ctx || !ctx->connected) {
        fprintf(stderr, "[MQTT] Error: Not connected to broker\n");
        return -1;
    }
    
    if (!title) {
        fprintf(stderr, "[MQTT] Error: NULL title\n");
        return -1;
    }
    
    char payload[1024];
    if (start_time) {
        snprintf(payload, sizeof(payload),
                "{\"title\":\"%s\",\"format\":\"%s\",\"duration_sec\":%d,\"start_time\":\"%s\"}",
                title, format ? format : "unknown", duration_sec, start_time);
    } else {
        snprintf(payload, sizeof(payload),
                "{\"title\":\"%s\",\"format\":\"%s\",\"duration_sec\":%d}",
                title, format ? format : "unknown", duration_sec);
    }
    
    int result = mqtt_publish_command(ctx, "theater/marquee", payload);
    if (result == 0) {
        fprintf(stderr, "[MQTT] Marquee updated: %s\n", title);
    }
    
    return result;
}

int mqtt_update_marquee_stage(MQTTContext* ctx, const char* title, const char* cover_art_path,
                             const char* event_text, const char* start_time) {
    if (!ctx || !ctx->connected) {
        fprintf(stderr, "[MQTT] Error: Not connected to broker\n");
        return -1;
    }
    
    if (!title) {
        fprintf(stderr, "[MQTT] Error: NULL title\n");
        return -1;
    }
    
    // Live-stage schema decision:
    // set "type":"live-stage" so downstream automation can suppress media
    // playback assumptions and render event-oriented marquee content instead.
    char payload[2048];
    snprintf(payload, sizeof(payload),
            "{\"title\":\"%s\",\"type\":\"live-stage\",\"cover_art\":\"%s\",\"description\":\"%s\",\"start_time\":\"%s\"}",
            title, 
            cover_art_path ? cover_art_path : "",
            event_text ? event_text : "",
            start_time ? start_time : "");
    
    int result = mqtt_publish_command(ctx, "theater/marquee", payload);
    if (result == 0) {
        fprintf(stderr, "[MQTT] Marquee updated (stage): %s\n", title);
    }
    
    return result;
}

int mqtt_house_lights(MQTTContext* ctx, int level) {
    if (!ctx || !ctx->connected) {
        fprintf(stderr, "[MQTT] Error: Not connected to broker\n");
        return -1;
    }
    
    // Clamp to physical dimmer domain [0,100].
    // Implication: callers can pass untrusted values without risking malformed
    // device payloads, but out-of-range intent is silently normalized.
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    
    char payload[256];
    snprintf(payload, sizeof(payload), "{\"device\":\"house_lights\",\"level\":%d}", level);
    
    int result = mqtt_publish_command(ctx, "theater/hardware/cmd", payload);
    if (result == 0) {
        fprintf(stderr, "[MQTT] House lights set to %d%%\n", level);
    }
    
    return result;
}

int mqtt_curtains(MQTTContext* ctx, int state) {
    if (!ctx || !ctx->connected) {
        fprintf(stderr, "[MQTT] Error: Not connected to broker\n");
        return -1;
    }
    
    char payload[256];
    snprintf(payload, sizeof(payload), "{\"device\":\"curtains\",\"state\":%s}", 
            state ? "\"closed\"" : "\"open\"");
    
    int result = mqtt_publish_command(ctx, "theater/hardware/cmd", payload);
    if (result == 0) {
        fprintf(stderr, "[MQTT] Curtains %s\n", state ? "CLOSED" : "OPEN");
    }
    
    return result;
}

int mqtt_subscribe_content_add(MQTTContext* ctx, MQTTContentAddCallback callback, void* user_data) {
    if (!ctx || !ctx->connected) {
        fprintf(stderr, "[MQTT] Error: Not connected to broker\n");
        return -1;
    }

    if (!callback) {
        fprintf(stderr, "[MQTT] Error: NULL callback for %s\n", MQTT_CONTENT_ADD_TOPIC);
        return -1;
    }

    // Store callback for both Paho callback path and local test injection path.
    ctx->content_add_callback = callback;
    ctx->content_add_user_data = user_data;

#ifdef PROJECTION_USE_PAHO_MQTT
    if (ctx->using_paho && ctx->client_handle) {
        MQTTClient client = (MQTTClient)ctx->client_handle;
        int rc = MQTTClient_subscribe(client, MQTT_CONTENT_ADD_TOPIC, MQTT_QOS);
        if (rc != MQTTCLIENT_SUCCESS) {
            fprintf(stderr, "[MQTT] Subscribe failed rc=%d topic=%s\n", rc, MQTT_CONTENT_ADD_TOPIC);
            return -1;
        }
        fprintf(stderr, "[MQTT] Subscribed to topic: %s\n", MQTT_CONTENT_ADD_TOPIC);
        return 0;
    }
#endif

    fprintf(stderr, "[MQTT] Subscribed (fallback) to topic: %s\n", MQTT_CONTENT_ADD_TOPIC);
    return 0;
}

int mqtt_publish_content_add_response(MQTTContext* ctx, const char* payload) {
    if (!ctx || !ctx->connected) {
        fprintf(stderr, "[MQTT] Error: Not connected to broker\n");
        return -1;
    }
    if (!payload) {
        fprintf(stderr, "[MQTT] Error: NULL response payload\n");
        return -1;
    }

    return mqtt_publish_command(ctx, MQTT_CONTENT_ADD_RESPONSE_TOPIC, payload);
}

int mqtt_stub_receive_content_add(MQTTContext* ctx, const char* payload) {
    if (!ctx || !ctx->connected) {
        fprintf(stderr, "[MQTT] Error: Not connected to broker\n");
        return -1;
    }
    if (!payload) {
        fprintf(stderr, "[MQTT] Error: NULL content/add payload\n");
        return -1;
    }
    if (!ctx->content_add_callback) {
        fprintf(stderr, "[MQTT] Warning: content/add payload received but no callback registered\n");
        return -1;
    }

    // Explicit test seam: allows deterministic ingestion tests without a broker.
    // Decision impact: this function should remain clearly marked as non-
    // production transport so deployment paths do not depend on it implicitly.
    fprintf(stderr, "[MQTT] Received (local-inject) %s: %s\n", MQTT_CONTENT_ADD_TOPIC, payload);
    ctx->content_add_callback(payload, ctx->content_add_user_data);
    return 0;
}

void mqtt_close(MQTTContext* ctx) {
    if (!ctx) return;
    
    if (ctx->connected) {
        fprintf(stderr, "[MQTT] Disconnecting from broker\n");

#ifdef PROJECTION_USE_PAHO_MQTT
        if (ctx->using_paho && ctx->client_handle) {
            MQTTClient client = (MQTTClient)ctx->client_handle;
            MQTTClient_disconnect(client, 10000);
            MQTTClient_destroy(&client);
            ctx->client_handle = NULL;
        }
#endif
    }
    
    if (ctx->broker_address) free(ctx->broker_address);
    if (ctx->client_id) free(ctx->client_id);
    free(ctx);
}
