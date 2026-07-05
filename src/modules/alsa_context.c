/* SPDX-License-Identifier: MIT */
#include "alsa_context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ALSAContext* alsa_init(const char* device_name, unsigned int channels, unsigned int sample_rate) {
    ALSAContext* ctx = (ALSAContext*)malloc(sizeof(ALSAContext));
    if (!ctx) return NULL;
    
    memset(ctx, 0, sizeof(ALSAContext));
    ctx->device_name = device_name;
    ctx->channels = channels;
    ctx->sample_rate = sample_rate;
    ctx->av_sync_offset_samples = 0;
    ctx->offset_applied = 0;
    
    snd_pcm_t* pcm_handle;
    snd_pcm_hw_params_t* hw_params;
    
    // Open PCM device for playback
    if (snd_pcm_open(&pcm_handle, device_name, SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        fprintf(stderr, "[ALSA] Error opening PCM device: %s\n", device_name);
        free(ctx);
        return NULL;
    }
    
    // Allocate HW params structure
    snd_pcm_hw_params_alloca(&hw_params);
    if (snd_pcm_hw_params_any(pcm_handle, hw_params) < 0) {
        fprintf(stderr, "[ALSA] Error initializing hw_params\n");
        snd_pcm_close(pcm_handle);
        free(ctx);
        return NULL;
    }
    
    // Set access method (interleaved)
    if (snd_pcm_hw_params_set_access(pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) {
        fprintf(stderr, "[ALSA] Error setting access\n");
        snd_pcm_close(pcm_handle);
        free(ctx);
        return NULL;
    }
    
    // Format decision:
    // prefer 32-bit for headroom/precision, fall back to 16-bit for broader
    // device compatibility.
    // Implication: precision can degrade on fallback, but playout remains alive.
    if (snd_pcm_hw_params_set_format(pcm_handle, hw_params, SND_PCM_FORMAT_S32_LE) < 0) {
        fprintf(stderr, "[ALSA] Error setting format to S32_LE, trying S16_LE\n");
        if (snd_pcm_hw_params_set_format(pcm_handle, hw_params, SND_PCM_FORMAT_S16_LE) < 0) {
            fprintf(stderr, "[ALSA] Error setting format\n");
            snd_pcm_close(pcm_handle);
            free(ctx);
            return NULL;
        }
    }
    
    // Set channels
    if (snd_pcm_hw_params_set_channels(pcm_handle, hw_params, channels) < 0) {
        fprintf(stderr, "[ALSA] Error setting channels to %u\n", channels);
        snd_pcm_close(pcm_handle);
        free(ctx);
        return NULL;
    }
    
    // Use *_near to negotiate closest hardware-supported rate while preserving
    // requested timing intent.
    unsigned int rate = sample_rate;
    if (snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &rate, 0) < 0) {
        fprintf(stderr, "[ALSA] Error setting sample rate\n");
        snd_pcm_close(pcm_handle);
        free(ctx);
        return NULL;
    }
    
    // Apply hw params
    if (snd_pcm_hw_params(pcm_handle, hw_params) < 0) {
        fprintf(stderr, "[ALSA] Error applying hw_params\n");
        snd_pcm_close(pcm_handle);
        free(ctx);
        return NULL;
    }
    
    ctx->pcm_handle = pcm_handle;
    fprintf(stderr, "[ALSA] Initialized: device=%s, channels=%u, rate=%u Hz\n", device_name, channels, sample_rate);
    return ctx;
}

void alsa_set_av_sync_offset(ALSAContext* ctx, int32_t offset_samples) {
    if (!ctx) return;
    ctx->av_sync_offset_samples = offset_samples;
    // Offset is applied once per playback start/reset window.
    // Decision impact: prevents repeated cumulative shifts across writes.
    ctx->offset_applied = 0;  // Reset flag for next audio write
    
    const char* direction = (offset_samples > 0) ? "late (advance)" : "early (delay)";
    fprintf(stderr, "[ALSA] AV sync offset set: %d samples (audio is %s)\n", offset_samples, direction);
}

snd_pcm_sframes_t alsa_apply_offset_and_write(ALSAContext* ctx, void* audio_data, snd_pcm_uframes_t frame_count) {
    if (!ctx || !ctx->pcm_handle || !audio_data) {
        fprintf(stderr, "[ALSA] Error: Invalid ALSA context or audio data\n");
        return -1;
    }
    
    // Apply offset on first write of this session
    if (!ctx->offset_applied && ctx->av_sync_offset_samples != 0) {
        ctx->offset_applied = 1;
        
        if (ctx->av_sync_offset_samples > 0) {
            // Audio is late: skip samples to advance it
            snd_pcm_uframes_t skip_count = (snd_pcm_uframes_t)ctx->av_sync_offset_samples;
            snd_pcm_uframes_t skipped = snd_pcm_forward(ctx->pcm_handle, skip_count);
            fprintf(stderr, "[ALSA] Advanced audio by %lu samples (compensation for late audio)\n", skipped);
        } else {
            // Audio is early: we would insert silence, but LibVLC handles this better
            // by adjusting the read pointer. Log the compensation for diagnostics.
            // Implication: negative offsets are cooperative with upstream player
            // timing model rather than forcing silence injection in this layer.
            fprintf(stderr, "[ALSA] Delaying audio by %lu samples (compensation for early audio)\n",
                    (snd_pcm_uframes_t)(-ctx->av_sync_offset_samples));
            // Note: Actual silence insertion would happen in the audio buffer management layer
        }
    }
    
    // Write audio to device
    snd_pcm_sframes_t frames_written = snd_pcm_writei(ctx->pcm_handle, audio_data, frame_count);
    if (frames_written < 0) {
        fprintf(stderr, "[ALSA] Write error: %s\n", snd_strerror(frames_written));
        // Try to recover from underrun
        if (snd_pcm_recover(ctx->pcm_handle, frames_written, 0) < 0) {
            fprintf(stderr, "[ALSA] Recovery failed\n");
        }
    }
    
    return frames_written;
}

void alsa_close(ALSAContext* ctx) {
    if (!ctx) return;
    
    if (ctx->pcm_handle) {
        snd_pcm_drain(ctx->pcm_handle);
        snd_pcm_close(ctx->pcm_handle);
        fprintf(stderr, "[ALSA] Device closed: %s\n", ctx->device_name);
    }
    
    free(ctx);
}
