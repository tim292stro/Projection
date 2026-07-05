/* SPDX-License-Identifier: MIT */
#ifndef ALSA_CONTEXT_H
#define ALSA_CONTEXT_H

#include <stdint.h>
#include <alsa/asoundlib.h>

/**
 * @file alsa_context.h
 * @brief ALSA device handling with AV sync offset compensation
 */

typedef struct {
    snd_pcm_t* pcm_handle;
    const char* device_name;
    int32_t av_sync_offset_samples;  // Current offset to apply
    int offset_applied;              // Flag: offset has been applied
    unsigned int sample_rate;
    unsigned int channels;
} ALSAContext;

/**
 * @brief Initialize ALSA device
 * @param device_name ALSA device string (e.g., "hw:0,0")
 * @param channels Number of channels (16 for RME)
 * @param sample_rate Sample rate (48000 Hz)
 * @return Initialized ALSAContext, or NULL on error
 */
ALSAContext* alsa_init(const char* device_name, unsigned int channels, unsigned int sample_rate);

/**
 * @brief Set AV sync offset for next playback
 * @param ctx ALSA context
 * @param offset_samples Signed sample offset (positive = audio late, negative = audio early)
 *
 * Decision semantics:
 * - Offset is consumed on next write cycle and then marked applied.
 * - Re-apply requires calling this API again (usually on transition/start).
 */
void alsa_set_av_sync_offset(ALSAContext* ctx, int32_t offset_samples);

/**
 * @brief Apply AV sync offset compensation to audio stream
 * @param ctx ALSA context
 * @param audio_data Pointer to audio samples
 * @param frame_count Number of audio frames
 * @return Number of frames written to device
 */
snd_pcm_sframes_t alsa_apply_offset_and_write(ALSAContext* ctx, void* audio_data, snd_pcm_uframes_t frame_count);

/**
 * @brief Close ALSA device and free context
 */
void alsa_close(ALSAContext* ctx);

#endif
