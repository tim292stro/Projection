/* SPDX-License-Identifier: MIT */
/**
 * @file caldav_scheduler.h
 * @brief CalDAV calendar integration for show scheduling with meeting room semantics
 * @note Uses libcurl for HTTP/WebDAV CalDAV protocol
 * 
 * Cinema acts as a meeting "room" resource:
 * - Scheduling system invites the cinema to events
 * - Engine accepts/declines based on conflicts
 * - Validates duration against runtime metadata
 * - Manages availability calendar with RSVP replies
 * 
 * CalDAV server syncs show schedule. Event format:
 *   SUMMARY: "Dune Part Two"
 *   DTSTART: 2026-07-04T20:00:00Z (show time)
 *   DTEND: 2026-07-04T23:00:00Z (show end, auto-calculated or provided)
 *   DESCRIPTION: "file://path/to/dune.mp4"
 *   ORGANIZER: "scheduler@theater.com"
 *   ATTENDEE: "cinema_room@theater.com" (with RSVP status)
 */

#include <time.h>

typedef enum {
    SHOW_STATUS_PENDING,     // Awaiting conflict check
    SHOW_STATUS_ACCEPTED,    // Accepted (no conflicts)
    SHOW_STATUS_DECLINED,    // Declined (conflict detected)
    SHOW_STATUS_PLAYING,     // Currently playing
    SHOW_STATUS_COMPLETED,   // Finished
    SHOW_STATUS_INTERMISSION // Intermission block (no cleaning after)
} ShowStatus;

typedef enum {
    SHOW_TYPE_MEDIA,         // Film/video media file playback
    SHOW_TYPE_LIVE_STAGE     // Live stage performance (no media file)
} ShowType;

typedef struct {
    char uid[256];           // Calendar event UID for RSVP
    char title[256];         // Event summary
    char file_path[512];     // Media file path (empty for live-stage)
    char cover_art_path[512]; // Cover art image path (for live-stage events)
    char event_text[1024];   // Event description text (for live-stage events, shown on marquee)
    time_t start_time;       // Show start (unix timestamp)
    time_t end_time;         // Show end (unix timestamp)
    int64_t duration_ms;     // Content duration in milliseconds
    ShowStatus status;       // Acceptance status
    ShowType show_type;      // SHOW_TYPE_MEDIA or SHOW_TYPE_LIVE_STAGE
    char conflict_reason[512]; // If declined, why
    int is_intermission;     // 1 if intermission (skip cleaning after), 0 if regular show
} ShowEvent;

typedef struct {
    char* caldav_url;        // CalDAV calendar URL
    char* username;          // HTTP basic auth username
    char* password;          // HTTP basic auth password
    char* resource_email;    // Cinema room email (e.g., "cinema_room@theater.com")
    int cleaning_time_minutes; // Buffer between shows for cleaning (default 15)
    time_t last_sync;        // Timestamp of last sync
    int connected;
    
    // Accepted events (scheduled shows)
    ShowEvent* accepted_shows;
    int accepted_count;
} CalDAVContext;

/**
 * @brief Initialize CalDAV client with room resource email
 * @param caldav_url Calendar URL (WebDAV endpoint or .ics file HTTP URL)
 * @param username HTTP basic auth username (NULL for anonymous)
 * @param password HTTP basic auth password
 * @param resource_email Cinema room email address (e.g., "cinema_room@theater.com")
 * @return CalDAVContext, or NULL on error
 */
CalDAVContext* caldav_init(const char* caldav_url, const char* username, const char* password,
                           const char* resource_email);

/**
 * @brief Sync events from CalDAV server
 * @param ctx CalDAV context
 * @return Number of events parsed, or -1 on error
 */
int caldav_sync_events(CalDAVContext* ctx);

/**
 * @brief Check for scheduling conflicts with existing accepted shows
 * @param ctx CalDAV context (includes cleaning_time_minutes for buffer enforcement)
 * @param show_start Unix timestamp of proposed show start
 * @param show_end Unix timestamp of proposed show end
 * @param conflict_reason Output: reason for conflict (if any)
 * @return 0 if no conflict (show can be accepted), -1 if conflict detected
 * 
 * Conflict logic:
 * - For each accepted show: calculate end_time_with_cleaning = end_time + (cleaning_time_minutes * 60)
 * - UNLESS show is marked is_intermission (then no cleaning buffer after)
 * - Check: new_start < existing_end_with_cleaning AND new_end > existing_start
 */
int caldav_check_conflicts(CalDAVContext* ctx, time_t show_start, time_t show_end,
                          char* conflict_reason);

/**
 * @brief Accept a show event (add to accepted shows calendar)
 * @param ctx CalDAV context
 * @param event Show event to accept
 * @return 0 on success, -1 on error
 */
int caldav_accept_event(CalDAVContext* ctx, ShowEvent* event);

/**
 * @brief Decline a show event with conflict reason (send RSVP decline)
 * @param ctx CalDAV context
 * @param event_uid Event UID to decline
 * @param reason Decline reason
 * @return 0 on success, -1 on error
 */
int caldav_decline_event(CalDAVContext* ctx, const char* event_uid, const char* reason);

/**
 * @brief Get next pending invitation (not yet accepted/declined)
 * @param ctx CalDAV context
 * @param event Output: pending show event
 * @return 0 if event found, -1 if no pending invitations
 * 
 * Workflow:
 * 1. Fetch calendar events
 * 2. Find events not yet responded to
 * 3. Calculate end_time from start_time + file duration
 * 4. Check for conflicts
 * 5. If no conflicts, accept; if conflicts, decline
 */
int caldav_get_pending_invitation(CalDAVContext* ctx, ShowEvent* event);

/**
 * @brief Get next accepted show and its mode classification
 * @param ctx CalDAV context
 * @param show_title Output: show title
 * @param file_path Output: media file path (empty for live-stage events)
 * @param show_time Output: show start time
 * @param prep_seconds Output: seconds until show start (optional)
 * @param show_type_out Output: show type (optional)
 * @return 0 if next show found, -1 otherwise
 */
int caldav_get_next_show(CalDAVContext* ctx, char* show_title, char* file_path,
                         time_t* show_time, time_t* prep_seconds,
                         ShowType* show_type_out);

/**
 * @brief Parse iCalendar VEVENT and extract show details
 * @param vevent Raw VEVENT block (from CalDAV response)
 * @param title Output: event summary/title
 * @param description Output: event description (contains file path)
 * @param start_time Output: event start (unix timestamp)
 * @return 0 on success, -1 on parse error
 */
int caldav_parse_vevent(const char* vevent, char* title, char* description, time_t* start_time);

/**
 * @brief Create and post a new show event to CalDAV (for manually added content)
 * @param ctx CalDAV context
 * @param title Show title
 * @param file_path Media file path
 * @param start_time Show start (unix timestamp)
 * @param duration_ms Content duration in milliseconds
 * @param event_out Output: created ShowEvent (optional, can be NULL)
 * @return 0 on success, -1 on error
 * 
 * Workflow for manually added content:
 * 1. Create VEVENT with given parameters
 * 2. Calculate end_time = start_time + (duration_ms / 1000)
 * 3. Add to accepted_shows[]
 * 4. POST to CalDAV server to synchronize calendar
 * 5. Return generated ShowEvent for local tracking
 */
int caldav_create_and_post_event(CalDAVContext* ctx, const char* title, const char* file_path,
                                 time_t start_time, int64_t duration_ms, ShowEvent* event_out);

/**
 * @brief Generate iCalendar VEVENT block for a show
 * @param title Show title
 * @param file_path Media file path
 * @param start_time Show start (unix timestamp)
 * @param end_time Show end (unix timestamp)
 * @param resource_email Cinema room email (ATTENDEE field)
 * @param event_uid Output: unique event ID (e.g., "show-20260704-xxx@theater.local")
 * @param vevent_out Output buffer for generated VEVENT block (should be 2KB+ for safety)
 * @return 0 on success, -1 on error
 */
int caldav_generate_vevent(const char* title, const char* file_path,
                           time_t start_time, time_t end_time,
                           const char* resource_email,
                           char* event_uid, char* vevent_out);

/**
 * @brief POST a VEVENT to CalDAV server
 * @param ctx CalDAV context
 * @param vevent_block iCalendar VEVENT content (full VCALENDAR wrapper)
 * @return 0 on success, -1 on error
 */
int caldav_post_vevent(CalDAVContext* ctx, const char* vevent_block);

/**
 * @brief Create and post a new show event to CalDAV (for manually added content)
 * @param ctx CalDAV context
 * @param title Show title
 * @param file_path Media file path
 * @param start_time Show start (unix timestamp)
 * @param duration_ms Content duration in milliseconds
 * @param event_out Output: created ShowEvent (optional, can be NULL)
 * @return 0 on success, -1 on error
 * 
 * Workflow for manually added content:
 * 1. Create VEVENT with given parameters
 * 2. Calculate end_time = start_time + (duration_ms / 1000)
 * 3. Add to accepted_shows[]
 * 4. POST to CalDAV server to synchronize calendar
 * 5. Return generated ShowEvent for local tracking
 */
int caldav_create_and_post_event(CalDAVContext* ctx, const char* title, const char* file_path,
                                 time_t start_time, int64_t duration_ms, ShowEvent* event_out);

/**
 * @brief Create and post an intermission block (no cleaning time required after)
 * @param ctx CalDAV context
 * @param title Intermission title (e.g., "15-minute intermission")
 * @param start_time Intermission start (unix timestamp)
 * @param duration_minutes Duration in minutes (e.g., 15)
 * @param event_out Output: created ShowEvent (optional, can be NULL)
 * @return 0 on success, -1 on error
 * 
 * Intermissions are marked with is_intermission=1, which means:
 * - No cleaning buffer is enforced AFTER this event
 * - Next show can start immediately at intermission end_time
 * - Useful for scheduled breaks or changeover time
 */
int caldav_create_intermission(CalDAVContext* ctx, const char* title,
                               time_t start_time, int duration_minutes, ShowEvent* event_out);

/**
 * @brief Create and post a live-stage event to CalDAV (no media file)
 * @param ctx CalDAV context
 * @param title Stage event title
 * @param cover_art_path Path to cover art image (e.g., "/mnt/artwork/concert.jpg")
 * @param event_text Event description text for marquee (e.g., "Live concert featuring...")
 * @param start_time Event start (unix timestamp)
 * @param duration_minutes Duration in minutes
 * @param event_out Output: created ShowEvent (optional, can be NULL)
 * @return 0 on success, -1 on error
 * 
 * Live-stage events:
 * - Do NOT use media file playback
 * - Display cover art on marquee (if provided)
 * - Display event_text description on marquee
 * - Block calendar time for theater scheduling
 * - Subject to cleaning time buffers
 * - Christie projector NOT powered on
 * - CP950A NOT unmuted (audio not used)
 * - Theater setup: House lights, curtains only
 */
int caldav_create_stage_event(CalDAVContext* ctx, const char* title, const char* cover_art_path,
                              const char* event_text, time_t start_time, int duration_minutes, 
                              ShowEvent* event_out);

/**
 * @brief Disconnect and free CalDAV context
 */
void caldav_close(CalDAVContext* ctx);

#endif
