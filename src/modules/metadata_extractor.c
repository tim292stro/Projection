/* SPDX-License-Identifier: MIT */
#include "metadata_extractor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* transfer_name(enum AVColorTransferCharacteristic trc) {
    switch (trc) {
        case AVCOL_TRC_SMPTE2084: return "smpte2084";
        case AVCOL_TRC_ARIB_STD_B67: return "arib-std-b67";
        case AVCOL_TRC_BT709: return "bt709";
        case AVCOL_TRC_GAMMA22: return "gamma22";
        case AVCOL_TRC_GAMMA28: return "gamma28";
        default: return "unspecified";
    }
}

static const char* primaries_name(enum AVColorPrimaries primaries) {
    switch (primaries) {
        case AVCOL_PRI_BT2020: return "bt2020";
        case AVCOL_PRI_BT709: return "bt709";
        case AVCOL_PRI_SMPTE432: return "smpte432";
        default: return "unspecified";
    }
}

static const char* colorspace_name(enum AVColorSpace colorspace) {
    switch (colorspace) {
        case AVCOL_SPC_BT2020_NCL: return "bt2020nc";
        case AVCOL_SPC_BT709: return "bt709";
        case AVCOL_SPC_SMPTE170M: return "smpte170m";
        default: return "unspecified";
    }
}

ProjectionMediaMetadata metadata_read_mp4(const char* file_path) {
    ProjectionMediaMetadata meta = {0};
    // Baseline defaults keep downstream routing deterministic even when source
    // metadata is sparse or malformed.
    // Implication: automation can still pick a safe presentation profile.
    meta.aspect_ratio = 2.39f;  // Default to scope aspect ratio
    strncpy(meta.color_primaries, "unspecified", sizeof(meta.color_primaries) - 1);
    strncpy(meta.color_transfer, "unspecified", sizeof(meta.color_transfer) - 1);
    strncpy(meta.color_space, "unspecified", sizeof(meta.color_space) - 1);
    AVFormatContext* fmt_ctx = NULL;
    
    if (!file_path) {
        fprintf(stderr, "[METADATA] Error: NULL file path\n");
        return meta;
    }
    
    // Open the MP4 file
    if (avformat_open_input(&fmt_ctx, file_path, NULL, NULL) < 0) {
        fprintf(stderr, "[METADATA] Error opening file: %s\n", file_path);
        return meta;
    }
    
    // Read stream info
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "[METADATA] Error reading stream info: %s\n", file_path);
        avformat_close_input(&fmt_ctx);
        return meta;
    }
    
    // Extract video aspect ratio from first video stream
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            AVStream* video_stream = fmt_ctx->streams[i];
            AVCodecParameters* codecpar = video_stream->codecpar;
            
            // Aspect ratio decision chain:
            // 1) prefer SAR-adjusted geometry when signaled,
            // 2) otherwise fall back to raw frame dimensions.
            // Implication: protects projection mode decisions from missing SAR.
            if (video_stream->codecpar->sample_aspect_ratio.den != 0) {
                float sar = (float)video_stream->codecpar->sample_aspect_ratio.num / 
                           (float)video_stream->codecpar->sample_aspect_ratio.den;
                float width = (float)video_stream->codecpar->width;
                float height = (float)video_stream->codecpar->height;
                meta.aspect_ratio = (width / height) * sar;
            } else if (video_stream->r_frame_rate.num && video_stream->r_frame_rate.den) {
                // Fallback: calculate from pixel dimensions
                float width = (float)video_stream->codecpar->width;
                float height = (float)video_stream->codecpar->height;
                meta.aspect_ratio = width / height;
            }
            
            fprintf(stderr, "[METADATA] Detected aspect ratio: %.2f:1\n", meta.aspect_ratio);

            strncpy(meta.color_transfer, transfer_name(codecpar->color_trc), sizeof(meta.color_transfer) - 1);
            strncpy(meta.color_primaries, primaries_name(codecpar->color_primaries), sizeof(meta.color_primaries) - 1);
            strncpy(meta.color_space, colorspace_name(codecpar->color_space), sizeof(meta.color_space) - 1);

            // HDR flag is derived from transfer characteristic only.
            // Implication: this is a signaling-based heuristic, not full HDR
            // mastering metadata validation.
            meta.hdr_signaled = (codecpar->color_trc == AVCOL_TRC_SMPTE2084 ||
                                 codecpar->color_trc == AVCOL_TRC_ARIB_STD_B67) ? 1 : 0;

            fprintf(stderr, "[METADATA] Color signaling: primaries=%s transfer=%s matrix=%s hdr=%d\n",
                    meta.color_primaries,
                    meta.color_transfer,
                    meta.color_space,
                    meta.hdr_signaled);
            break;  // Use first video stream only
        }
    }
    
    // Extract Projection AV sync offset (milliseconds)
    AVDictionaryEntry* av_sync_entry = av_dict_get(
        fmt_ctx->metadata,
        "projection:av_sync_offset_ms",
        NULL,
        AV_DICT_IGNORE_SUFFIX
    );
    
    if (av_sync_entry) {
        int32_t offset_ms = (int32_t)atoi(av_sync_entry->value);
        
        // Safety clamp keeps sync correction in the control range expected by
        // theater playback path and prevents extreme offsets from causing
        // unintuitive operator behavior.
        if (offset_ms > 3000) {
            fprintf(stderr, "[METADATA] Warning: AV sync offset clamped from %d to 3000 ms\n", offset_ms);
            offset_ms = 3000;
        }
        if (offset_ms < -3000) {
            fprintf(stderr, "[METADATA] Warning: AV sync offset clamped from %d to -3000 ms\n", offset_ms);
            offset_ms = -3000;
        }
        
        // Convert ms -> samples at the fixed house clock rate (48 kHz).
        // Implication: audio correction aligns with sample-accurate pipeline
        // controls instead of coarse time-domain values.
        meta.av_sync_offset_samples = offset_ms * 48;
        
        const char* direction = (offset_ms > 0) ? "late (advance audio)" : "early (delay audio)";
        fprintf(stderr, "[METADATA] AV sync offset: %d ms = %d samples (audio is %s)\n",
                offset_ms, meta.av_sync_offset_samples, direction);
    }
    
    // Extract macro cues from MP4 chapter markers
    meta.cue_count = 0;
    meta.last_triggered_cue_index = -1;
    
    if (fmt_ctx->nb_chapters > 0) {
        fprintf(stderr, "[METADATA] Found %d chapter(s) in file\n", fmt_ctx->nb_chapters);
        
        // Cue extraction policy reads chapter title as cue identity and optional
        // description as payload. This keeps authoring simple in standard tools.
        for (unsigned int i = 0; i < fmt_ctx->nb_chapters && i < MAX_CUES; i++) {
            AVChapter* chapter = fmt_ctx->chapters[i];
            
            // Convert chapter start time to milliseconds
            int64_t start_ms = (int64_t)((double)chapter->start * 
                                         (double)chapter->time_base.num / 
                                         (double)chapter->time_base.den * 1000.0);
            
            // Extract chapter title (cue type)
            AVDictionaryEntry* title_entry = av_dict_get(chapter->metadata, "title", NULL, 0);
            if (title_entry && title_entry->value) {
                meta.cues[meta.cue_count].timecode_ms = (uint32_t)start_ms;
                strncpy(meta.cues[meta.cue_count].cue_type, title_entry->value, 
                       sizeof(meta.cues[meta.cue_count].cue_type) - 1);
                
                // Optional: Extract chapter description as JSON payload
                AVDictionaryEntry* desc_entry = av_dict_get(chapter->metadata, "description", NULL, 0);
                if (desc_entry && desc_entry->value) {
                    strncpy(meta.cues[meta.cue_count].description, desc_entry->value,
                           sizeof(meta.cues[meta.cue_count].description) - 1);
                }
                
                fprintf(stderr, "[METADATA] Cue %d: %s @ %lu ms\n",
                       meta.cue_count, meta.cues[meta.cue_count].cue_type, 
                       (unsigned long)meta.cues[meta.cue_count].timecode_ms);
                
                meta.cue_count++;
            }
        }
    }
    
    // Extract standard metadata fields
    AVDictionaryEntry* entry = NULL;
    
    entry = av_dict_get(fmt_ctx->metadata, "title", NULL, 0);
    if (entry) strncpy(meta.title, entry->value, sizeof(meta.title) - 1);
    
    entry = av_dict_get(fmt_ctx->metadata, "©cmt", NULL, 0);
    if (!entry) entry = av_dict_get(fmt_ctx->metadata, "comment", NULL, 0);
    if (entry) strncpy(meta.comment, entry->value, sizeof(meta.comment) - 1);
    
    entry = av_dict_get(fmt_ctx->metadata, "description", NULL, 0);
    if (entry) strncpy(meta.description, entry->value, sizeof(meta.description) - 1);
    
    entry = av_dict_get(fmt_ctx->metadata, "©ldes", NULL, 0);
    if (!entry) entry = av_dict_get(fmt_ctx->metadata, "synopsis", NULL, 0);
    if (entry) strncpy(meta.synopsis, entry->value, sizeof(meta.synopsis) - 1);
    
    // Check for attached picture
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC) {
            meta.has_attached_pic = 1;
            break;
        }
    }
    
    fprintf(stderr, "[METADATA] Loaded: title='%s', av_offset=%d samples, has_poster=%d\n",
            meta.title, meta.av_sync_offset_samples, meta.has_attached_pic);
    
    avformat_close_input(&fmt_ctx);
    return meta;
}

int metadata_extract_poster(const char* file_path, uint8_t** out_data, size_t* out_size) {
    AVFormatContext* fmt_ctx = NULL;
    
    if (!file_path || !out_data || !out_size) {
        fprintf(stderr, "[METADATA] Error: Invalid parameters to metadata_extract_poster\n");
        return -1;
    }
    
    if (avformat_open_input(&fmt_ctx, file_path, NULL, NULL) < 0) {
        fprintf(stderr, "[METADATA] Error opening file for poster extraction: %s\n", file_path);
        return -1;
    }
    
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        avformat_close_input(&fmt_ctx);
        return -1;
    }
    
    // Find attached picture stream
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC) {
            AVPacket pkt = fmt_ctx->streams[i]->attached_pic;
            if (pkt.size > 0) {
                *out_data = (uint8_t*)malloc(pkt.size);
                if (*out_data) {
                    memcpy(*out_data, pkt.data, pkt.size);
                    *out_size = pkt.size;
                    fprintf(stderr, "[METADATA] Extracted poster image: %zu bytes\n", pkt.size);
                    avformat_close_input(&fmt_ctx);
                    return 0;
                }
            }
        }
    }
    
    avformat_close_input(&fmt_ctx);
    fprintf(stderr, "[METADATA] No poster image found in: %s\n", file_path);
    return -1;
}

void metadata_free_poster(uint8_t* data) {
    if (data) free(data);
}

int64_t metadata_get_duration_ms(const char* file_path) {
    if (!file_path) {
        fprintf(stderr, "[METADATA] Error: NULL file path for duration query\n");
        return 0;
    }
    
    AVFormatContext* fmt_ctx = NULL;
    
    // Open the file
    if (avformat_open_input(&fmt_ctx, file_path, NULL, NULL) < 0) {
        fprintf(stderr, "[METADATA] Error opening file for duration: %s\n", file_path);
        return 0;
    }
    
    // Read stream info
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "[METADATA] Error reading stream info for duration: %s\n", file_path);
        avformat_close_input(&fmt_ctx);
        return 0;
    }
    
    // Duration resolution order:
    // 1) container-level duration (fast and usually authoritative),
    // 2) first stream duration fallback when container value is absent.
    // Implication: scheduler can still operate on imperfect files.
    int64_t duration_ms = 0;
    if (fmt_ctx->duration > 0) {
        // av_format_ctx->duration is in AV_TIME_BASE units (microseconds)
        // Convert to milliseconds: duration_us / 1000
        duration_ms = fmt_ctx->duration / 1000;
        fprintf(stderr, "[METADATA] Duration: %lld ms (%.1f seconds)\n", 
               (long long)duration_ms, (double)duration_ms / 1000.0);
    } else {
        // Fallback path for containers that do not expose aggregate duration.
        // Decision impact: first valid stream wins, which favors deterministic
        // behavior over expensive multi-stream reconciliation.
        for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
            AVStream* stream = fmt_ctx->streams[i];
            if (stream->duration > 0 && stream->time_base.den > 0) {
                // duration = stream->duration * time_base
                // In ms: duration * time_base * 1000
                duration_ms = (stream->duration * stream->time_base.num * 1000) / stream->time_base.den;
                fprintf(stderr, "[METADATA] Duration from stream %u: %lld ms (%.1f seconds)\n", 
                       i, (long long)duration_ms, (double)duration_ms / 1000.0);
                break;
            }
        }
    }
    
    avformat_close_input(&fmt_ctx);
    return duration_ms;
}

double metadata_get_frame_rate(const char* file_path) {
    if (!file_path) {
        fprintf(stderr, "[METADATA] Error: NULL file path for frame-rate query\n");
        return 0.0;
    }

    AVFormatContext* fmt_ctx = NULL;
    if (avformat_open_input(&fmt_ctx, file_path, NULL, NULL) < 0) {
        fprintf(stderr, "[METADATA] Error opening file for frame-rate: %s\n", file_path);
        return 0.0;
    }

    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "[METADATA] Error reading stream info for frame-rate: %s\n", file_path);
        avformat_close_input(&fmt_ctx);
        return 0.0;
    }

    double fps = 0.0;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        AVStream* stream = fmt_ctx->streams[i];
        if (stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
            continue;
        }

        // Frame-rate decision: prefer averaged stream rate for presentation
        // stability, then fall back to nominal rate if needed.
        AVRational rate = stream->avg_frame_rate;
        if (rate.num == 0 || rate.den == 0) {
            rate = stream->r_frame_rate;
        }

        if (rate.num > 0 && rate.den > 0) {
            fps = av_q2d(rate);
            break;
        }
    }

    avformat_close_input(&fmt_ctx);

    if (fps > 0.0) {
        fprintf(stderr, "[METADATA] Frame-rate: %.3f fps\n", fps);
    } else {
        fprintf(stderr, "[METADATA] Frame-rate unavailable for: %s\n", file_path);
    }

    return fps;
}
