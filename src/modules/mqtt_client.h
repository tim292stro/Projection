/* SPDX-License-Identifier: MIT */
#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

/**
 * @file mqtt_client.h
 * @brief MQTT automation control for Dolby CP950A and theater hardware
 * @note Uses Paho MQTT C client library (mosquitto development library)
 */

typedef struct {
    char* broker_address;
    int broker_port;
    char* client_id;
    int connected;
    int using_paho;
    void* client_handle;

    // Stub callback path for content/add ingestion.
    void (*content_add_callback)(const char* payload, void* user_data);
    void* content_add_user_data;
} MQTTContext;

typedef void (*MQTTContentAddCallback)(const char* payload, void* user_data);

/**
 * @brief Initialize MQTT client and connect to broker
 * @param broker_address IP or hostname (e.g., "192.168.1.50")
 * @param broker_port MQTT port (typically 1883)
 * @return Initialized MQTTContext, or NULL on error
 *
 * Decision semantics:
 * - Uses Paho MQTT C transport when available at build time.
 * - Falls back to local non-broker mode when Paho transport is not compiled.
 */
MQTTContext* mqtt_init(const char* broker_address, int broker_port);

/**
 * @brief Mute Dolby CP950A output via MQTT
 * @param ctx MQTT context
 * @return 0 on success, -1 on error
 */
int mqtt_mute_cp950a(MQTTContext* ctx);

/**
 * @brief Unmute Dolby CP950A output via MQTT
 * @param ctx MQTT context
 * @return 0 on success, -1 on error
 */
int mqtt_unmute_cp950a(MQTTContext* ctx);

/**
 * @brief Publish generic theater hardware command
 * @param ctx MQTT context
 * @param device Device name (e.g., "lens_sled", "masking_curtains")
 * @param payload JSON payload
 * @return 0 on success, -1 on error
 *
 * Control implication:
 * - In Paho mode, success indicates broker publish acceptance.
 * - In fallback mode, success indicates local acceptance/logging only.
 */
int mqtt_publish_command(MQTTContext* ctx, const char* device, const char* payload);

/**
 * @brief Update marquee display via MQTT
 * @param ctx MQTT context
 * @param title Show title/filename
 * @param format Format string (e.g., "scope_2.39", "flat_1.85")
 * @param duration_sec Duration in seconds
 * @param start_time ISO8601 start time (e.g., "2026-07-04T20:00:00Z") or NULL
 * @return 0 on success, -1 on error
 * 
 * Publishes to: theater/marquee with JSON
 * Example: {"title":"Dune Part Two", "format":"scope_2.39", "duration_sec":10800, "start_time":"2026-07-04T20:00:00Z"}
 */
int mqtt_update_marquee(MQTTContext* ctx, const char* title, const char* format, 
                       int duration_sec, const char* start_time);

/**
 * @brief Publish marquee update for live-stage events
 * @param ctx MQTT context
 * @param title Event title
 * @param cover_art_path Path to cover art image (e.g., "/mnt/artwork/concert.jpg")
 * @param event_text Event description text
 * @param start_time ISO8601 start time
 * @return 0 on success, -1 on error
 * 
 * Publishes to: theater/marquee with JSON (live-stage variant)
 * Example: {"title":"Live Concert","type":"live-stage","cover_art":"/mnt/artwork/concert.jpg","description":"Live performance...","start_time":"2026-07-04T20:00:00Z"}
 */
int mqtt_update_marquee_stage(MQTTContext* ctx, const char* title, const char* cover_art_path,
                             const char* event_text, const char* start_time);

/**
 * @brief Publish house lights command
 * @param ctx MQTT context
 * @param level 0-100 (0=off, 100=full brightness)
 * @return 0 on success, -1 on error
 */
int mqtt_house_lights(MQTTContext* ctx, int level);

/**
 * @brief Publish curtain command (open/close)
 * @param ctx MQTT context
 * @param state 0 = open, 1 = closed
 * @return 0 on success, -1 on error
 */
int mqtt_curtains(MQTTContext* ctx, int state);

/**
 * @brief Subscribe MQTT ingest topic for add requests.
 * @param ctx MQTT context
 * @param callback Called for each content/add payload
 * @param user_data Opaque caller data passed to callback
 * @return 0 on success, -1 on error
 *
 * Decision semantics:
 * - Current implementation stores callback for in-process stub delivery.
 * - Production implementation should bind this callback to real broker
 *   subscription events for topic `content/add`.
 */
int mqtt_subscribe_content_add(MQTTContext* ctx, MQTTContentAddCallback callback, void* user_data);

/**
 * @brief Publish ingest response payload.
 * @param ctx MQTT context
 * @param payload JSON payload for response topic
 * @return 0 on success, -1 on error
 */
int mqtt_publish_content_add_response(MQTTContext* ctx, const char* payload);

/**
 * @brief Stub helper: inject an inbound content/add payload.
 * @note Used by local test paths until full broker subscription is integrated.
 * @param ctx MQTT context
 * @param payload JSON payload for content/add
 * @return 0 on success, -1 on error
 */
int mqtt_stub_receive_content_add(MQTTContext* ctx, const char* payload);

/**
 * @brief Disconnect and free MQTT context
 */
void mqtt_close(MQTTContext* ctx);

#endif
