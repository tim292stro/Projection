/* SPDX-License-Identifier: MIT */
#ifndef PLAYLIST_MANAGER_H
#define PLAYLIST_MANAGER_H

#include <time.h>
#include <stdint.h>

/**
 * @file playlist_manager.h
 * @brief Playlist management with timing, durations, and dual-engine pre-buffering
 */

typedef struct {
    char file_path[512];
    int64_t duration_ms;      // Runtime duration (auto-read from file)
    int64_t start_delay_ms;   // Optional: delay before playback (default 0)
    time_t scheduled_time;    // Optional: unix timestamp for scheduled playout
    char title[256];          // Display title (optional)
} PlaylistEntry;

typedef struct {
    PlaylistEntry* playlist_entries;
    int total_tracks;
    int current_index;
    int max_capacity;
    int prebuffer_enabled;
    time_t last_added_timestamp;  // When first file added (for idle timer)
    time_t idle_timer_start;      // When queue became empty
} PlaylistContext;

/**
 * @brief Create playlist context from array of file paths (or empty if count=0)
 * @param files Array of file paths (can be NULL if count=0)
 * @param count Number of files (0 for empty playlist)
 * @param max_capacity Maximum number of files the playlist can hold (0 = no limit)
 * @return PlaylistContext, or NULL on error
 */
PlaylistContext* playlist_create(char** files, int count, int max_capacity);

/**
 * @brief Get current file path
 * @param ctx Playlist context
 * @return File path, or NULL if invalid
 */
const char* playlist_current_file(PlaylistContext* ctx);

/**
 * @brief Get next file path (for pre-buffering)
 * @param ctx Playlist context
 * @return File path, or NULL if end of playlist
 */
const char* playlist_next_file(PlaylistContext* ctx);

/**
 * @brief Advance to next track
 * @param ctx Playlist context
 * @return 0 on success, -1 if at end
 */
int playlist_advance(PlaylistContext* ctx);

/**
 * @brief Rewind to previous track
 * @param ctx Playlist context
 * @return 0 on success, -1 if at beginning
 */
int playlist_rewind(PlaylistContext* ctx);

/**
 * @brief Reset playlist to beginning
 */
void playlist_reset(PlaylistContext* ctx);

/**
 * @brief Get total track count
 */
int playlist_count(PlaylistContext* ctx);

/**
 * @brief Get current index
 */
int playlist_index(PlaylistContext* ctx);

/**
 * @brief Add file to end of playlist with timing info
 * @param ctx Playlist context
 * @param file_path Path to media file
 * @param start_delay_ms Delay before playback in milliseconds (0 = immediate)
 * @param scheduled_unix Optional: unix timestamp for scheduled playout (0 = not scheduled)
 * @param title Display title (optional, can be NULL)
 * @return 0 on success, -1 if playlist is full or error
 *
 * Decision semantics:
 * - `start_delay_ms` applies relative delay behavior for local playout logic.
 * - `scheduled_unix` preserves absolute schedule intent when provided.
 * - If both are set, caller policy determines precedence at orchestration layer.
 */
int playlist_add_file_timed(PlaylistContext* ctx, const char* file_path, 
                            int64_t start_delay_ms, time_t scheduled_unix, 
                            const char* title);

/**
 * @brief Get duration of file at index (in milliseconds)
 * @param ctx Playlist context
 * @param index Index (0-based)
 * @return Duration in ms, or -1 if invalid
 *
 * Note:
 * - Duration may be resolved lazily on first access by implementation.
 * - This enables fast ingest while still supporting deterministic timing when
 *   scheduling or telemetry requires an exact value.
 */
int64_t playlist_get_duration(PlaylistContext* ctx, int index);

/**
 * @brief Get start delay of file at index (in milliseconds)
 * @param ctx Playlist context
 * @param index Index (0-based)
 * @return Start delay in ms, or 0 if not set
 */
int64_t playlist_get_start_delay(PlaylistContext* ctx, int index);

/**
 * @brief Get scheduled unix timestamp for file at index
 * @param ctx Playlist context
 * @param index Index (0-based)
 * @return Unix timestamp, or 0 if not scheduled
 */
time_t playlist_get_scheduled_time(PlaylistContext* ctx, int index);

/**
 * @brief Get display title of file at index
 * @param ctx Playlist context
 * @param index Index (0-based)
 * @return Title string, or file basename if not set
 */
const char* playlist_get_title(PlaylistContext* ctx, int index);

/**
 * @brief Remove file at index from playlist
 * @param ctx Playlist context
 * @param index Index to remove (0-based)
 * @return 0 on success, -1 on invalid index
 */
int playlist_remove_file(PlaylistContext* ctx, int index);

/**
 * @brief Clear all files from playlist
 * @param ctx Playlist context
 */
void playlist_clear(PlaylistContext* ctx);

/**
 * @brief Check if playlist is empty
 * @param ctx Playlist context
 * @return 1 if empty, 0 if has files
 */
int playlist_is_empty(PlaylistContext* ctx);

/**
 * @brief Get file at specific index
 * @param ctx Playlist context
 * @param index Index (0-based)
 * @return File path, or NULL if invalid index
 */
const char* playlist_get_file(PlaylistContext* ctx, int index);

/**
 * @brief Check time since last file was added (for idle timeout)
 * @param ctx Playlist context
 * @return Seconds since first file was added, or -1 if playlist empty
 */
time_t playlist_idle_seconds(PlaylistContext* ctx);

/**
 * @brief Free playlist context
 */
void playlist_destroy(PlaylistContext* ctx);

#endif
