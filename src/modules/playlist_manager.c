/* SPDX-License-Identifier: MIT */
#include "playlist_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "metadata_extractor.h"

PlaylistContext* playlist_create(char** files, int count, int max_capacity) {
    if (count < 0) {
        fprintf(stderr, "[PLAYLIST] Error: Invalid count\n");
        return NULL;
    }
    
    if (count > 0 && !files) {
        fprintf(stderr, "[PLAYLIST] Error: Invalid files array\n");
        return NULL;
    }
    
    PlaylistContext* ctx = (PlaylistContext*)malloc(sizeof(PlaylistContext));
    if (!ctx) return NULL;
    
    // Capacity decision:
    // - bounded queues protect against unbounded memory growth from ingest.
    // - default cap keeps behavior deterministic when callers pass 0.
    int capacity = max_capacity > 0 ? max_capacity : 1000;
    ctx->playlist_entries = (PlaylistEntry*)malloc(sizeof(PlaylistEntry) * capacity);
    if (!ctx->playlist_entries) {
        free(ctx);
        return NULL;
    }
    
    ctx->max_capacity = capacity;
    ctx->total_tracks = 0;
    ctx->current_index = -1;  // -1 means no valid track
    ctx->prebuffer_enabled = 1;
    ctx->last_added_timestamp = 0;
    ctx->idle_timer_start = 0;
    
    // Bootstrap decision: preload entries when provided so startup can begin
    // with a deterministic initial playlist state.
    if (count > 0 && files) {
        for (int i = 0; i < count && i < capacity; i++) {
            memset(&ctx->playlist_entries[i], 0, sizeof(PlaylistEntry));
            strncpy(ctx->playlist_entries[i].file_path, files[i], 511);
            ctx->playlist_entries[i].duration_ms = 0;  // Will be calculated on-demand
            ctx->playlist_entries[i].start_delay_ms = 0;
            ctx->playlist_entries[i].scheduled_time = 0;
            strncpy(ctx->playlist_entries[i].title, files[i], 255);
        }
        ctx->total_tracks = count;
        ctx->current_index = 0;
        ctx->last_added_timestamp = time(NULL);
    } else {
        ctx->idle_timer_start = time(NULL);  // Start idle timer if empty
    }
    
    if (count == 0) {
        fprintf(stderr, "[PLAYLIST] Created empty playlist (capacity %d)\n", capacity);
    } else {
        fprintf(stderr, "[PLAYLIST] Created playlist with %d tracks (capacity %d)\n", count, capacity);
    }
    return ctx;
}

const char* playlist_current_file(PlaylistContext* ctx) {
    if (!ctx || ctx->current_index < 0 || ctx->current_index >= ctx->total_tracks) {
        return NULL;
    }
    return ctx->playlist_entries[ctx->current_index].file_path;
}

const char* playlist_next_file(PlaylistContext* ctx) {
    if (!ctx) return NULL;
    
    int next_index = ctx->current_index + 1;
    if (next_index >= ctx->total_tracks) {
        return NULL;  // No next file
    }
    
    return ctx->playlist_entries[next_index].file_path;
}

int playlist_advance(PlaylistContext* ctx) {
    if (!ctx) return -1;
    
    if (ctx->total_tracks <= 0) {
        fprintf(stderr, "[PLAYLIST] Playlist is empty\n");
        return -1;
    }
    
    if (ctx->current_index + 1 >= ctx->total_tracks) {
        fprintf(stderr, "[PLAYLIST] End of playlist reached\n");
        return -1;
    }
    
    ctx->current_index++;
    fprintf(stderr, "[PLAYLIST] Advanced to track %d: %s\n", ctx->current_index, 
            ctx->playlist_entries[ctx->current_index].file_path);
    return 0;
}

int playlist_rewind(PlaylistContext* ctx) {
    if (!ctx) return -1;
    
    if (ctx->total_tracks <= 0) {
        fprintf(stderr, "[PLAYLIST] Playlist is empty\n");
        return -1;
    }
    
    if (ctx->current_index - 1 < 0) {
        fprintf(stderr, "[PLAYLIST] At beginning of playlist\n");
        return -1;
    }
    
    ctx->current_index--;
    fprintf(stderr, "[PLAYLIST] Rewound to track %d: %s\n", ctx->current_index, 
            ctx->playlist_entries[ctx->current_index].file_path);
    return 0;
}

void playlist_reset(PlaylistContext* ctx) {
    if (!ctx) return;
    ctx->current_index = (ctx->total_tracks > 0) ? 0 : -1;
    fprintf(stderr, "[PLAYLIST] Reset to beginning\n");
}

int playlist_count(PlaylistContext* ctx) {
    return (ctx) ? ctx->total_tracks : 0;
}

int playlist_index(PlaylistContext* ctx) {
    return (ctx) ? ctx->current_index : -1;
}

void playlist_destroy(PlaylistContext* ctx) {
    if (!ctx) return;
    
    if (ctx->playlist_entries) {
        free(ctx->playlist_entries);
    }
    
    free(ctx);
    fprintf(stderr, "[PLAYLIST] Destroyed\n");
}

int playlist_add_file_timed(PlaylistContext* ctx, const char* file_path, 
                            int64_t start_delay_ms, time_t scheduled_unix, 
                            const char* title) {
    if (!ctx || !file_path) {
        fprintf(stderr, "[PLAYLIST] Error: Invalid context or file path\n");
        return -1;
    }
    
    if (ctx->total_tracks >= ctx->max_capacity) {
        fprintf(stderr, "[PLAYLIST] Playlist is full (capacity %d)\n", ctx->max_capacity);
        return -1;
    }
    
    int index = ctx->total_tracks;
    
    memset(&ctx->playlist_entries[index], 0, sizeof(PlaylistEntry));
    strncpy(ctx->playlist_entries[index].file_path, file_path, 511);
    ctx->playlist_entries[index].start_delay_ms = start_delay_ms;
    ctx->playlist_entries[index].scheduled_time = scheduled_unix;
    
    // Auto-calculate duration from file if not already set
    ctx->playlist_entries[index].duration_ms = metadata_get_duration_ms(file_path);
    
    if (title) {
        strncpy(ctx->playlist_entries[index].title, title, 255);
    } else {
        // Use filename as title
        const char* basename = strrchr(file_path, '/');
        if (basename) basename++;
        else basename = file_path;
        strncpy(ctx->playlist_entries[index].title, basename, 255);
    }
    
    ctx->total_tracks++;
    
    // Activation transition:
    // when queue goes empty -> non-empty, clear idle timer and establish
    // playback cursor at index 0.
    // Implication: the engine can safely treat this as a fresh wake-up cycle.
    if (ctx->current_index < 0) {
        ctx->current_index = 0;
        ctx->last_added_timestamp = time(NULL);
        ctx->idle_timer_start = 0;  // Stop idle timer
        fprintf(stderr, "[PLAYLIST] First file added, playlist now active\n");
    }
    
    fprintf(stderr, "[PLAYLIST] Added file %d: %s (delay=%lldms, scheduled=%ld)\n", 
            index, file_path, (long long)start_delay_ms, scheduled_unix);
    
    return 0;
}

int64_t playlist_get_duration(PlaylistContext* ctx, int index) {
    if (!ctx || index < 0 || index >= ctx->total_tracks) {
        return -1;
    }

    // Lazy duration resolution decision:
    // avoid up-front metadata I/O for all entries, but guarantee duration is
    // available when a caller needs it for scheduling/telemetry.
    if (ctx->playlist_entries[index].duration_ms <= 0 &&
        ctx->playlist_entries[index].file_path[0] != '\0') {
        ctx->playlist_entries[index].duration_ms = metadata_get_duration_ms(ctx->playlist_entries[index].file_path);
    }

    return ctx->playlist_entries[index].duration_ms;
}

int64_t playlist_get_start_delay(PlaylistContext* ctx, int index) {
    if (!ctx || index < 0 || index >= ctx->total_tracks) {
        return 0;
    }
    
    return ctx->playlist_entries[index].start_delay_ms;
}

time_t playlist_get_scheduled_time(PlaylistContext* ctx, int index) {
    if (!ctx || index < 0 || index >= ctx->total_tracks) {
        return 0;
    }
    
    return ctx->playlist_entries[index].scheduled_time;
}

const char* playlist_get_title(PlaylistContext* ctx, int index) {
    if (!ctx || index < 0 || index >= ctx->total_tracks) {
        return NULL;
    }
    
    if (ctx->playlist_entries[index].title[0] != 0) {
        return ctx->playlist_entries[index].title;
    }
    
    // Return filename if title not set
    const char* basename = strrchr(ctx->playlist_entries[index].file_path, '/');
    if (basename) basename++;
    else basename = ctx->playlist_entries[index].file_path;
    
    return basename;
}

// Legacy function for backward compatibility
int playlist_add_file(PlaylistContext* ctx, const char* file_path) {
    return playlist_add_file_timed(ctx, file_path, 0, 0, NULL);
}

int playlist_remove_file(PlaylistContext* ctx, int index) {
    if (!ctx || index < 0 || index >= ctx->total_tracks) {
        fprintf(stderr, "[PLAYLIST] Invalid index: %d\n", index);
        return -1;
    }
    
    // Keep contiguous storage by compacting entries after removal.
    // Implication: indices are stable only between mutations, so callers
    // should not cache stale indices across add/remove operations.
    for (int i = index; i < ctx->total_tracks - 1; i++) {
        memcpy(&ctx->playlist_entries[i], 
               &ctx->playlist_entries[i + 1], 
               sizeof(PlaylistEntry));
    }
    
    ctx->total_tracks--;
    
    // Cursor repair decision after removal.
    // Implication: current index always points to a valid element or -1.
    if (ctx->current_index >= ctx->total_tracks) {
        ctx->current_index = ctx->total_tracks - 1;
    }
    
    if (ctx->total_tracks == 0) {
        // Empty transition starts idle timing window used by upstream logic
        // for fallback content or keep-alive behavior.
        ctx->current_index = -1;
        ctx->idle_timer_start = time(NULL);  // Start idle timer
    }
    
    fprintf(stderr, "[PLAYLIST] Removed file at index %d\n", index);
    return 0;
}

void playlist_clear(PlaylistContext* ctx) {
    if (!ctx) return;
    
    // Force clear is a hard state reset for playlist control plane.
    // Implication: all pending titles are dropped and idle timing restarts.
    ctx->total_tracks = 0;
    ctx->current_index = -1;
    ctx->idle_timer_start = time(NULL);  // Start idle timer
    
    fprintf(stderr, "[PLAYLIST] Cleared all files\n");
}

int playlist_is_empty(PlaylistContext* ctx) {
    return (ctx && ctx->total_tracks == 0) ? 1 : 0;
}

const char* playlist_get_file(PlaylistContext* ctx, int index) {
    if (!ctx || index < 0 || index >= ctx->total_tracks) {
        return NULL;
    }
    
    return ctx->playlist_entries[index].file_path;
}

time_t playlist_idle_seconds(PlaylistContext* ctx) {
    if (!ctx || ctx->total_tracks > 0 || ctx->idle_timer_start == 0) {
        return -1;  // Playlist not empty or no idle timer
    }
    
    return time(NULL) - ctx->idle_timer_start;
}
