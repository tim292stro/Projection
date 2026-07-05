/* SPDX-License-Identifier: MIT */
#ifndef METADATA_EXTRACTOR_H
#define METADATA_EXTRACTOR_H

#include <libavformat/avformat.h>
#include <stdint.h>

/**
 * @file metadata_extractor.h
 * @brief Extract cinema metadata from MP4 files using libavformat
 * @note Handles custom Projection metadata including AV sync offset and macro cues
 */

#define MAX_CUES 32

typedef struct {
    uint32_t timecode_ms;         // Cue trigger time in milliseconds
    char cue_type[32];            // "[FEATURE]", "[CREDITS]", "[INTRO]", "[RECAP]"
    char description[256];        // Optional JSON payload
} MacroCue;

typedef struct {
    int32_t av_sync_offset_samples;  // Converted from milliseconds @ 48 kHz
                                      // Positive = audio is late (needs advancement)
                                      // Negative = audio is early (needs delay)
    float aspect_ratio;               // Video aspect ratio (e.g., 2.39 for scope, 1.85 for flat)
    int hdr_signaled;                 // 1 when transfer characteristics signal HDR (PQ/HLG)
    char color_primaries[64];         // Human-readable color primaries
    char color_transfer[64];          // Human-readable transfer characteristic
    char color_space[64];             // Human-readable YCbCr matrix/colorspace
    char title[256];
    char comment[512];
    char description[512];
    char synopsis[2048];
    int has_attached_pic;
    
    // Macro cues (theater automation triggers)
    MacroCue cues[MAX_CUES];
    int cue_count;
    int last_triggered_cue_index;     // Track which cue was last triggered (-1 = none)
} ProjectionMediaMetadata;

/**
 * @brief Open MP4 file and extract all Projection metadata
 * @param file_path Path to MP4 file
 * @return Populated ProjectionMediaMetadata structure; av_sync_offset_samples=0 if not found
 *
 * Decision semantics:
 * - Provides safe defaults when optional metadata is missing.
 * - Intended to keep show control deterministic rather than fail hard on
 *   non-critical metadata gaps.
 */
ProjectionMediaMetadata metadata_read_mp4(const char* file_path);

/**
 * @brief Get attached poster image from MP4 file
 * @param file_path Path to MP4 file
 * @param out_data Pointer to allocate and fill with image data
 * @param out_size Pointer to store image size in bytes
 * @return 0 on success, -1 if no attached image
 */
int metadata_extract_poster(const char* file_path, uint8_t** out_data, size_t* out_size);

/**
 * @brief Get media duration from file
 * @param file_path Path to MP4 file
 * @return Duration in milliseconds, or 0 if unable to determine
 *
 * Control implication:
 * - Return value 0 means duration unknown and should be treated as
 *   unschedulable or fallback-only by callers that require exact timing.
 */
int64_t metadata_get_duration_ms(const char* file_path);

/**
 * @brief Get media frame rate from first video stream
 * @param file_path Path to media file
 * @return Frames per second, or 0.0 if unavailable
 */
double metadata_get_frame_rate(const char* file_path);

/**
 * @brief Free allocated poster image data
 */
void metadata_free_poster(uint8_t* data);

#endif
