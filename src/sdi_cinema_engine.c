/* SPDX-License-Identifier: MIT */
/**
 * @file sdi_cinema_engine.c
 * @brief Cinema Media Player Engine for DeckLink 8K Pro G2 and RME HDSPe AES
 * @note Compiles natively on Headless Ubuntu Server 22.04 LTS
 * 
 * Build: use `make` in src/ (plus `make decklink-enforcer` for SDK helper)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <math.h>

#include <vlc/vlc.h>

#include "modules/metadata_extractor.h"
#include "modules/alsa_context.h"
#include "modules/mqtt_client.h"
#include "modules/christie_control.h"
#include "modules/playlist_manager.h"
#include "modules/caldav_scheduler.h"
#include "modules/dxd16_control.h"
#include "modules/decklink_enforcer.h"

#define TCP_CONTROL_PORT 8080
#define RME_DEVICE_DEFAULT "rme_cinema_map"
#define RME_DEVICE_LEGACY "hw:0,0"
#define RME_CHANNELS 16
#define RME_SAMPLE_RATE 48000
#define MQTT_BROKER_IP "192.168.1.50"
#define MQTT_BROKER_PORT 1883
#define CHRISTIE_IP "192.168.1.75"
#define CHRISTIE_PORT 3002
#define PREBUFFER_TIME 30000  // 30 seconds in milliseconds
#define IDLE_TIMEOUT_SECONDS 300  // 5 minutes

#define DEGRADED_DECKLINK_2SI        (1u << 0)
#define DEGRADED_DXD16_TIMING        (1u << 1)
#define DEGRADED_TRANSITION_SAFEHOLD (1u << 2)

/* Global state */
static volatile int should_exit = 0;
static libvlc_instance_t* vlc_instance = NULL;
static libvlc_media_player_t* player_A = NULL;
static libvlc_media_player_t* player_B = NULL;
static int current_player_is_A = 1;
static pthread_mutex_t player_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Hardware contexts */
static MQTTContext* mqtt_ctx = NULL;
static ChristieContext* christie_ctx = NULL;
static ALSAContext* alsa_ctx = NULL;
static PlaylistContext* playlist_ctx = NULL;
static CalDAVContext* caldav_ctx = NULL;
static DXD16Context* dxd16_ctx = NULL;

/* Current media metadata */
static ProjectionMediaMetadata current_metadata = {0};

/* Track previous aspect ratio to detect changes */
static float previous_aspect_ratio = 2.39f;  // Default scope
static volatile int live_mode_active = 0;
static volatile unsigned int degraded_state_flags = 0;

static int add_file_with_caldav_check(const char* file_path, int64_t start_delay_ms,
                                      const char* title_in,
                                      char* response_out, int response_len,
                                      int* was_empty_out);
static void power_on_and_preload(void);
static libvlc_media_player_t* create_player(const char* file_path);
static void execute_transition(const char* target_file);

static void handle_end_reached_handover(void) {
    // End-of-media handover is serialized under player_mutex so playlist cursor,
    // transition side-effects, and player swaps remain atomic to control-plane
    // commands arriving on TCP/MQTT.
    pthread_mutex_lock(&player_mutex);

    if (playlist_advance(playlist_ctx) != 0) {
        fprintf(stderr, "[ENGINE] End reached: no next track available\n");
        pthread_mutex_unlock(&player_mutex);
        return;
    }

    const char* next_file = playlist_current_file(playlist_ctx);
    if (!next_file) {
        fprintf(stderr, "[ENGINE] End reached: playlist advanced but no current file\n");
        pthread_mutex_unlock(&player_mutex);
        return;
    }

    execute_transition(next_file);
    libvlc_media_player_t* next_player = create_player(next_file);
    if (!next_player) {
        fprintf(stderr, "[ENGINE] End reached: failed to create next player\n");
        pthread_mutex_unlock(&player_mutex);
        return;
    }

    if (current_player_is_A) {
        if (player_B) libvlc_media_player_release(player_B);
        player_B = next_player;
        libvlc_media_player_play(player_B);
        current_player_is_A = 0;
    } else {
        if (player_A) libvlc_media_player_release(player_A);
        player_A = next_player;
        libvlc_media_player_play(player_A);
        current_player_is_A = 1;
    }

    fprintf(stderr, "[ENGINE] End reached: automatic handover complete\n");
    pthread_mutex_unlock(&player_mutex);
}

static int is_live_mode_active(void) {
    return live_mode_active != 0;
}

static void enforce_live_mode_safety(void) {
    // Live-mode hard safety contract:
    // projector must be OFF and CP950A must be muted to prevent unintended
    // playout paths while stage-only events are active.
    if (christie_ctx) {
        fprintf(stderr, "[LIVE_MODE] Forcing projector OFF\n");
        christie_power_off(christie_ctx);
    }
    if (mqtt_ctx) {
        fprintf(stderr, "[LIVE_MODE] Forcing CP950A MUTE\n");
        mqtt_mute_cp950a(mqtt_ctx);
    }
}

static void set_live_mode_active(int enabled, const char* reason) {
    if (enabled) {
        if (!live_mode_active) {
            fprintf(stderr, "[LIVE_MODE] Enabled (%s)\n", reason ? reason : "unspecified");
        }
        live_mode_active = 1;
        // Entering live mode always re-applies safety (idempotent) so callers
        // can assert policy without first checking current state.
        enforce_live_mode_safety();
    } else {
        if (live_mode_active) {
            fprintf(stderr, "[LIVE_MODE] Disabled (%s)\n", reason ? reason : "unspecified");
        }
        live_mode_active = 0;
    }
}

static int degraded_state_has(unsigned int flag) {
    return (degraded_state_flags & flag) != 0;
}

static void degraded_state_set(unsigned int flag, const char* reason) {
    if (!degraded_state_has(flag)) {
        fprintf(stderr, "[DEGRADED] ENTER flag=0x%X reason=%s\n", flag, reason ? reason : "unspecified");
    }
    // Bitflag model allows simultaneous degraded causes to accumulate without
    // losing context from prior failures.
    degraded_state_flags |= flag;
}

static void degraded_state_clear(unsigned int flag, const char* reason) {
    if (degraded_state_has(flag)) {
        fprintf(stderr, "[DEGRADED] CLEAR flag=0x%X reason=%s\n", flag, reason ? reason : "unspecified");
    }
    degraded_state_flags &= ~flag;
}

static void log_degraded_state(const char* context) {
    if (degraded_state_flags == 0) {
        fprintf(stderr, "[DEGRADED] %s: nominal\n", context ? context : "state");
    } else {
        fprintf(stderr, "[DEGRADED] %s: active_flags=0x%X\n", context ? context : "state", degraded_state_flags);
    }
}

static void enforce_transition_safe_hold(const char* reason) {
    fprintf(stderr, "[FAILSAFE] Transition safe-hold active: %s\n", reason ? reason : "unspecified");

    if (christie_ctx) {
        fprintf(stderr, "[FAILSAFE] Closing projector shutter\n");
        christie_shutter(christie_ctx, 1);
    }

    if (mqtt_ctx) {
        fprintf(stderr, "[FAILSAFE] Muting CP950A\n");
        mqtt_mute_cp950a(mqtt_ctx);
    }

    // Safe-hold is an intentional fail-closed posture: shutter closed + audio
    // muted until critical transition risk is cleared.
    degraded_state_set(DEGRADED_TRANSITION_SAFEHOLD, reason);
    log_degraded_state("transition-safe-hold");
}

static int json_extract_string(const char* json, const char* key, char* out, size_t out_size) {
    if (!json || !key || !out || out_size == 0) {
        return -1;
    }

    char pattern[64] = {0};
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* key_pos = strstr(json, pattern);
    if (!key_pos) {
        return -1;
    }

    const char* colon = strchr(key_pos, ':');
    if (!colon) {
        return -1;
    }

    const char* p = colon + 1;
    // Minimal parser for constrained control payloads.
    // Implication: this is intentionally not full JSON parsing and should only
    // be used with simple trusted key/value messages.
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p != '"') {
        return -1;
    }

    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < out_size - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';

    return (i > 0) ? 0 : -1;
}

static int json_extract_int64(const char* json, const char* key, int64_t* value_out) {
    if (!json || !key || !value_out) {
        return -1;
    }

    char pattern[64] = {0};
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* key_pos = strstr(json, pattern);
    if (!key_pos) {
        return -1;
    }

    const char* colon = strchr(key_pos, ':');
    if (!colon) {
        return -1;
    }

    char* end_ptr = NULL;
    long long parsed = strtoll(colon + 1, &end_ptr, 10);
    if (end_ptr == colon + 1) {
        return -1;
    }

    *value_out = (int64_t)parsed;
    return 0;
}

static void escape_json(const char* in, char* out, size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }

    if (!in) {
        out[0] = '\0';
        return;
    }

    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j < out_size - 1; i++) {
        if ((in[i] == '"' || in[i] == '\\') && j < out_size - 2) {
            out[j++] = '\\';
        }
        out[j++] = in[i];
    }
    out[j] = '\0';
}

static void handle_mqtt_content_add(const char* payload, void* user_data) {
    (void)user_data;

    if (!mqtt_ctx || !payload) {
        return;
    }

    char file_path[512] = {0};
    char title[256] = {0};
    int64_t delay_ms = 0;
    char response_line[2048] = {0};
    char response_json[2300] = {0};

    if (json_extract_string(payload, "file", file_path, sizeof(file_path)) != 0) {
        snprintf(response_json, sizeof(response_json),
                 "{\"status\":\"error\",\"code\":\"ERROR_INVALID_PAYLOAD\",\"reason\":\"missing file\"}");
        mqtt_publish_content_add_response(mqtt_ctx, response_json);
        return;
    }

    if (json_extract_int64(payload, "delay_ms", &delay_ms) != 0) {
        delay_ms = 0;
    }
    (void)json_extract_string(payload, "title", title, sizeof(title));

    // Serialize playlist mutation with playback state changes so "first file"
    // activation and power-on sequencing cannot race with TCP commands.
    pthread_mutex_lock(&player_mutex);
    int was_empty = 0;
    int result = add_file_with_caldav_check(file_path, delay_ms, title[0] ? title : NULL,
                                            response_line, sizeof(response_line), &was_empty);
    if (result == 0 && was_empty) {
        fprintf(stderr, "[MQTT] First file added from content/add - initiating power-on sequence\n");
        power_on_and_preload();
    }
    pthread_mutex_unlock(&player_mutex);

    if (result == 0) {
        int index = -1;
        int parsed_empty = was_empty;
        (void)sscanf(response_line, "STATUS_FILE_ADDED|index=%d|was_empty=%d", &index, &parsed_empty);
        snprintf(response_json, sizeof(response_json),
                 "{\"status\":\"ok\",\"code\":\"STATUS_FILE_ADDED\",\"index\":%d,\"was_empty\":%d}",
                 index, parsed_empty);
    } else if (strncmp(response_line, "ERROR_CALDAV_CONFLICT|reason=", 28) == 0) {
        char escaped_reason[1200] = {0};
        const char* reason = response_line + 28;
        char reason_copy[1024] = {0};
        strncpy(reason_copy, reason, sizeof(reason_copy) - 1);
        reason_copy[strcspn(reason_copy, "\r\n")] = '\0';
        escape_json(reason_copy, escaped_reason, sizeof(escaped_reason));
        snprintf(response_json, sizeof(response_json),
                 "{\"status\":\"error\",\"code\":\"ERROR_CALDAV_CONFLICT\",\"reason\":\"%s\"}",
                 escaped_reason);
    } else {
        snprintf(response_json, sizeof(response_json),
                 "{\"status\":\"error\",\"code\":\"ERROR_CANNOT_ADD_FILE\"}");
    }

    mqtt_publish_content_add_response(mqtt_ctx, response_json);
}

/**
 * @brief Resolve ALSA device name for RME output.
 * Priority: PROJECTION_RME_DEVICE env var -> routed map -> legacy hardware path
 */
static const char* get_rme_device(void) {
    const char* env_device = getenv("PROJECTION_RME_DEVICE");
    if (env_device && env_device[0] != '\0') {
        return env_device;
    }
    return RME_DEVICE_DEFAULT;
}

/**
 * @brief Apply DXD-16 preset selection based on source frame-rate.
 */
static int apply_dxd16_preset_for_file(const char* file_path, const char* context_label) {
    if (!file_path || !file_path[0]) {
        fprintf(stderr, "[DXD16][DECISION] %s: skipped (no media path)\n",
                context_label ? context_label : "Timing profile");
        return 1;
    }

    if (!dxd16_ctx || !dxd16_ctx->enabled) {
        // Treat unavailable timing control as soft-skip (not immediate error)
        // so non-DXD deployments can still run while logging explicit intent.
        fprintf(stderr, "[DXD16][DECISION] %s: skipped (control unavailable)\n",
                context_label ? context_label : "Timing profile");
        return 1;
    }

    double detected_fps = 0.0;
    DXD16Preset selected = DXD16_PRESET_UNKNOWN;
    if (dxd16_apply_for_media_file(dxd16_ctx, file_path, &detected_fps, &selected) == 0) {
        fprintf(stderr, "[DXD16] %s: %.3f fps -> %s\n",
                context_label ? context_label : "Timing profile",
                detected_fps,
                dxd16_preset_name(selected));
        degraded_state_clear(DEGRADED_DXD16_TIMING, "DXD16 preset applied");
        fprintf(stderr, "[DXD16][DECISION] %s: success\n", context_label ? context_label : "Timing profile");
        return 0;
    } else {
        fprintf(stderr, "[DXD16] %s: unable to apply preset for %s\n",
                context_label ? context_label : "Timing profile",
                file_path);
        degraded_state_set(DEGRADED_DXD16_TIMING, "DXD16 preset apply failed");
        fprintf(stderr, "[DXD16][DECISION] %s: failure\n", context_label ? context_label : "Timing profile");
        return -1;
    }
}

/**
 * @brief Set cinema to idle/standby state (empty playlist)
 */
static void enter_idle_state(void) {
    fprintf(stderr, "\n[IDLE] Entering idle/standby state...\n");

    if (is_live_mode_active()) {
        fprintf(stderr, "[IDLE] Live mode active: skipping curtain/lighting automation\n");
        enforce_live_mode_safety();
        fprintf(stderr, "[IDLE] Live mode standby state enforced\n");
        return;
    }
    
    // House lights up
    if (mqtt_ctx) {
        fprintf(stderr, "[IDLE] Setting house lights to full (100%%)\n");
        mqtt_house_lights(mqtt_ctx, 100);
    }
    
    // Close shutter
    if (christie_ctx) {
        fprintf(stderr, "[IDLE] Closing shutter on projector\n");
        christie_shutter(christie_ctx, 1);  // 1 = closed
    }
    
    // Set laser power to 0
    if (christie_ctx) {
        fprintf(stderr, "[IDLE] Setting laser power to 0\n");
        christie_laser_power(christie_ctx, 0);
    }
    
    // Mute CP950A
    if (mqtt_ctx) {
        fprintf(stderr, "[IDLE] Muting CP950A\n");
        mqtt_mute_cp950a(mqtt_ctx);
    }
    
    // Close curtains
    if (mqtt_ctx) {
        fprintf(stderr, "[IDLE] Closing curtains\n");
        mqtt_curtains(mqtt_ctx, 1);  // 1 = closed
    }
    
    
    fprintf(stderr, "[IDLE] Ready for standby (awaiting queue)\n");
}

/**
 * @brief Helper: Add file with CalDAV conflict checking
 * Returns response string suitable for TCP/MQTT reply
 * Sets output params on success
 */
static int add_file_with_caldav_check(const char* file_path, int64_t start_delay_ms,
                                      const char* title_in, 
                                      char* response_out, int response_len,
                                      int* was_empty_out) {
    if (!file_path || !response_out || !was_empty_out) return -1;
    
    *was_empty_out = playlist_is_empty(playlist_ctx);
    
    char title[256] = {0};
    if (title_in && title_in[0]) {
        strncpy(title, title_in, 255);
    } else {
        // Generate display title from filename
        const char* basename = strrchr(file_path, '/');
        if (!basename) basename = strrchr(file_path, '\\');
        if (basename) basename++;
        else basename = file_path;
        strncpy(title, basename, 255);
    }
    
    // Check for CalDAV conflicts BEFORE adding to playlist
    if (caldav_ctx) {
        extern int64_t metadata_get_duration_ms(const char* file_path);
        int64_t duration_ms = metadata_get_duration_ms(file_path);
        
        if (duration_ms > 0) {
            time_t start_time = time(NULL) + (start_delay_ms / 1000);
            time_t end_time = start_time + (duration_ms / 1000);
            
            char conflict_reason[512] = {0};
            if (caldav_check_conflicts(caldav_ctx, start_time, end_time, conflict_reason) != 0) {
                // Conflict detected - reject the addition
                snprintf(response_out, response_len, 
                        "ERROR_CALDAV_CONFLICT|reason=%s\r\n", conflict_reason);
                fprintf(stderr, "[ADD] File addition rejected: %s\n", conflict_reason);
                return -1;
            }
        }
    }
    
    // Add to playlist
    if (playlist_add_file_timed(playlist_ctx, file_path, start_delay_ms, 0, title) != 0) {
        snprintf(response_out, response_len, "ERROR_CANNOT_ADD_FILE\r\n");
        return -1;
    }
    
    // Create CalDAV event for tracking
    if (caldav_ctx) {
        extern int64_t metadata_get_duration_ms(const char* file_path);
        int64_t duration_ms = metadata_get_duration_ms(file_path);
        
        if (duration_ms > 0) {
            time_t start_time = time(NULL) + (start_delay_ms / 1000);
            
            if (caldav_create_and_post_event(caldav_ctx, title, file_path,
                                            start_time, duration_ms, NULL) == 0) {
                fprintf(stderr, "[ADD] CalDAV event created: %s\n", title);
            } else {
                fprintf(stderr, "[ADD] Warning: CalDAV event creation failed (non-fatal)\n");
            }
        }
    }
    
    snprintf(response_out, response_len, 
            "STATUS_FILE_ADDED|index=%d|was_empty=%d\r\n", 
            playlist_count(playlist_ctx) - 1, *was_empty_out);
    
    return 0;
}

/**
 * @brief Power-on sequence for first file in queue
 */
static void power_on_and_preload(void) {
    fprintf(stderr, "\n[POWER_ON] Beginning power-on sequence...\n");

    if (is_live_mode_active()) {
        fprintf(stderr, "[POWER_ON] Live mode active: blocking projector/audio/curtain automation\n");
        enforce_live_mode_safety();
        return;
    }

    // Select DXD-16 timing domain from first queued file before hardware warmup.
    const char* first_file = playlist_current_file(playlist_ctx);
    if (first_file) {
        if (apply_dxd16_preset_for_file(first_file, "Power-on") < 0) {
            fprintf(stderr, "[POWER_ON] Warning: DXD16 timing selection failed (continuing degraded)\n");
            log_degraded_state("power-on");
        }
    }

    // Startup guard: if output mapping enforcement failed, abort power-on into
    // safe-hold to avoid presenting with uncertain SDI topology.
    if (degraded_state_has(DEGRADED_DECKLINK_2SI)) {
        enforce_transition_safe_hold("DeckLink 2SI not enforced at startup");
        return;
    }
    
    // Power on projector
    if (christie_ctx) {
        fprintf(stderr, "[POWER_ON] Powering on Christie projector\n");
        christie_power_on(christie_ctx);
        
        // Wait for projector to reach ready state (max 30 seconds)
        int ready_count = 0;
        int max_attempts = 30;  // 30 x 1 second = 30 seconds
        
        fprintf(stderr, "[POWER_ON] Waiting for projector ready state...\n");
        while (ready_count < max_attempts) {
            usleep(1000000);  // 1 second
            
            int is_ready = christie_is_ready(christie_ctx);
            if (is_ready == 1) {
                fprintf(stderr, "[POWER_ON] ✓ Projector is ready\n");
                break;
            } else if (is_ready == 0) {
                fprintf(stderr, "[POWER_ON] Still warming up... (%d/%d)\n", ready_count + 1, max_attempts);
            }
            ready_count++;
        }
    }
    
    // Power on CP950A and unmute (via MQTT)
    if (mqtt_ctx) {
        fprintf(stderr, "[POWER_ON] Unmuting CP950A\n");
        mqtt_unmute_cp950a(mqtt_ctx);
    }
    
    // Open curtains
    if (mqtt_ctx) {
        fprintf(stderr, "[POWER_ON] Opening curtains\n");
        mqtt_curtains(mqtt_ctx, 0);  // 0 = open
    }
    
    // Update marquee with first file info
    first_file = playlist_current_file(playlist_ctx);
    if (first_file) {
        const char* title = playlist_get_title(playlist_ctx, 0);
        int64_t duration_ms = playlist_get_duration(playlist_ctx, 0);
        int duration_sec = (int)(duration_ms / 1000);
        
        // Determine aspect ratio for display format
        ProjectionMediaMetadata meta = metadata_read_mp4(first_file);
        char format_str[64] = {0};
        
        if (meta.aspect_ratio > 2.3f) {
            snprintf(format_str, sizeof(format_str), "scope_%.2f", meta.aspect_ratio);
        } else if (meta.aspect_ratio > 1.8f) {
            snprintf(format_str, sizeof(format_str), "flat_%.2f", meta.aspect_ratio);
        } else {
            snprintf(format_str, sizeof(format_str), "custom_%.2f", meta.aspect_ratio);
        }
        
        fprintf(stderr, "[POWER_ON] Updating marquee: %s (%s)\n", title, format_str);
        if (mqtt_ctx) {
            mqtt_update_marquee(mqtt_ctx, title, format_str, duration_sec, NULL);
        }
    }
    
    fprintf(stderr, "[POWER_ON] Power-on sequence complete\n");
}

/**
 * @brief Check CalDAV for upcoming shows and auto-queue if needed
 * Polls once every 60 seconds to avoid excessive network traffic
 */
static void check_caldav_queue(void) {
    static time_t last_caldav_check = 0;
    time_t now = time(NULL);
    
    if (!caldav_ctx || (now - last_caldav_check) < 60) {
        return;  // Not time for next check yet
    }
    
    last_caldav_check = now;
    
    char show_title[256] = {0};
    char file_path[512] = {0};
    time_t show_time = 0;
    time_t prep_seconds = 0;
    ShowType show_type = SHOW_TYPE_MEDIA;
    
    if (caldav_get_next_show(caldav_ctx, show_title, file_path, &show_time, &prep_seconds, &show_type) != 0) {
        return;  // No upcoming shows
    }

    // Live-stage events are occupancy signals, not media playout jobs.
    // Decision impact: enable live safety posture and skip queue mutation.
    if (show_type == SHOW_TYPE_LIVE_STAGE) {
        set_live_mode_active(1, "live-stage event scheduled");
        fprintf(stderr, "[CALDAV] Live-stage event scheduled: %s (automation suppressed)\n", show_title);
        return;
    }

    if (is_live_mode_active()) {
        set_live_mode_active(0, "media event scheduled");
    }
    
    // Check if this show is already in the playlist
    for (int i = 0; i < playlist_count(playlist_ctx); i++) {
        const char* queued = playlist_get_file(playlist_ctx, i);
        if (queued && strcmp(queued, file_path) == 0) {
            fprintf(stderr, "[CALDAV] Show already queued: %s\n", show_title);
            return;
        }
    }
    
    // Auto-queue the show
    int was_empty = playlist_is_empty(playlist_ctx);
    
    // If show is more than 1 hour away, queue it with delay until show time
    int64_t delay_ms = 0;
    if (prep_seconds > 3600) {  // 1 hour threshold
        // Queue close to showtime to reduce long-idle preloaded entries while
        // still guaranteeing hardware warmup runway.
        delay_ms = (prep_seconds - 600) * 1000;  // Queue 10 minutes before show
    }
    
    if (playlist_add_file_timed(playlist_ctx, file_path, delay_ms, show_time, show_title) == 0) {
        fprintf(stderr, "[CALDAV] ✓ Auto-queued: %s (starts in %ld seconds)\n", 
               show_title, (long)prep_seconds);
        
        // Trigger power-on if this was first file
        if (was_empty) {
            fprintf(stderr, "[CALDAV] First file from calendar - initiating power-on\n");
            power_on_and_preload();
        }
    } else {
        fprintf(stderr, "[CALDAV] Failed to auto-queue: %s\n", show_title);
    }
}

/**
 * @brief Check idle timeout and power off if needed
 */
static void check_idle_timeout(void) {
    if (!playlist_ctx || !playlist_is_empty(playlist_ctx)) {
        return;  // Playlist not empty
    }
    
    time_t idle_secs = playlist_idle_seconds(playlist_ctx);
    if (idle_secs >= IDLE_TIMEOUT_SECONDS) {
        fprintf(stderr, "\n[IDLE] Idle timeout reached (%.0f seconds). Powering off...\n", 
                (double)idle_secs);

        if (is_live_mode_active()) {
            fprintf(stderr, "[IDLE] Live mode active: enforcing projector OFF + CP950A MUTE only\n");
            enforce_live_mode_safety();
            return;
        }
        
        // Power off projector
        if (christie_ctx) {
            fprintf(stderr, "[IDLE] Powering off Christie projector\n");
            christie_power_off(christie_ctx);
        }
        
        // Power off CP950A (via MQTT)
        if (mqtt_ctx) {
            fprintf(stderr, "[IDLE] Powering off CP950A\n");
            // Send power-off command - implementation depends on your MQTT infrastructure
            mqtt_publish_command(mqtt_ctx, "theater/hardware/cmd", 
                               "{\"device\":\"dolby_cp950a\",\"action\":\"power_off\"}");
        }
        
        fprintf(stderr, "[IDLE] Theater powered down\n");
    }
}

/**
 * @brief Check and trigger macro cues based on current playback position
 * @param player Active LibVLC player
 * @param metadata Current file's metadata with cues
 */
static void check_and_trigger_macrocues(libvlc_media_player_t* player, 
                                        const ProjectionMediaMetadata* metadata) {
    if (!player || !metadata || metadata->cue_count == 0) {
        return;
    }

    if (is_live_mode_active()) {
        return;
    }
    
    // Get current playback position in milliseconds
    int64_t current_pos_ms = libvlc_media_player_get_time(player);
    
    // Check each cue to see if we've passed its timecode
    for (int i = 0; i < metadata->cue_count; i++) {
        // Skip if we've already triggered this cue
        if (i <= metadata->last_triggered_cue_index) {
            continue;
        }
        
        // Check if current position has passed this cue's timecode
        // Trigger-once behavior is enforced via last_triggered_cue_index.
        // Implication: cue delivery is monotonic as playback advances.
        if (current_pos_ms >= (int64_t)metadata->cues[i].timecode_ms) {
            fprintf(stderr, "\n[MACRO_CUE] TRIGGERED: %s @ %lu ms (playback @ %lu ms)\n",
                   metadata->cues[i].cue_type, 
                   (unsigned long)metadata->cues[i].timecode_ms,
                   (unsigned long)current_pos_ms);
            
            // Publish MQTT event for theater automation
            if (mqtt_ctx) {
                char payload[512] = {0};
                snprintf(payload, sizeof(payload),
                        "{\"cue_type\":\"%s\",\"timecode_ms\":%u}",
                        metadata->cues[i].cue_type,
                        metadata->cues[i].timecode_ms);
                
                mqtt_publish_command(mqtt_ctx, "theater/macro_cue", payload);
                fprintf(stderr, "[MACRO_CUE] Published MQTT: theater/macro_cue\n");
            }
        }
    }
}

/**
 * @brief Signal handler for graceful shutdown
 */
void signal_handler(int sig) {
    fprintf(stderr, "\n[ENGINE] Received signal %d, shutting down...\n", sig);
    should_exit = 1;
}

/**
 * @brief Callback: LibVLC media event
 */
void on_vlc_event(const libvlc_event_t* event, void* data) {
    (void)data;
    switch (event->type) {
        case libvlc_MediaPlayerEndReached:
            // EndReached is authoritative handover trigger in this design.
            // Control implication: queue advance + player swap happens from
            // event callback path, not polling loop.
            fprintf(stderr, "[ENGINE] Playback ended, performing handover...\n");
            handle_end_reached_handover();
            break;
        case libvlc_MediaPlayerPlaying:
            fprintf(stderr, "[ENGINE] Playback started\n");
            break;
        case libvlc_MediaPlayerPaused:
            fprintf(stderr, "[ENGINE] Playback paused\n");
            break;
        case libvlc_MediaPlayerEncounteredError:
            fprintf(stderr, "[ENGINE] ERROR: Playback error encountered\n");
            break;
        default:
            break;
    }
}

/**
 * @brief Initialize LibVLC with cinema-specific parameters
 */
static int vlc_init(void) {
    char alsa_device_arg[128] = {0};
    snprintf(alsa_device_arg, sizeof(alsa_device_arg), "--alsa-audio-device=%s", get_rme_device());

    int content_tolerance_enabled = 1;
    const char* disable_content_tolerance = getenv("PROJECTION_DISABLE_CONTENT_TOLERANCE");
    if (disable_content_tolerance &&
        (strcmp(disable_content_tolerance, "1") == 0 ||
         strcasecmp(disable_content_tolerance, "true") == 0 ||
         strcasecmp(disable_content_tolerance, "yes") == 0)) {
        content_tolerance_enabled = 0;
    }

    const char* vlc_args[40] = {0};
    int arg_count = 0;

    vlc_args[arg_count++] = "--intf=dummy";

    // Video output to DeckLink 8K Pro G2 base card
    vlc_args[arg_count++] = "--vout=decklink";
    vlc_args[arg_count++] = "--decklink-ten-bits";               // Enables 10-bit color for the Christie imager

    if (content_tolerance_enabled) {
        // Tolerance path favors robust playback across heterogeneous masters.
        // Implication: strict source-signaling fidelity may be traded for
        // predictable output behavior on mixed ingest catalogs.
        // Content tolerance mitigation path: normalize SDR gamut toward Rec.2020 and preserve HDR passthrough behavior.
        vlc_args[arg_count++] = "--video-filter=colorspace{all=bt2020nc:primaries=bt2020}";
        vlc_args[arg_count++] = "--target-prim=bt2020";
        vlc_args[arg_count++] = "--vlc-tonemap-algo=none";
        vlc_args[arg_count++] = "--swscale-mode=0";
    }
        
    // Constant Image Height (Anamorphic Stretch Engine)
    vlc_args[arg_count++] = "--force-aspect-ratio=17:9";         // Disables file metadata; forces 17:9 stretch
    vlc_args[arg_count++] = "--zoom=1.0";                        // Forces full pixel mapping
    vlc_args[arg_count++] = "--no-autoscale";                    // Disables file-specific aspect ratio logic

    // Audio output to RME HDSPe AES over ALSA
    vlc_args[arg_count++] = "--aout=alsa";
    vlc_args[arg_count++] = alsa_device_arg;
    vlc_args[arg_count++] = "--audio-channels=16";               // Configures 16 physical output channels
    vlc_args[arg_count++] = "--audio-filter=none";               // Completely disables internal volume scaling
    vlc_args[arg_count++] = "--no-audio-time-stretch";           // Disables software resampling; defers to DXD-16
        
    // -------------------------------------------------------------
    // Selective Dolby Passthrough & DTS Local Decoding Matrix
    // -------------------------------------------------------------
    vlc_args[arg_count++] = "--spdif";                           // Activates binary passthrough subsystem capabilities
    vlc_args[arg_count++] = "--codec=a52";                       // STRICT: Only pass A/52 (Dolby AC-3 / TrueHD) raw. Omit "dts"
    vlc_args[arg_count++] = "--no-dts-passthrough";              // Force VLC to intercept and decode DTS inside the host PC
    // -------------------------------------------------------------

    // Timing & Buffer Management for Multi-Card Stability
    vlc_args[arg_count++] = "--clock-jitter=0";                  // Disables internal system software timing smoothing
    vlc_args[arg_count++] = "--clock-synchro=0";                 // 0 = Disable. Allows the RME and DeckLink hardware clocks to lead
    vlc_args[arg_count++] = "--cr-average=10000";                // Maximum frame smoothing index to match DXD-16
        
    // Interface Cleanliness & Latency Buffers
    vlc_args[arg_count++] = "--no-osd";
    vlc_args[arg_count++] = "--no-video-title-show";
    vlc_args[arg_count++] = "--network-caching=30000";
    vlc_args[arg_count++] = "--file-caching=30000";
    
    vlc_instance = libvlc_new(arg_count, vlc_args);
    if (!vlc_instance) {
        fprintf(stderr, "[ENGINE] Error initializing LibVLC\n");
        return -1;
    }

    if (content_tolerance_enabled) {
        fprintf(stderr, "[ENGINE] LibVLC initialized with content-tolerance video policy\n");
    } else {
        fprintf(stderr, "[ENGINE] LibVLC initialized with content-tolerance policy disabled\n");
    }
    fprintf(stderr, "[ENGINE] LibVLC initialized successfully for Selective Dolby Passthrough\n");
    return 0;
}


/**
 * @brief Create and configure media player instance
 */
static libvlc_media_player_t* create_player(const char* file_path) {
    if (!vlc_instance || !file_path) return NULL;
    
    libvlc_media_t* media = libvlc_media_new_path(vlc_instance, file_path);
    if (!media) {
        fprintf(stderr, "[ENGINE] Error creating media for: %s\n", file_path);
        return NULL;
    }
    
    libvlc_media_player_t* player = libvlc_media_player_new_from_media(media);
    libvlc_media_release(media);
    
    if (!player) {
        fprintf(stderr, "[ENGINE] Error creating media player\n");
        return NULL;
    }
    
    // Set aspect ratio for Christie optics
    libvlc_video_set_aspect_ratio(player, "256:135");
    
    // Register event callbacks
    libvlc_event_manager_t* em = libvlc_media_player_event_manager(player);
    libvlc_event_attach(em, libvlc_MediaPlayerEndReached, on_vlc_event, NULL);
    libvlc_event_attach(em, libvlc_MediaPlayerPlaying, on_vlc_event, NULL);
    libvlc_event_attach(em, libvlc_MediaPlayerPaused, on_vlc_event, NULL);
    libvlc_event_attach(em, libvlc_MediaPlayerEncounteredError, on_vlc_event, NULL);
    
    fprintf(stderr, "[ENGINE] Created media player for: %s\n", file_path);
    return player;
}

/**
 * @brief Execute transition state: prepare for next title
 */
static void execute_transition(const char* target_file) {
    fprintf(stderr, "\n[TRANSITION] Beginning state machine...\n");

    if (is_live_mode_active()) {
        fprintf(stderr, "[TRANSITION] Live mode active: suppressing transition automation\n");
        enforce_live_mode_safety();
        fprintf(stderr, "[TRANSITION] Live mode safety hold complete\n\n");
        return;
    }

    // Transition precondition gate: refuse state-machine progression when SDI
    // output mapping is degraded, forcing fail-closed behavior.
    if (degraded_state_has(DEGRADED_DECKLINK_2SI)) {
        enforce_transition_safe_hold("DeckLink 2SI enforcement degraded");
        return;
    }

    int transition_critical_ok = 1;
    
    // Step 1: Close shutter on projector
    if (christie_ctx) {
        fprintf(stderr, "[TRANSITION] Sending shutter close command to Christie...\n");
        christie_shutter(christie_ctx, 1);  // 1 = closed
    }
    
    // Step 2: Mute Dolby CP950A (keep RME streaming for sync)
    if (mqtt_ctx) {
        fprintf(stderr, "[TRANSITION] Muting CP950A output...\n");
        mqtt_mute_cp950a(mqtt_ctx);
    }
    
    // Step 3: Load metadata for the transition target.
    // Decision control: all downstream optics/audio adjustments derive from
    // this metadata snapshot.
    const char* next_file = target_file ? target_file : playlist_next_file(playlist_ctx);
    if (next_file) {
        if (apply_dxd16_preset_for_file(next_file, "Transition") < 0) {
            // DXD timing recall is treated as transition-critical because it
            // directly affects sync stability across frame-rate families.
            transition_critical_ok = 0;
        }

        fprintf(stderr, "[TRANSITION] Reading metadata for next file: %s\n", next_file);
        current_metadata = metadata_read_mp4(next_file);
        if (decklink_inject_hdr_ancillary_for_media(next_file, &current_metadata) != 0) {
            fprintf(stderr, "[TRANSITION] Warning: DeckLink HDR ancillary injection failed (non-fatal)\n");
        }
        
        // Calculate laser power using CIH formula with detected aspect ratio
        float aspect = current_metadata.aspect_ratio;
        int laser_level = (int)((435.484f * aspect) - 225.154f);
        if (laser_level < 200) laser_level = 200;
        if (laser_level > 1000) laser_level = 1000;
        
        // Step 4: Set laser power
        if (christie_ctx) {
            fprintf(stderr, "[TRANSITION] Setting laser power to %d (aspect %.2f:1)\n", laser_level, aspect);
            christie_laser_power(christie_ctx, laser_level);
        }
        
        // Step 5: Detect aspect ratio changes and adjust lens/masking
        float ratio_diff = fabs(current_metadata.aspect_ratio - previous_aspect_ratio);
        if (ratio_diff > 0.05f) {  // Threshold to avoid oscillation (0.05 ≈ 1 pixel drift on 4K)
            fprintf(stderr, "[TRANSITION] Aspect ratio change detected: %.2f → %.2f\n", 
                    previous_aspect_ratio, current_metadata.aspect_ratio);
            
            // Determine masking configuration based on aspect ratio
            const char* masking_config = NULL;
            const char* lens_position = NULL;
            
            if (aspect > 2.3f) {
                // Scope (2.39:1 or wider)
                masking_config = "{\"mode\":\"scope\",\"left_curtain\":0,\"right_curtain\":0,\"top_blind\":10,\"bottom_blind\":10}";
                lens_position = "{\"target_position\":\"full_aperture\",\"settling_time_ms\":500}";
                fprintf(stderr, "[TRANSITION] Configuring for SCOPE (%.2f:1)...\n", aspect);
            } else if (aspect > 1.8f && aspect <= 2.3f) {
                // Academy Flat (1.85:1 - 2.00:1)
                masking_config = "{\"mode\":\"flat\",\"left_curtain\":150,\"right_curtain\":150,\"top_blind\":5,\"bottom_blind\":5}";
                lens_position = "{\"target_position\":\"reduced_aperture\",\"settling_time_ms\":500}";
                fprintf(stderr, "[TRANSITION] Configuring for FLAT (%.2f:1)...\n", aspect);
            } else {
                // Other formats (1.37:1, etc.)
                masking_config = "{\"mode\":\"custom\",\"left_curtain\":200,\"right_curtain\":200,\"top_blind\":50,\"bottom_blind\":50}";
                lens_position = "{\"target_position\":\"custom\",\"settling_time_ms\":500}";
                fprintf(stderr, "[TRANSITION] Configuring for CUSTOM (%.2f:1)...\n", aspect);
            }
            
            // Send MQTT lens sled command
            if (mqtt_ctx && lens_position) {
                fprintf(stderr, "[TRANSITION] Sending lens sled command...\n");
                mqtt_publish_command(mqtt_ctx, "lens_sled", lens_position);
            }
            
            // Send MQTT masking curtains command
            if (mqtt_ctx && masking_config) {
                fprintf(stderr, "[TRANSITION] Sending masking curtains command...\n");
                mqtt_publish_command(mqtt_ctx, "masking_curtains", masking_config);
            }
            
            // Update previous aspect ratio tracking
            previous_aspect_ratio = current_metadata.aspect_ratio;
        } else {
            fprintf(stderr, "[TRANSITION] Aspect ratio stable (%.2f:1), no masking adjustment needed\n", aspect);
        }
    }

    if (!transition_critical_ok) {
        // Fail-safe branch: do not open shutter/unmute when transition-critical
        // timing control failed.
        enforce_transition_safe_hold("DXD16 preset recall failed during transition");
        return;
    }
    
    // Wait for mechanical hardware to settle
    fprintf(stderr, "[TRANSITION] Waiting for mechanical settle (50ms)...\n");
    usleep(50000);
    
    fprintf(stderr, "[TRANSITION] Opening shutter...\n");
    if (christie_ctx) {
        christie_shutter(christie_ctx, 0);  // 0 = open
    }
    
    // Unmute CP950A
    if (mqtt_ctx) {
        fprintf(stderr, "[TRANSITION] Unmuting CP950A output...\n");
        mqtt_unmute_cp950a(mqtt_ctx);
    }

    degraded_state_clear(DEGRADED_TRANSITION_SAFEHOLD, "transition completed");
    log_degraded_state("transition-complete");
    
    // Apply AV sync offset if present
    if (alsa_ctx && current_metadata.av_sync_offset_samples != 0) {
        fprintf(stderr, "[TRANSITION] Applying AV sync offset: %d samples\n", current_metadata.av_sync_offset_samples);
        alsa_set_av_sync_offset(alsa_ctx, current_metadata.av_sync_offset_samples);
    }
    
    fprintf(stderr, "[TRANSITION] State machine complete, ready for playback\n\n");
}

/**
 * @brief TCP remote control server thread
 */
void* tcp_control_thread(void* arg) {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    char buffer[512];
    char response[2048];
    socklen_t addr_len;
    
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        fprintf(stderr, "[TCP] Error creating socket\n");
        pthread_exit(NULL);
    }
    
    int reuse = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(TCP_CONTROL_PORT);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "[TCP] Error binding socket\n");
        close(server_socket);
        pthread_exit(NULL);
    }
    
    listen(server_socket, 5);
    fprintf(stderr, "[TCP] Control server listening on port %d\n", TCP_CONTROL_PORT);
    
    while (!should_exit) {
        addr_len = sizeof(client_addr);
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
        
        if (client_socket < 0) continue;
        
        memset(buffer, 0, sizeof(buffer));
        memset(response, 0, sizeof(response));
        ssize_t bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes > 0) {
            // Strip whitespace/newlines
            buffer[strcspn(buffer, "\r\n")] = 0;
            fprintf(stderr, "[TCP] Command: %s\n", buffer);
            
            pthread_mutex_lock(&player_mutex);
            libvlc_media_player_t* active = current_player_is_A ? player_A : player_B;
            
            // Playback transport controls
            if (strcmp(buffer, "PLAY") == 0) {
                if (active) {
                    libvlc_media_player_play(active);
                    snprintf(response, sizeof(response), "STATUS_OK\r\n");
                } else {
                    snprintf(response, sizeof(response), "ERROR_NO_MEDIA\r\n");
                }
            } 
            else if (strcmp(buffer, "PAUSE") == 0) {
                if (active) {
                    libvlc_media_player_set_pause(active, 1);
                    snprintf(response, sizeof(response), "STATUS_OK\r\n");
                } else {
                    snprintf(response, sizeof(response), "ERROR_NO_MEDIA\r\n");
                }
            } 
            else if (strcmp(buffer, "STOP") == 0) {
                if (active) libvlc_media_player_stop(active);
                snprintf(response, sizeof(response), "STATUS_OK\r\n");
            }
            else if (strcmp(buffer, "NEXT") == 0) {
                if (playlist_advance(playlist_ctx) == 0) {
                    const char* next_file = playlist_current_file(playlist_ctx);
                    if (next_file) {
                        execute_transition(next_file);
                        libvlc_media_player_t* next_player = create_player(next_file);
                        if (next_player) {
                            if (current_player_is_A) {
                                if (player_B) libvlc_media_player_release(player_B);
                                player_B = next_player;
                                libvlc_media_player_play(player_B);
                                current_player_is_A = 0;
                            } else {
                                if (player_A) libvlc_media_player_release(player_A);
                                player_A = next_player;
                                libvlc_media_player_play(player_A);
                                current_player_is_A = 1;
                            }
                            snprintf(response, sizeof(response), "STATUS_HANDOVER_FORCED\r\n");
                        }
                    }
                } else {
                    snprintf(response, sizeof(response), "ERROR_END_OF_PLAYLIST\r\n");
                }
            }
            else if (strcmp(buffer, "PREVIOUS") == 0) {
                if (playlist_rewind(playlist_ctx) == 0) {
                    const char* prev_file = playlist_current_file(playlist_ctx);
                    if (prev_file) {
                        execute_transition(prev_file);
                        libvlc_media_player_t* next_player = create_player(prev_file);
                        if (next_player) {
                            if (active) libvlc_media_player_stop(active);
                            if (current_player_is_A) {
                                if (player_B) libvlc_media_player_release(player_B);
                                player_B = next_player;
                                libvlc_media_player_play(player_B);
                                current_player_is_A = 0;
                            } else {
                                if (player_A) libvlc_media_player_release(player_A);
                                player_A = next_player;
                                libvlc_media_player_play(player_A);
                                current_player_is_A = 1;
                            }
                            snprintf(response, sizeof(response), "STATUS_PREVIOUS_TRACK\r\n");
                        }
                    }
                } else {
                    snprintf(response, sizeof(response), "ERROR_BEGINNING_OF_PLAYLIST\r\n");
                }
            }
            // Playlist management commands
            else if (strcmp(buffer, "LIST_PLAYLIST") == 0) {
                int count = playlist_count(playlist_ctx);
                int index = playlist_index(playlist_ctx);
                snprintf(response, sizeof(response), "PLAYLIST_INFO|count=%d|current_index=%d\r\n", count, index);
                
                // Append file list
                for (int i = 0; i < count; i++) {
                    const char* file = playlist_get_file(playlist_ctx, i);
                    if (file) {
                        char* marker = (i == index) ? " [CURRENT]" : "";
                        snprintf(response + strlen(response), sizeof(response) - strlen(response),
                                "%d|%s%s\r\n", i, file, marker);
                    }
                }
            }
            else if (strncmp(buffer, "ADD_FILE ", 9) == 0) {
                const char* cmd_args = buffer + 9;
                char file_path[512] = {0};
                int64_t start_delay_ms = 0;
                char title[256] = {0};
                
                // Parse: ADD_FILE <path> [start_delay_ms] [title]
                int parsed = sscanf(cmd_args, "%511s %lld", file_path, (long long*)&start_delay_ms);
                
                // Extract title if present (rest of line after path and delay)
                const char* title_start = strchr(cmd_args, '"');
                if (title_start) {
                    const char* title_end = strchr(title_start + 1, '"');
                    if (title_end && (title_end - title_start) < 255) {
                        strncpy(title, title_start + 1, title_end - title_start - 1);
                    }
                }
                
                int was_empty = 0;
                if (add_file_with_caldav_check(file_path, start_delay_ms, title[0] ? title : NULL,
                                              response, sizeof(response), &was_empty) == 0) {
                    // Successfully added
                    if (was_empty) {
                        fprintf(stderr, "[TCP] First file added - initiating power-on sequence\n");
                        power_on_and_preload();
                    }
                } else {
                    // Error response already in 'response' from helper
                }
            }
            else if (strncmp(buffer, "MQTT_CONTENT_ADD ", 17) == 0) {
                const char* mqtt_payload = buffer + 17;
                // Avoid self-deadlock: callback acquires player_mutex internally.
                pthread_mutex_unlock(&player_mutex);
                if (mqtt_stub_receive_content_add(mqtt_ctx, mqtt_payload) == 0) {
                    snprintf(response, sizeof(response), "STATUS_MQTT_CONTENT_ADD_ACCEPTED\r\n");
                } else {
                    snprintf(response, sizeof(response), "ERROR_MQTT_CONTENT_ADD_FAILED\r\n");
                }
                pthread_mutex_lock(&player_mutex);
            }
            else if (strncmp(buffer, "REMOVE_FILE ", 12) == 0) {
                int index = atoi(buffer + 12);
                if (playlist_remove_file(playlist_ctx, index) == 0) {
                    snprintf(response, sizeof(response), "STATUS_FILE_REMOVED\r\n");
                } else {
                    snprintf(response, sizeof(response), "ERROR_INVALID_INDEX\r\n");
                }
            }
            else if (strcmp(buffer, "CLEAR_PLAYLIST") == 0) {
                if (active) libvlc_media_player_stop(active);
                playlist_clear(playlist_ctx);
                snprintf(response, sizeof(response), "STATUS_PLAYLIST_CLEARED\r\n");
            }
            else if (strncmp(buffer, "PLAY_TRACK ", 11) == 0) {
                int index = atoi(buffer + 11);
                const char* file = playlist_get_file(playlist_ctx, index);
                if (file) {
                    execute_transition(file);
                    playlist_reset(playlist_ctx);
                    for (int i = 0; i < index; i++) playlist_advance(playlist_ctx);
                    
                    libvlc_media_player_t* next_player = create_player(file);
                    if (next_player) {
                        if (active) libvlc_media_player_stop(active);
                        if (current_player_is_A) {
                            if (player_B) libvlc_media_player_release(player_B);
                            player_B = next_player;
                            libvlc_media_player_play(player_B);
                            current_player_is_A = 0;
                        } else {
                            if (player_A) libvlc_media_player_release(player_A);
                            player_A = next_player;
                            libvlc_media_player_play(player_A);
                            current_player_is_A = 1;
                        }
                        snprintf(response, sizeof(response), "STATUS_PLAYING_TRACK|index=%d\r\n", index);
                    }
                } else {
                    snprintf(response, sizeof(response), "ERROR_INVALID_TRACK_INDEX\r\n");
                }
            }
            else {
                snprintf(response, sizeof(response), "ERROR_UNKNOWN_COMMAND\r\n");
            }
            
            pthread_mutex_unlock(&player_mutex);
            
            // Send response
            if (strlen(response) > 0) {
                send(client_socket, response, strlen(response), 0);
            }
        }
        
        close(client_socket);
    }
    
    close(server_socket);
    pthread_exit(NULL);
}

/**
 * @brief Main engine initialization
 */
int engine_init(char** playlist_files, int file_count) {
    // Startup decision: DeckLink enforcement outcome immediately shapes degraded
    // state and later transition safety behavior.
    if (decklink_enforce_quadlink_2si() != 0) {
        degraded_state_set(DEGRADED_DECKLINK_2SI, "DeckLink 2SI enforcement failed");
        fprintf(stderr, "[ENGINE][DECISION] DeckLink 2SI enforcement: failed, entering degraded mode\n");
    } else {
        degraded_state_clear(DEGRADED_DECKLINK_2SI, "DeckLink 2SI enforcement successful");
        fprintf(stderr, "[ENGINE][DECISION] DeckLink 2SI enforcement: success\n");
    }
    log_degraded_state("engine-init-start");

    // Initialize LibVLC
    if (vlc_init() < 0) {
        fprintf(stderr, "[ENGINE] Failed to initialize LibVLC\n");
        return -1;
    }
    
    // Initialize playlist (can be empty, with max 100 tracks)
    playlist_ctx = playlist_create(playlist_files, file_count, 100);
    if (!playlist_ctx) {
        fprintf(stderr, "[ENGINE] Failed to create playlist\n");
        return -1;
    }
    
    // Initialize hardware modules
    mqtt_ctx = mqtt_init(MQTT_BROKER_IP, MQTT_BROKER_PORT);
    if (!mqtt_ctx) {
        fprintf(stderr, "[ENGINE] Warning: MQTT initialization failed (non-fatal)\n");
    } else {
        if (mqtt_subscribe_content_add(mqtt_ctx, handle_mqtt_content_add, NULL) != 0) {
            fprintf(stderr, "[ENGINE] Warning: MQTT content/add subscription failed (non-fatal)\n");
        }
    }
    
    christie_ctx = christie_connect(CHRISTIE_IP, CHRISTIE_PORT);
    if (!christie_ctx) {
        fprintf(stderr, "[ENGINE] Warning: Christie connection failed (non-fatal)\n");
    }
    
    const char* alsa_device = get_rme_device();
    alsa_ctx = alsa_init(alsa_device, RME_CHANNELS, RME_SAMPLE_RATE);
    if (!alsa_ctx && strcmp(alsa_device, RME_DEVICE_LEGACY) != 0) {
        fprintf(stderr, "[ENGINE] ALSA routed device unavailable, falling back to %s\n", RME_DEVICE_LEGACY);
        alsa_ctx = alsa_init(RME_DEVICE_LEGACY, RME_CHANNELS, RME_SAMPLE_RATE);
    }
    if (!alsa_ctx) {
        fprintf(stderr, "[ENGINE] Warning: ALSA initialization failed (non-fatal)\n");
    }

    dxd16_ctx = dxd16_init_from_env();
    
    // Initialize CalDAV (optional, checks for env var CALDAV_URL)
    const char* caldav_url = getenv("CALDAV_URL");
    if (caldav_url) {
        const char* caldav_user = getenv("CALDAV_USER");
        const char* caldav_pass = getenv("CALDAV_PASS");
        const char* caldav_email = getenv("CALDAV_RESOURCE_EMAIL");
        caldav_ctx = caldav_init(caldav_url, caldav_user, caldav_pass, caldav_email);
        if (!caldav_ctx) {
            fprintf(stderr, "[ENGINE] Warning: CalDAV initialization failed (non-fatal)\n");
        }
    }
    
    // If playlist has files, create first player; otherwise enter idle state
    const char* first_file = playlist_current_file(playlist_ctx);
    if (first_file) {
        // Preload first title metadata to front-load color/aspect/AV-sync
        // decisions before playback begins.
        player_A = create_player(first_file);
        if (!player_A) {
            fprintf(stderr, "[ENGINE] Failed to create initial player\n");
            return -1;
        }
        
        // Load metadata for first file
        current_metadata = metadata_read_mp4(first_file);
        if (decklink_inject_hdr_ancillary_for_media(first_file, &current_metadata) != 0) {
            fprintf(stderr, "[ENGINE] Warning: Initial DeckLink HDR ancillary injection failed (non-fatal)\n");
        }
        if (alsa_ctx && current_metadata.av_sync_offset_samples != 0) {
            alsa_set_av_sync_offset(alsa_ctx, current_metadata.av_sync_offset_samples);
        }
    } else {
        fprintf(stderr, "[ENGINE] Playlist is empty, entering idle state...\n");
        enter_idle_state();
    }
    
    fprintf(stderr, "[ENGINE] Initialization complete\n");
    return 0;
}

/**
 * @brief Start playback
 */
int engine_play(void) {
    if (!player_A) {
        fprintf(stderr, "[ENGINE] No player available\n");
        return -1;
    }
    
    libvlc_media_player_play(player_A);
    fprintf(stderr, "[ENGINE] Playback started\n");
    return 0;
}

/**
 * @brief Cleanup and shutdown
 */
void engine_shutdown(void) {
    fprintf(stderr, "[ENGINE] Shutting down...\n");
    
    if (player_A) {
        libvlc_media_player_stop(player_A);
        libvlc_media_player_release(player_A);
    }
    if (player_B) {
        libvlc_media_player_stop(player_B);
        libvlc_media_player_release(player_B);
    }
    if (vlc_instance) {
        libvlc_release(vlc_instance);
    }
    
    if (mqtt_ctx) mqtt_close(mqtt_ctx);
    if (christie_ctx) christie_disconnect(christie_ctx);
    if (alsa_ctx) alsa_close(alsa_ctx);
    if (caldav_ctx) caldav_close(caldav_ctx);
    if (dxd16_ctx) dxd16_close(dxd16_ctx);
    if (playlist_ctx) playlist_destroy(playlist_ctx);
    
    fprintf(stderr, "[ENGINE] Shutdown complete\n");
}

/**
 * @brief Main entry point
 */
int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    fprintf(stderr, "\n╔════════════════════════════════════════════════════════╗\n");
    fprintf(stderr, "║  Projection Cinema Engine - SDI Media Server           ║\n");
    fprintf(stderr, "║  Frame-Accurate DCI 4K + 16-Ch AES Audio               ║\n");
    fprintf(stderr, "║  Ubuntu Server 22.04 LTS                              ║\n");
    fprintf(stderr, "╚════════════════════════════════════════════════════════╝\n\n");
    
    int file_count = (argc > 1) ? (argc - 1) : 0;
    
    if (engine_init((argc > 1) ? &argv[1] : NULL, file_count) < 0) {
        fprintf(stderr, "[ENGINE] Initialization failed\n");
        return EXIT_FAILURE;
    }
    
    // Start TCP control thread
    pthread_t tcp_thread;
    if (pthread_create(&tcp_thread, NULL, tcp_control_thread, NULL) != 0) {
        fprintf(stderr, "[ENGINE] Error creating TCP control thread\n");
        engine_shutdown();
        return EXIT_FAILURE;
    }
    
    // Start playback only if playlist has files
    if (file_count > 0) {
        if (engine_play() < 0) {
            fprintf(stderr, "[ENGINE] Playback start failed\n");
            engine_shutdown();
            return EXIT_FAILURE;
        }
        fprintf(stderr, "[ENGINE] Running with initial playlist (press Ctrl+C to exit)...\n");
    } else {
        fprintf(stderr, "[ENGINE] Running in daemon mode, awaiting network playlist (press Ctrl+C to exit)...\n");
        fprintf(stderr, "[ENGINE] Available TCP commands on port 8080:\n");
        fprintf(stderr, "  ADD_FILE <path>       - Add file to playlist\n");
        fprintf(stderr, "  LIST_PLAYLIST         - Show all files in queue\n");
        fprintf(stderr, "  PLAY_TRACK <index>    - Play file at index\n");
        fprintf(stderr, "  REMOVE_FILE <index>   - Remove file from playlist\n");
        fprintf(stderr, "  CLEAR_PLAYLIST        - Clear entire playlist\n");
        
        if (caldav_ctx) {
            fprintf(stderr, "[ENGINE] CalDAV integration enabled - auto-queueing from calendar\n");
            fprintf(stderr, "[ENGINE] Set CALDAV_URL env var to disable CalDAV (or restart without it)\n");
        } else {
            fprintf(stderr, "[ENGINE] CalDAV disabled. To enable:\n");
            fprintf(stderr, "  export CALDAV_URL=\"https://calendar.example.com/user/shows.ics\"\n");
            fprintf(stderr, "  export CALDAV_USER=\"username\"\n");
            fprintf(stderr, "  export CALDAV_PASS=\"password\"\n");
            fprintf(stderr, "  ./sdi_cinema_engine\n");
        }
    }
    
    // Main event loop
    while (!should_exit) {
        // Check for macro cues during playback
        pthread_mutex_lock(&player_mutex);
        libvlc_media_player_t* active = current_player_is_A ? player_A : player_B;
        if (active) {
            check_and_trigger_macrocues(active, &current_metadata);
        }
        pthread_mutex_unlock(&player_mutex);
        
        // Check idle timeout (power off if no content for 5+ minutes)
        check_idle_timeout();
        
        // Check CalDAV for upcoming shows (polls every 60 seconds)
        check_caldav_queue();
        
        usleep(100000);  // 100ms check interval
    }
    
    // Cleanup
    pthread_cancel(tcp_thread);
    pthread_join(tcp_thread, NULL);
    engine_shutdown();
    
    return EXIT_SUCCESS;
}
