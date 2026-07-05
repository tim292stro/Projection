/* SPDX-License-Identifier: MIT */
#ifndef DXD16_CONTROL_H
#define DXD16_CONTROL_H

#include <stddef.h>

typedef enum {
    DXD16_PRESET_UNKNOWN = 0,
    DXD16_PRESET_A_FRACTIONAL,
    DXD16_PRESET_B_INTEGER,
    DXD16_PRESET_C_PAL
} DXD16Preset;

typedef struct {
    int enabled;
    char base_url[256];
    char username[128];
    char password[128];
    char preset_a_name[64];
    char preset_b_name[64];
    char preset_c_name[64];
    char preset_a_url[256];
    char preset_b_url[256];
    char preset_c_url[256];
    int lock_wait_ms;
    DXD16Preset last_preset;
} DXD16Context;

/**
 * @brief Initialize DXD-16 control context from environment variables.
 *
 * Required for active control:
 * - DXD16_URL or all of DXD16_PRESET_A_URL/B_URL/C_URL
 */
DXD16Context* dxd16_init_from_env(void);

/**
 * @brief Apply matching DXD-16 preset for a media file's frame-rate.
 * @param ctx DXD context
 * @param file_path Media file path
 * @param detected_fps_out Optional output for detected frame-rate
 * @param selected_out Optional output for selected preset
 * @return 0 on success, -1 on error/unavailable
 *
 * Decision semantics:
 * - Detects media frame-rate, selects preset family, optionally applies lock
 *   settle wait, and records last preset to suppress redundant recalls.
 */
int dxd16_apply_for_media_file(DXD16Context* ctx, const char* file_path,
                               double* detected_fps_out, DXD16Preset* selected_out);

/**
 * @brief Get string name for preset enum.
 */
const char* dxd16_preset_name(DXD16Preset preset);

/**
 * @brief Free DXD context.
 */
void dxd16_close(DXD16Context* ctx);

#endif
