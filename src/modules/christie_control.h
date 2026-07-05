/* SPDX-License-Identifier: MIT */
#ifndef CHRISTIE_CONTROL_H
#define CHRISTIE_CONTROL_H

/**
 * @file christie_control.h
 * @brief Christie Sapphire 4K projector control via TCP port 3002
 * @note Command format: (CMD VALUE)\r\n with integer padding
 */

typedef struct {
    char* host;
    int port;
    int socket_fd;
    int connected;
} ChristieContext;

/**
 * @brief Connect to Christie projector control port
 * @param host IP address (e.g., "192.168.1.75")
 * @param port TCP port (typically 3002)
 * @return ChristieContext, or NULL on connection error
 */
ChristieContext* christie_connect(const char* host, int port);

/**
 * @brief Send shutter command (SHU)
 * @param ctx Christie context
 * @param state 0 = shutter open, 1 = shutter closed
 * @return 0 on success, -1 on error
 */
int christie_shutter(ChristieContext* ctx, int state);

/**
 * @brief Set laser power level (LPLV)
 * @param ctx Christie context
 * @param level Power level 0-1000 (0% to 100%)
 * @return 0 on success, -1 on error
 */
int christie_laser_power(ChristieContext* ctx, int level);

/**
 * @brief Query Christie status (e.g., power, temperature)
 * @param ctx Christie context
 * @param command Command to query (e.g., "PWR" for power status)
 * @param response Buffer to store response (must be at least 128 bytes)
 * @return 0 on success, -1 on error
 *
 * Note:
 * - This API performs synchronous request/response I/O and should not be used
 *   on latency-critical render threads.
 */
int christie_query(ChristieContext* ctx, const char* command, char* response);

/**
 * @brief Check projector power status
 * @param ctx Christie context
 * @return 1 if powered on, 0 if powered off, -1 on error
 */
int christie_is_powered_on(ChristieContext* ctx);

/**
 * @brief Check projector ready status (lamp stable, temp OK)
 * @param ctx Christie context
 * @return 1 if ready for playback, 0 if not ready, -1 on error
 */
int christie_is_ready(ChristieContext* ctx);

/**
 * @brief Get projector temperature in Celsius
 * @param ctx Christie context
 * @param temp_out Output temperature value
 * @return 0 on success, -1 on error
 */
int christie_get_temperature(ChristieContext* ctx, float* temp_out);

/**
 * @brief Power on the projector
 * @param ctx Christie context
 * @return 0 on success, -1 on error
 */
int christie_power_on(ChristieContext* ctx);

/**
 * @brief Power off the projector
 * @param ctx Christie context
 * @return 0 on success, -1 on error
 */
int christie_power_off(ChristieContext* ctx);

/**
 * @brief Disconnect and free Christie context
 */
void christie_disconnect(ChristieContext* ctx);

#endif
