/* SPDX-License-Identifier: MIT */
/**
 * @file caldav_scheduler.c
 * @brief CalDAV calendar integration implementation
 * @compile: gcc ... -lcurl
 */

#include "caldav_scheduler.h"
#include "metadata_extractor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <time.h>

#define MAX_CALDAV_RESPONSE 65536

typedef struct {
    char* data;
    size_t size;
} CURLBuffer;

static size_t curl_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    CURLBuffer* buf = (CURLBuffer*)userp;
    
    char* ptr = realloc(buf->data, buf->size + realsize + 1);
    if (!ptr) {
        fprintf(stderr, "[CALDAV] Not enough memory for curl response\n");
        return 0;
    }
    
    buf->data = ptr;
    memcpy(&(buf->data[buf->size]), contents, realsize);
    buf->size += realsize;
    buf->data[buf->size] = 0;
    
    return realsize;
}

static void format_utc_now(char* out, size_t out_len) {
    time_t now = time(NULL);
    struct tm tm_info = {0};
    gmtime_r(&now, &tm_info);
    strftime(out, out_len, "%Y%m%dT%H%M%SZ", &tm_info);
}

static int caldav_post_rsvp_reply(CalDAVContext* ctx,
                                  const char* event_uid,
                                  const char* partstat,
                                  const char* summary,
                                  const char* comment) {
    if (!ctx || !event_uid || !partstat) {
        return -1;
    }

    char dtstamp[32] = {0};
    format_utc_now(dtstamp, sizeof(dtstamp));

    char safe_summary[256] = {0};
    char safe_comment[512] = {0};
    strncpy(safe_summary, summary && summary[0] ? summary : "Projection Room Response", sizeof(safe_summary) - 1);
    strncpy(safe_comment, comment ? comment : "", sizeof(safe_comment) - 1);

    // RSVP replies are emitted as METHOD:REPLY so remote schedulers can
    // reconcile room availability without manual intervention.
    // Decision impact: if this call fails, local state may still be correct,
    // but external booking systems can remain out-of-sync.
    char vevent_block[4096] = {0};
    snprintf(vevent_block, sizeof(vevent_block),
             "BEGIN:VCALENDAR\r\n"
             "VERSION:2.0\r\n"
             "PRODID:-//Projection Engine//CalDAV RSVP//EN\r\n"
             "METHOD:REPLY\r\n"
             "BEGIN:VEVENT\r\n"
             "UID:%s\r\n"
             "DTSTAMP:%s\r\n"
             "SUMMARY:%s\r\n"
             "ATTENDEE;CN=Cinema_Room;PARTSTAT=%s;RSVP=FALSE:mailto:%s\r\n"
             "COMMENT:%s\r\n"
             "END:VEVENT\r\n"
             "END:VCALENDAR\r\n",
             event_uid,
             dtstamp,
             safe_summary,
             partstat,
             ctx->resource_email ? ctx->resource_email : "cinema_room@theater.local",
             safe_comment);

    return caldav_post_vevent(ctx, vevent_block);
}

CalDAVContext* caldav_init(const char* caldav_url, const char* username, const char* password,
                           const char* resource_email) {
    if (!caldav_url) {
        fprintf(stderr, "[CALDAV] Error: NULL CalDAV URL\n");
        return NULL;
    }
    
    CalDAVContext* ctx = (CalDAVContext*)malloc(sizeof(CalDAVContext));
    if (!ctx) return NULL;
    
    ctx->caldav_url = (char*)malloc(strlen(caldav_url) + 1);
    if (!ctx->caldav_url) {
        free(ctx);
        return NULL;
    }
    strcpy(ctx->caldav_url, caldav_url);
    
    if (username) {
        ctx->username = (char*)malloc(strlen(username) + 1);
        strcpy(ctx->username, username);
    } else {
        ctx->username = NULL;
    }
    
    if (password) {
        ctx->password = (char*)malloc(strlen(password) + 1);
        strcpy(ctx->password, password);
    } else {
        ctx->password = NULL;
    }
    
    if (resource_email) {
        ctx->resource_email = (char*)malloc(strlen(resource_email) + 1);
        strcpy(ctx->resource_email, resource_email);
    } else {
        ctx->resource_email = (char*)malloc(32);
        strcpy(ctx->resource_email, "cinema_room@theater.local");
    }
    
    // Read cleaning time from environment (default 15 minutes)
    const char* cleaning_env = getenv("CLEANING_TIME_MINUTES");
    ctx->cleaning_time_minutes = (cleaning_env) ? atoi(cleaning_env) : 15;
    if (ctx->cleaning_time_minutes < 0) ctx->cleaning_time_minutes = 0;
    
    ctx->last_sync = 0;
    ctx->connected = 1;
    ctx->accepted_shows = (ShowEvent*)malloc(sizeof(ShowEvent) * 100);
    ctx->accepted_count = 0;
    
    fprintf(stderr, "[CALDAV] Initialized as room resource: %s (cleaning buffer: %d min)\n",
           ctx->resource_email, ctx->cleaning_time_minutes);

    return ctx;
}

int caldav_sync_events(CalDAVContext* ctx) {
    if (!ctx) return -1;
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "[CALDAV] Failed to initialize CURL\n");
        return -1;
    }
    
    CURLBuffer response = {0};
    response.data = (char*)malloc(1);
    response.size = 0;
    
    curl_easy_setopt(curl, CURLOPT_URL, ctx->caldav_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    // Set HTTP basic auth if credentials provided
    if (ctx->username && ctx->password) {
        char auth[512];
        snprintf(auth, sizeof(auth), "%s:%s", ctx->username, ctx->password);
        curl_easy_setopt(curl, CURLOPT_USERPWD, auth);
    }
    
    // This sync is intentionally lightweight: fetch calendar payload and count
    // VEVENT markers as a health/visibility indicator.
    // Decision impact: this is not a full parser pass, so event_count is
    // observability data rather than authoritative schedule state.
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "[CALDAV] CURL error: %s\n", curl_easy_strerror(res));
        free(response.data);
        curl_easy_cleanup(curl);
        return -1;
    }
    
    ctx->last_sync = time(NULL);
    
    // Count VEVENT entries in response (simple heuristic)
    int event_count = 0;
    char* ptr = response.data;
    while ((ptr = strstr(ptr, "BEGIN:VEVENT")) != NULL) {
        event_count++;
        ptr += 12;
    }
    
    fprintf(stderr, "[CALDAV] Synced %d events from calendar\n", event_count);
    
    free(response.data);
    curl_easy_cleanup(curl);
    return event_count;
}
int caldav_parse_vevent(const char* vevent, char* title, char* description, time_t* start_time) {
    if (!vevent || !title || !description || !start_time) return -1;
    
    title[0] = 0;
    description[0] = 0;
    *start_time = 0;
    
    const char* summary_start = strstr(vevent, "SUMMARY:");
    if (summary_start) {
        summary_start += 8;
        const char* summary_end = strchr(summary_start, '\n');
        if (summary_end) {
            int len = summary_end - summary_start;
            if (len > 0 && summary_start[len-1] == '\r') len--;
            strncpy(title, summary_start, len < 255 ? len : 255);
            title[len] = 0;
        }
    }
    
    const char* desc_start = strstr(vevent, "DESCRIPTION:");
    if (desc_start) {
        desc_start += 12;
        const char* desc_end = strchr(desc_start, '\n');
        if (desc_end) {
            int len = desc_end - desc_start;
            if (len > 0 && desc_start[len-1] == '\r') len--;
            strncpy(description, desc_start, len < 511 ? len : 511);
            description[len] = 0;
        }
    }
    
    const char* dtstart = strstr(vevent, "DTSTART:");
    if (dtstart) {
        dtstart += 8;
        char dtstart_str[32];
        int i = 0;
        while (i < 31 && dtstart[i] != '\n' && dtstart[i] != '\r' && dtstart[i] != 0) {
            dtstart_str[i] = dtstart[i];
            i++;
        }
        dtstart_str[i] = 0;
        
        *start_time = parse_iso8601_datetime(dtstart_str);
    }
    
    return (*start_time > 0) ? 0 : -1;
}

static int parse_vevent_uid(const char* vevent, char* uid_out, size_t uid_out_len) {
    if (!vevent || !uid_out || uid_out_len == 0) {
        return -1;
    }

    const char* uid_start = strstr(vevent, "UID:");
    if (!uid_start) {
        return -1;
    }

    uid_start += 4;
    const char* uid_end = strchr(uid_start, '\n');
    if (!uid_end) {
        return -1;
    }

    size_t len = (size_t)(uid_end - uid_start);
    while (len > 0 && (uid_start[len - 1] == '\r' || uid_start[len - 1] == '\n')) {
        len--;
    }

    if (len == 0) {
        return -1;
    }

    if (len >= uid_out_len) {
        len = uid_out_len - 1;
    }

    memcpy(uid_out, uid_start, len);
    uid_out[len] = 0;
    return 0;
}

int caldav_check_conflicts(CalDAVContext* ctx, time_t show_start, time_t show_end,
                          char* conflict_reason) {
    if (!ctx || !conflict_reason) return -1;
    
    conflict_reason[0] = 0;
    
    // Check each accepted show for overlap (accounting for cleaning time)
    for (int i = 0; i < ctx->accepted_count; i++) {
        ShowEvent* accepted = &ctx->accepted_shows[i];
        
        // Calculate effective end with post-show turnover buffer.
        // Decision controlled here: normal shows enforce cleaning slack;
        // intermissions intentionally do not, enabling tight split-program
        // timelines where the break itself is the operational buffer.
        time_t effective_end = accepted->end_time;
        if (!accepted->is_intermission) {
            effective_end += (ctx->cleaning_time_minutes * 60);  // Add cleaning buffer in seconds
        }
        
        // Overlap rule models half-open time ranges.
        // Implication: boundary-touching events are allowed when one ends
        // exactly when the next starts (unless cleaning extends effective_end).
        if (show_start < effective_end && show_end > accepted->start_time) {
            if (accepted->is_intermission) {
                snprintf(conflict_reason, 511,
                        "Conflict with intermission '%s' (%ld - %ld)",
                        accepted->title, (long)accepted->start_time, (long)accepted->end_time);
            } else {
                snprintf(conflict_reason, 511,
                        "Conflict with '%s' plus %d-min cleaning (%ld - %ld+%d)",
                        accepted->title, ctx->cleaning_time_minutes,
                        (long)accepted->start_time, (long)accepted->end_time, 
                        ctx->cleaning_time_minutes * 60);
            }
            fprintf(stderr, "[CALDAV] CONFLICT DETECTED: %s\n", conflict_reason);
            return -1;  // Conflict found
        }
    }
    
    return 0;  // No conflicts
}

int caldav_accept_event(CalDAVContext* ctx, ShowEvent* event) {
    if (!ctx || !event) return -1;
    
    if (ctx->accepted_count >= 100) {
        fprintf(stderr, "[CALDAV] Warning: Accepted shows buffer full, cannot accept more\n");
        return -1;
    }
    
    // Add to accepted shows list
    memcpy(&ctx->accepted_shows[ctx->accepted_count], event, sizeof(ShowEvent));
    ctx->accepted_count++;
    
    event->status = SHOW_STATUS_ACCEPTED;
    
    fprintf(stderr, "[CALDAV] ✓ ACCEPTED: %s (UID: %s)\n", event->title, event->uid);
    fprintf(stderr, "[CALDAV]   Start: %ld, End: %ld, Duration: %lldms\n",
           (long)event->start_time, (long)event->end_time, (long long)event->duration_ms);
    
    if (caldav_post_rsvp_reply(ctx, event->uid, "ACCEPTED", event->title, "Automatically accepted by Projection scheduler") != 0) {
        fprintf(stderr, "[CALDAV] Warning: Failed to send RSVP ACCEPTED reply for UID: %s\n", event->uid);
    }
    
    return 0;
}

int caldav_decline_event(CalDAVContext* ctx, const char* event_uid, const char* reason) {
    if (!ctx || !event_uid || !reason) return -1;
    
    fprintf(stderr, "[CALDAV] ✗ DECLINED: %s\n", event_uid);
    fprintf(stderr, "[CALDAV]   Reason: %s\n", reason);
    
    if (caldav_post_rsvp_reply(ctx, event_uid, "DECLINED", "Projection Scheduler Decline", reason) != 0) {
        fprintf(stderr, "[CALDAV] Warning: Failed to send RSVP DECLINED reply for UID: %s\n", event_uid);
    }
    
    return 0;
}

int caldav_get_pending_invitation(CalDAVContext* ctx, ShowEvent* event) {
    if (!ctx || !event) return -1;

    memset(event, 0, sizeof(*event));
    fprintf(stderr, "[CALDAV] Checking for pending invitations...\n");

    char show_title[256] = {0};
    char file_path[512] = {0};
    time_t show_time = 0;
    time_t prep_seconds = 0;
    ShowType show_type = SHOW_TYPE_MEDIA;

    // This module currently resolves "pending invitation" by selecting the
    // next conflict-free event candidate via caldav_get_next_show.
    // Implication: event state is derived from practical schedulability rather
    // than explicit PARTSTAT inspection on inbound attendee records.
    if (caldav_get_next_show(ctx, show_title, file_path, &show_time, &prep_seconds, &show_type) != 0) {
        return -1;
    }

    strncpy(event->title, show_title, sizeof(event->title) - 1);
    strncpy(event->file_path, file_path, sizeof(event->file_path) - 1);
    event->start_time = show_time;
    event->show_type = show_type;
    event->status = SHOW_STATUS_PENDING;

    // Media events compute end_time from real metadata duration to align with
    // automation transitions and conflict windows.
    if (show_type == SHOW_TYPE_MEDIA && file_path[0] != '\0') {
        event->duration_ms = metadata_get_duration_ms(file_path);
        if (event->duration_ms > 0) {
            event->end_time = show_time + (event->duration_ms / 1000);
        }
    }

    // Fallback for non-media or unknown duration paths.
    // Decision impact: keeps scheduler deterministic even when exact runtime
    // duration is unavailable from media metadata.
    if (event->end_time == 0 && prep_seconds > 0) {
        event->end_time = show_time + prep_seconds;
    }

    return 0;
}
static time_t parse_iso8601_datetime(const char* iso_str) {
    struct tm tm_info = {0};
    
    // Parse format: 2026-07-04T20:00:00Z or 2026-07-04T20:00:00
    if (sscanf(iso_str, "%d-%d-%dT%d:%d:%d", 
               &tm_info.tm_year, &tm_info.tm_mon, &tm_info.tm_mday,
               &tm_info.tm_hour, &tm_info.tm_min, &tm_info.tm_sec) != 6) {
        return 0;
    }
    
    tm_info.tm_year -= 1900;  // Years since 1900
    tm_info.tm_mon -= 1;      // Months are 0-11
    tm_info.tm_isdst = -1;    // Auto-detect DST
    
    return mktime(&tm_info);
}

static int parse_vevent_dtend(const char* vevent, time_t* end_time_out) {
    if (!vevent || !end_time_out) {
        return -1;
    }

    const char* dtend = strstr(vevent, "DTEND:");
    if (!dtend) {
        return -1;
    }

    dtend += 6;
    char dtend_str[32] = {0};
    int i = 0;
    while (i < 31 && dtend[i] != '\n' && dtend[i] != '\r' && dtend[i] != 0) {
        dtend_str[i] = dtend[i];
        i++;
    }
    dtend_str[i] = 0;

    time_t parsed = parse_iso8601_datetime(dtend_str);
    if (parsed <= 0) {
        return -1;
    }

    *end_time_out = parsed;
    return 0;
}

int caldav_get_next_show(CalDAVContext* ctx, char* show_title, char* file_path, 
                         time_t* show_time, time_t* prep_seconds,
                         ShowType* show_type_out) {
    if (!ctx) return -1;
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "[CALDAV] Failed to initialize CURL for event fetch\n");
        return -1;
    }
    
    CURLBuffer response = {0};
    response.data = (char*)malloc(1);
    response.size = 0;
    
    curl_easy_setopt(curl, CURLOPT_URL, ctx->caldav_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    if (ctx->username && ctx->password) {
        char auth[512];
        snprintf(auth, sizeof(auth), "%s:%s", ctx->username, ctx->password);
        curl_easy_setopt(curl, CURLOPT_USERPWD, auth);
    }
    
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "[CALDAV] CURL error fetching events: %s\n", curl_easy_strerror(res));
        free(response.data);
        curl_easy_cleanup(curl);
        return -1;
    }
    
    ctx->last_sync = time(NULL);
    
    // Process calendar entries with room-resource semantics:
    // 1) candidate classification (media vs live-stage),
    // 2) duration resolution,
    // 3) conflict enforcement,
    // 4) earliest schedulable selection.
    time_t now = time(NULL);
    time_t next_show_time = 0;
    ShowEvent next_show = {0};
    
    char* vevent_start = response.data;
    while ((vevent_start = strstr(vevent_start, "BEGIN:VEVENT")) != NULL) {
        char* vevent_end = strstr(vevent_start, "END:VEVENT");
        if (!vevent_end) break;
        
        // Extract VEVENT block
        int vevent_len = vevent_end - vevent_start + 10;
        char* vevent_block = (char*)malloc(vevent_len + 1);
        if (!vevent_block) break;
        
        strncpy(vevent_block, vevent_start, vevent_len);
        vevent_block[vevent_len] = 0;
        
        char title[256] = {0};
        char desc[512] = {0};
        time_t event_time = 0;
        
        if (caldav_parse_vevent(vevent_block, title, desc, &event_time) == 0) {
            ShowType candidate_type = SHOW_TYPE_MEDIA;
            int64_t duration_ms = 0;
            time_t event_end = 0;
            char file_path_temp[512] = {0};
            char event_uid[256] = {0};

            if (parse_vevent_uid(vevent_block, event_uid, sizeof(event_uid)) != 0) {
                strncpy(event_uid, title, sizeof(event_uid) - 1);
            }

            const char* file_start = strstr(desc, "file://");
            int has_media_uri = (file_start != NULL);

            // Classification decision:
            // - missing media URI or explicit live-stage marker => stage event
            // - otherwise => media playback event
            // Operational implication: stage events avoid media-duration lookup
            // and are bounded by DTSTART/DTEND from calendar data.
            if (!has_media_uri || strstr(desc, "LIVE_STAGE") || strstr(desc, "live-stage")) {
                candidate_type = SHOW_TYPE_LIVE_STAGE;

                // Stage events must provide a valid DTEND.
                // Implication: malformed stage bookings are skipped to avoid
                // uncertain occupancy windows that could block valid shows.
                if (parse_vevent_dtend(vevent_block, &event_end) != 0 || event_end <= event_time) {
                    free(vevent_block);
                    vevent_start = vevent_end + 10;
                    continue;
                }

                duration_ms = (int64_t)(event_end - event_time) * 1000;
            } else {
                const char* file_end = file_start;
                while (*file_end && *file_end != ' ' && *file_end != '\n' && *file_end != '\r') {
                    file_end++;
                }

                int path_len = (int)(file_end - file_start);
                if (path_len <= 0 || path_len >= 511) {
                    free(vevent_block);
                    vevent_start = vevent_end + 10;
                    continue;
                }

                strncpy(file_path_temp, file_start, path_len);
                file_path_temp[path_len] = 0;

                extern int64_t metadata_get_duration_ms(const char* file_path);
                duration_ms = metadata_get_duration_ms(file_path_temp);

                // If media duration cannot be determined, skip scheduling this
                // candidate because downstream transition and conflict logic
                // requires deterministic end timing.
                if (duration_ms <= 0) {
                    free(vevent_block);
                    vevent_start = vevent_end + 10;
                    continue;
                }

                event_end = event_time + (duration_ms / 1000);
            }

            // Apply a rolling 24h scheduling window.
            // Implication: near-term automation remains stable while far-future
            // planning stays in calendar systems rather than runtime memory.
            if (event_time >= now && event_time <= (now + 86400)) {
                char conflict_reason[512] = {0};
                int has_conflict = caldav_check_conflicts(ctx, event_time, event_end, conflict_reason);

                if (has_conflict == 0) {
                    // Keep earliest conflict-free candidate as next show.
                    // Implication: deterministic first-fit behavior when
                    // multiple acceptable bookings exist in the same horizon.
                    if (next_show_time == 0 || event_time < next_show_time) {
                        next_show_time = event_time;
                        strncpy(next_show.title, title, 255);
                        strncpy(next_show.file_path, file_path_temp, 511);
                        next_show.start_time = event_time;
                        next_show.end_time = event_end;
                        next_show.duration_ms = duration_ms;
                        next_show.status = SHOW_STATUS_PENDING;
                        next_show.show_type = candidate_type;
                        strncpy(next_show.uid, event_uid, 255);
                    }
                } else {
                    // Conflict path actively notifies upstream scheduler via RSVP
                    // decline so the room can be rebooked/resolved promptly.
                    fprintf(stderr, "[CALDAV] CONFLICT: %s - %s\n", title, conflict_reason);
                    caldav_decline_event(ctx, event_uid, conflict_reason);
                }
            }
        }
        
        free(vevent_block);
        vevent_start = vevent_end + 10;
    }
    
    free(response.data);
    curl_easy_cleanup(curl);
    
    // Accept exactly one selected candidate and return it to caller.
    // Decision impact: acceptance mutates local conflict state immediately,
    // preventing duplicate acceptance in subsequent polling cycles.
    if (next_show_time > 0 && show_title && file_path && show_time) {
        caldav_accept_event(ctx, &next_show);
        
        strncpy(show_title, next_show.title, 255);
        strncpy(file_path, next_show.file_path, 511);
        *show_time = next_show.start_time;

        if (show_type_out) {
            *show_type_out = next_show.show_type;
        }
        
        if (prep_seconds) {
            *prep_seconds = next_show.start_time - now;
        }
        
        fprintf(stderr, "[CALDAV] Next accepted show: %s (in %ld seconds, ends at %ld)\n", 
               show_title, (long)(next_show.start_time - now), (long)next_show.end_time);
        
        return 0;
    }
    
    
    fprintf(stderr, "[CALDAV] No available shows without conflicts\n");
    return -1;
}

static void unix_time_to_iso8601(time_t unix_time, char* iso_out) {
    struct tm* tm_info = gmtime(&unix_time);
    strftime(iso_out, 32, "%Y%m%dT%H%M%SZ", tm_info);
}

int caldav_generate_vevent(const char* title, const char* file_path,
                           time_t start_time, time_t end_time,
                           const char* resource_email,
                           char* event_uid, char* vevent_out) {
    if (!title || !file_path || !event_uid || !vevent_out || !resource_email) return -1;
    
    // Generate unique event UID
    snprintf(event_uid, 256, "show-%ld-%s@theater.local", (long)start_time, title);
    // Remove spaces from UID
    for (char* p = event_uid; *p; p++) {
        if (*p == ' ') *p = '_';
    }
    
    // Convert times to ISO8601
    char dtstart[32] = {0};
    char dtend[32] = {0};
    unix_time_to_iso8601(start_time, dtstart);
    unix_time_to_iso8601(end_time, dtend);
    
    // Generate VCALENDAR with VEVENT
    snprintf(vevent_out, 2048,
        "BEGIN:VCALENDAR\r\n"
        "VERSION:2.0\r\n"
        "PRODID:-//Cinema Automation//Media Engine//EN\r\n"
        "CALSCALE:GREGORIAN\r\n"
        "METHOD:REQUEST\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:%s\r\n"
        "DTSTART:%s\r\n"
        "DTEND:%s\r\n"
        "SUMMARY:%s\r\n"
        "DESCRIPTION:%s\r\n"
        "ORGANIZER;CN=Cinema_Engine:mailto:cinema@theater.local\r\n"
        "ATTENDEE;CN=Cinema_Room;PARTSTAT=ACCEPTED;RSVP=FALSE:mailto:%s\r\n"
        "STATUS:CONFIRMED\r\n"
        "CREATED:%s\r\n"
        "LAST-MODIFIED:%s\r\n"
        "SEQUENCE:0\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n",
        event_uid, dtstart, dtend, title, file_path, resource_email, dtstart, dtstart);
    
    fprintf(stderr, "[CALDAV] Generated VEVENT for '%s' (%s - %s)\n", title, dtstart, dtend);
    return 0;
}

int caldav_post_vevent(CalDAVContext* ctx, const char* vevent_block) {
    if (!ctx || !vevent_block) return -1;
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "[CALDAV] Failed to initialize CURL for POST\n");
        return -1;
    }
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: text/calendar");
    headers = curl_slist_append(headers, "Expect:");
    
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, ctx->caldav_url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, vevent_block);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    if (ctx->username && ctx->password) {
        char auth[512];
        snprintf(auth, sizeof(auth), "%s:%s", ctx->username, ctx->password);
        curl_easy_setopt(curl, CURLOPT_USERPWD, auth);
    }
    
    CURLBuffer response = {0};
    response.data = (char*)malloc(1);
    response.size = 0;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response);
    
    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "[CALDAV] CURL POST error: %s (HTTP %ld)\n", curl_easy_strerror(res), http_code);
        free(response.data);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return -1;
    }
    
    // 201 Created or 200 OK
    if (http_code != 201 && http_code != 200) {
        fprintf(stderr, "[CALDAV] POST failed with HTTP %ld\n", http_code);
        fprintf(stderr, "[CALDAV] Response: %s\n", response.data);
        free(response.data);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return -1;
    }
    
    fprintf(stderr, "[CALDAV] ✓ Event posted successfully (HTTP %ld)\n", http_code);
    free(response.data);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return 0;
}

int caldav_create_and_post_event(CalDAVContext* ctx, const char* title, const char* file_path,
                                 time_t start_time, int64_t duration_ms, ShowEvent* event_out) {
    if (!ctx || !title || !file_path) return -1;
    
    // Calculate end time from duration
    time_t end_time = start_time + (duration_ms / 1000);
    
    // Generate VEVENT block
    char event_uid[256] = {0};
    char vevent_block[2048] = {0};
    
    if (caldav_generate_vevent(title, file_path, start_time, end_time, 
                              ctx->resource_email, event_uid, vevent_block) != 0) {
        return -1;
    }
    
    // Post to CalDAV server
    if (caldav_post_vevent(ctx, vevent_block) != 0) {
        fprintf(stderr, "[CALDAV] Warning: Event creation posted but may not have persisted\n");
        // Don't fail here - local tracking is still important
    }
    
    // Add to local accepted_shows[] for conflict tracking
    if (ctx->accepted_count >= 100) {
        fprintf(stderr, "[CALDAV] Warning: Accepted shows buffer full\n");
        return -1;
    }
    
    ShowEvent new_event = {0};
    strncpy(new_event.uid, event_uid, 255);
    strncpy(new_event.title, title, 255);
    strncpy(new_event.file_path, file_path, 511);
    new_event.start_time = start_time;
    new_event.end_time = end_time;
    new_event.duration_ms = duration_ms;
    new_event.status = SHOW_STATUS_ACCEPTED;
    new_event.show_type = SHOW_TYPE_MEDIA;  // Media file playback
    new_event.is_intermission = 0;  // Regular show, cleaning required after
    strcpy(new_event.conflict_reason, "");
    
    memcpy(&ctx->accepted_shows[ctx->accepted_count], &new_event, sizeof(ShowEvent));
    ctx->accepted_count++;
    
    if (event_out) {
        memcpy(event_out, &new_event, sizeof(ShowEvent));
    }
    
    
    fprintf(stderr, "[CALDAV] ✓ Event created and tracked locally: %s\n", title);
    fprintf(stderr, "[CALDAV]   Time: %s - %s (%lldms duration)\n",
           title, title, (long long)duration_ms);  // Simplified logging
    
    return 0;
}

int caldav_create_intermission(CalDAVContext* ctx, const char* title,
                               time_t start_time, int duration_minutes, ShowEvent* event_out) {
    if (!ctx || !title || duration_minutes <= 0) return -1;
    
    time_t end_time = start_time + (duration_minutes * 60);
    int64_t duration_ms = duration_minutes * 60 * 1000;
    
    // Generate VEVENT block
    char event_uid[256] = {0};
    char vevent_block[2048] = {0};
    
    if (caldav_generate_vevent(title, "", start_time, end_time,
                              ctx->resource_email, event_uid, vevent_block) != 0) {
        return -1;
    }
    
    // Post to CalDAV server
    if (caldav_post_vevent(ctx, vevent_block) != 0) {
        fprintf(stderr, "[CALDAV] Warning: Intermission posted but may not have persisted\n");
    }
    
    // Add to local accepted_shows[] with is_intermission = 1
    if (ctx->accepted_count >= 100) {
        fprintf(stderr, "[CALDAV] Warning: Accepted shows buffer full\n");
        return -1;
    }
    
    ShowEvent intermission = {0};
    strncpy(intermission.uid, event_uid, 255);
    strncpy(intermission.title, title, 255);
    intermission.start_time = start_time;
    intermission.end_time = end_time;
    intermission.duration_ms = duration_ms;
    intermission.status = SHOW_STATUS_INTERMISSION;
    intermission.is_intermission = 1;  // NO cleaning buffer after intermission
    strcpy(intermission.conflict_reason, "");
    
    memcpy(&ctx->accepted_shows[ctx->accepted_count], &intermission, sizeof(ShowEvent));
    ctx->accepted_count++;
    
    if (event_out) {
        memcpy(event_out, &intermission, sizeof(ShowEvent));
    }
    
    fprintf(stderr, "[CALDAV] ✓ Intermission created: %s\n", title);
    fprintf(stderr, "[CALDAV]   Duration: %d minutes (no cleaning time enforced after)\n", duration_minutes);
    
    return 0;
}

int caldav_create_stage_event(CalDAVContext* ctx, const char* title, const char* cover_art_path,
                              const char* event_text, time_t start_time, int duration_minutes, 
                              ShowEvent* event_out) {
    if (!ctx || !title || duration_minutes <= 0) return -1;
    
    time_t end_time = start_time + (duration_minutes * 60);
    int64_t duration_ms = duration_minutes * 60 * 1000;
    
    // Generate VEVENT block (no file path for stage events, use event_text in description)
    char event_uid[256] = {0};
    char vevent_block[2048] = {0};
    
    // Use caller-provided description when available; otherwise fall back to
    // event title so calendar entries stay human-readable.
    const char* description = (event_text && event_text[0]) ? event_text : title;
    
    if (caldav_generate_vevent(title, description, start_time, end_time,
                              ctx->resource_email, event_uid, vevent_block) != 0) {
        return -1;
    }
    
    // Post to CalDAV server
    if (caldav_post_vevent(ctx, vevent_block) != 0) {
        fprintf(stderr, "[CALDAV] Warning: Stage event posted but may not have persisted\n");
    }
    
    // Add to local accepted_shows[] with show_type = SHOW_TYPE_LIVE_STAGE
    if (ctx->accepted_count >= 100) {
        fprintf(stderr, "[CALDAV] Warning: Accepted shows buffer full\n");
        return -1;
    }
    
    ShowEvent stage_event = {0};
    strncpy(stage_event.uid, event_uid, 255);
    strncpy(stage_event.title, title, 255);
    // file_path left empty for live-stage
    if (cover_art_path) {
        strncpy(stage_event.cover_art_path, cover_art_path, 511);
    }
    strncpy(stage_event.event_text, description, 1023);
    stage_event.start_time = start_time;
    stage_event.end_time = end_time;
    stage_event.duration_ms = duration_ms;
    stage_event.status = SHOW_STATUS_ACCEPTED;
    stage_event.show_type = SHOW_TYPE_LIVE_STAGE;  // Live stage, not media file
    stage_event.is_intermission = 0;  // Regular event (cleaning time applies)
    strcpy(stage_event.conflict_reason, "");
    
    memcpy(&ctx->accepted_shows[ctx->accepted_count], &stage_event, sizeof(ShowEvent));
    ctx->accepted_count++;
    
    if (event_out) {
        memcpy(event_out, &stage_event, sizeof(ShowEvent));
    }
    
    fprintf(stderr, "[CALDAV] ✓ Stage event created: %s\n", title);
    fprintf(stderr, "[CALDAV]   Duration: %d minutes (cleaning time enforced after)\n", duration_minutes);
    fprintf(stderr, "[CALDAV]   Type: LIVE_STAGE (no media file, projector, or CP950A)\n");
    if (cover_art_path) {
        fprintf(stderr, "[CALDAV]   Cover art: %s\n", cover_art_path);
    }
    if (event_text) {
        fprintf(stderr, "[CALDAV]   Description: %s\n", event_text);
    }
    
    return 0;
}

void caldav_close(CalDAVContext* ctx) {
    if (!ctx) return;
    
    if (ctx->caldav_url) free(ctx->caldav_url);
    if (ctx->username) free(ctx->username);
    if (ctx->password) free(ctx->password);
    if (ctx->resource_email) free(ctx->resource_email);
    if (ctx->accepted_shows) free(ctx->accepted_shows);
    
    free(ctx);
    fprintf(stderr, "[CALDAV] Disconnected\n");
}
