/* SPDX-License-Identifier: MIT */
#include "dxd16_control.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "metadata_extractor.h"

static int approx(double a, double b, double tolerance) {
    double diff = a - b;
    if (diff < 0.0) diff = -diff;
    return diff <= tolerance;
}

static DXD16Preset select_preset_for_fps(double fps) {
    if (fps <= 0.0) return DXD16_PRESET_UNKNOWN;

    // Mapping policy groups fractional/integer/PAL frame-rate families.
    // Decision impact: ensures house clock profile follows content cadence,
    // reducing cadence conversion artifacts and sync drift at output.
    if (approx(fps, 23.976, 0.03) ||
        approx(fps, 29.97, 0.03) ||
        approx(fps, 59.94, 0.05) ||
        approx(fps, 119.88, 0.10)) {
        return DXD16_PRESET_A_FRACTIONAL;
    }

    if (approx(fps, 24.0, 0.03) ||
        approx(fps, 30.0, 0.03) ||
        approx(fps, 60.0, 0.05) ||
        approx(fps, 120.0, 0.10)) {
        return DXD16_PRESET_B_INTEGER;
    }

    if (approx(fps, 25.0, 0.03) ||
        approx(fps, 50.0, 0.05) ||
        approx(fps, 100.0, 0.10)) {
        return DXD16_PRESET_C_PAL;
    }

    return DXD16_PRESET_UNKNOWN;
}

const char* dxd16_preset_name(DXD16Preset preset) {
    switch (preset) {
        case DXD16_PRESET_A_FRACTIONAL: return "Preset A (Fractional)";
        case DXD16_PRESET_B_INTEGER: return "Preset B (Integer)";
        case DXD16_PRESET_C_PAL: return "Preset C (PAL)";
        default: return "Unknown";
    }
}

static const char* preset_recall_url(const DXD16Context* ctx, DXD16Preset preset, char* out, size_t out_len) {
    if (!ctx || !out || out_len == 0) return NULL;

    // URL selection decision:
    // - explicit per-preset URLs override derived base_url pattern.
    // - fallback derived URL keeps configuration compact for default devices.
    if (preset == DXD16_PRESET_A_FRACTIONAL && ctx->preset_a_url[0]) return ctx->preset_a_url;
    if (preset == DXD16_PRESET_B_INTEGER && ctx->preset_b_url[0]) return ctx->preset_b_url;
    if (preset == DXD16_PRESET_C_PAL && ctx->preset_c_url[0]) return ctx->preset_c_url;

    if (!ctx->base_url[0]) return NULL;

    const char* preset_name = NULL;
    if (preset == DXD16_PRESET_A_FRACTIONAL) preset_name = ctx->preset_a_name;
    else if (preset == DXD16_PRESET_B_INTEGER) preset_name = ctx->preset_b_name;
    else if (preset == DXD16_PRESET_C_PAL) preset_name = ctx->preset_c_name;

    if (!preset_name || !preset_name[0]) return NULL;

    snprintf(out, out_len, "%s/presets/%s/recall", ctx->base_url, preset_name);
    return out;
}

static int recall_preset_http(DXD16Context* ctx, DXD16Preset preset) {
    char derived_url[512] = {0};
    const char* url = preset_recall_url(ctx, preset, derived_url, sizeof(derived_url));
    if (!url) {
        fprintf(stderr, "[DXD16] No recall URL configured for %s\n", dxd16_preset_name(preset));
        return -1;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "[DXD16] Failed to initialize CURL\n");
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);

    if (ctx->username[0] && ctx->password[0]) {
        char auth[300] = {0};
        snprintf(auth, sizeof(auth), "%s:%s", ctx->username, ctx->password);
        curl_easy_setopt(curl, CURLOPT_USERPWD, auth);
    }

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    }

    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "[DXD16] Recall failed for %s: %s\n",
                dxd16_preset_name(preset), curl_easy_strerror(res));
        return -1;
    }

    if (http_code < 200 || http_code >= 300) {
        fprintf(stderr, "[DXD16] Recall failed for %s: HTTP %ld\n",
                dxd16_preset_name(preset), http_code);
        return -1;
    }

    fprintf(stderr, "[DXD16] Recalled %s via %s\n", dxd16_preset_name(preset), url);
    return 0;
}

DXD16Context* dxd16_init_from_env(void) {
    DXD16Context* ctx = (DXD16Context*)calloc(1, sizeof(DXD16Context));
    if (!ctx) return NULL;

    const char* base_url = getenv("DXD16_URL");
    const char* user = getenv("DXD16_USER");
    const char* pass = getenv("DXD16_PASS");
    const char* pa = getenv("DXD16_PRESET_A_NAME");
    const char* pb = getenv("DXD16_PRESET_B_NAME");
    const char* pc = getenv("DXD16_PRESET_C_NAME");
    const char* ua = getenv("DXD16_PRESET_A_URL");
    const char* ub = getenv("DXD16_PRESET_B_URL");
    const char* uc = getenv("DXD16_PRESET_C_URL");
    const char* lock_ms = getenv("DXD16_LOCK_WAIT_MS");

    if (base_url) strncpy(ctx->base_url, base_url, sizeof(ctx->base_url) - 1);
    if (user) strncpy(ctx->username, user, sizeof(ctx->username) - 1);
    if (pass) strncpy(ctx->password, pass, sizeof(ctx->password) - 1);

    strncpy(ctx->preset_a_name, (pa && pa[0]) ? pa : "A", sizeof(ctx->preset_a_name) - 1);
    strncpy(ctx->preset_b_name, (pb && pb[0]) ? pb : "B", sizeof(ctx->preset_b_name) - 1);
    strncpy(ctx->preset_c_name, (pc && pc[0]) ? pc : "C", sizeof(ctx->preset_c_name) - 1);

    if (ua) strncpy(ctx->preset_a_url, ua, sizeof(ctx->preset_a_url) - 1);
    if (ub) strncpy(ctx->preset_b_url, ub, sizeof(ctx->preset_b_url) - 1);
    if (uc) strncpy(ctx->preset_c_url, uc, sizeof(ctx->preset_c_url) - 1);

    ctx->lock_wait_ms = (lock_ms && lock_ms[0]) ? atoi(lock_ms) : 1500;
    if (ctx->lock_wait_ms < 0) ctx->lock_wait_ms = 0;

    ctx->enabled =
        (ctx->base_url[0] != '\0') ||
        (ctx->preset_a_url[0] != '\0' && ctx->preset_b_url[0] != '\0' && ctx->preset_c_url[0] != '\0');

    ctx->last_preset = DXD16_PRESET_UNKNOWN;

    if (!ctx->enabled) {
        fprintf(stderr, "[DXD16] Control disabled (set DXD16_URL or DXD16_PRESET_A_URL/B_URL/C_URL)\n");
    } else {
        fprintf(stderr, "[DXD16] Control enabled (lock wait %d ms)\n", ctx->lock_wait_ms);
    }

    return ctx;
}

int dxd16_apply_for_media_file(DXD16Context* ctx, const char* file_path,
                               double* detected_fps_out, DXD16Preset* selected_out) {
    if (detected_fps_out) *detected_fps_out = 0.0;
    if (selected_out) *selected_out = DXD16_PRESET_UNKNOWN;

    // Guardrails: no-op as failure when control is unavailable so callers can
    // explicitly enter degraded-state behavior upstream.
    if (!ctx || !ctx->enabled || !file_path || !file_path[0]) {
        return -1;
    }

    double fps = metadata_get_frame_rate(file_path);
    if (detected_fps_out) *detected_fps_out = fps;
    if (fps <= 0.0) {
        fprintf(stderr, "[DXD16] Unable to detect frame-rate for %s\n", file_path);
        return -1;
    }

    DXD16Preset preset = select_preset_for_fps(fps);
    if (selected_out) *selected_out = preset;

    if (preset == DXD16_PRESET_UNKNOWN) {
        fprintf(stderr, "[DXD16] No preset mapping for %.3f fps\n", fps);
        return -1;
    }

    // Debounce repeated preset recalls.
    // Implication: avoids unnecessary HTTP calls and relay churn when adjacent
    // playlist items share the same timing family.
    if (ctx->last_preset == preset) {
        fprintf(stderr, "[DXD16] Preset already active for %.3f fps: %s\n", fps, dxd16_preset_name(preset));
        return 0;
    }

    if (recall_preset_http(ctx, preset) != 0) {
        return -1;
    }

    ctx->last_preset = preset;

    // Optional lock wait gives external clock domain time to settle before
    // playback starts, trading startup latency for deterministic sync.
    if (ctx->lock_wait_ms > 0) {
        usleep((useconds_t)ctx->lock_wait_ms * 1000);
        fprintf(stderr, "[DXD16] Lock wait complete (%d ms)\n", ctx->lock_wait_ms);
    }

    return 0;
}

void dxd16_close(DXD16Context* ctx) {
    if (!ctx) return;
    free(ctx);
}
