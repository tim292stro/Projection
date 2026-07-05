# Building the Projection Cinema Engine

Status: Current as of 2026-07-05.

Transition telemetry/fail-safe hardening: Proposed complete (code integrated; deployment/runtime validation deferred).

## System Requirements

- **OS:** Ubuntu Server 22.04 LTS (headless)
- **Architecture:** x86_64
- **RAM:** 4GB minimum
- **Storage:** 10GB for system + media

## Build Dependencies

Install required packages:

```bash
sudo apt-get update
sudo apt-get install -y build-essential pkg-config

# Core media playback libraries
sudo apt-get install -y libvlc-dev libavformat-dev libavutil-dev

# Audio output
sudo apt-get install -y libasound2-dev

# Optional: Development tools
sudo apt-get install -y git curl wget vim htop
```

Alternatively, use the Makefile automation:

```bash
cd src/
make install-deps
```

## Compilation

### Build from source

```bash
cd src/
make clean
make
```

### Verify build

```bash
./sdi_cinema_engine
```

Expected output:

```text
╔════════════════════════════════════════════════════════╗
║  Projection Cinema Engine - SDI Media Server           ║
║  Frame-Accurate DCI 4K + 16-Ch AES Audio               ║
║  Ubuntu Server 22.04 LTS                              ║
╚════════════════════════════════════════════════════════╝

[ENGINE] LibVLC initialized
[ENGINE] Initialization complete
```

### Check dependencies

```bash
make show-deps
```

Output should show all three dependencies as `1`:

```text
=== Dependency Check ===
libvlc: 1
libavformat: 1
ALSA: 1
libcurl: 1
```

## New Features (2026-07-04)

### 0. Fourth Cinema Runtime Control Hooks (2026-07-05)

Two runtime orchestration hooks are now integrated in startup/transition flow:

1. DXD-16 preset switching by detected source frame-rate (23.976/24/25/29.97/30/50/59.94/60/100/119.88/120 families)
2. DeckLink Quad-Link 2SI startup enforcement via external SDK helper command

Environment variables:

```bash
# DXD-16 control (enable via base URL or explicit per-preset URLs)
export DXD16_URL="http://192.168.1.60"
export DXD16_USER="admin"                   # Optional
export DXD16_PASS="password"                # Optional
export DXD16_PRESET_A_NAME="A"             # Optional (default A)
export DXD16_PRESET_B_NAME="B"             # Optional (default B)
export DXD16_PRESET_C_NAME="C"             # Optional (default C)
export DXD16_LOCK_WAIT_MS="1500"           # Optional lock settle wait

# Optional explicit recall URLs (override DXD16_URL-derived paths)
# export DXD16_PRESET_A_URL="http://192.168.1.60/custom/recallA"
# export DXD16_PRESET_B_URL="http://192.168.1.60/custom/recallB"
# export DXD16_PRESET_C_URL="http://192.168.1.60/custom/recallC"

# DeckLink Quad-Link 2SI enforcement hook
export DECKLINK_2SI_ENFORCER_CMD="/usr/local/bin/decklink_2si_enforcer"
# export DECKLINK_SKIP_2SI_ENFORCE="1"      # Optional: disable enforcement
```

Startup behavior:

- Engine invokes `decklink_enforce_quadlink_2si()` before LibVLC init.
- Power-on and transitions invoke DXD-16 preset selection from detected video frame-rate.
- Both controls are non-fatal; playback continues if either control path is unavailable.

Build the SDK helper command on Ubuntu:

```bash
cd src/
make decklink-enforcer \
  DECKLINK_SDK_INCLUDE=/opt/blackmagic/DesktopVideo/include \
  DECKLINK_SDK_LIB=/opt/blackmagic/DesktopVideo/lib

sudo make install-decklink-enforcer

make decklink-hdr-ancillary \
  DECKLINK_SDK_INCLUDE=/opt/blackmagic/DesktopVideo/include \
  DECKLINK_SDK_LIB=/opt/blackmagic/DesktopVideo/lib

sudo make install-decklink-hdr-ancillary
```

If your SDK install uses different paths, override `DECKLINK_SDK_INCLUDE` and `DECKLINK_SDK_LIB` accordingly.

### 0.1 Content Tolerance Runtime Policy (2026-07-05)

The engine now includes a content-tolerance video policy path aligned to [README.md Section 1.2](../README.md#12-content-tolerance-policy-baseline):

- Applies Rec.2020-targeted colorspace filter path for mixed SDR ingest.
- Uses tone-map bypass mode (`--vlc-tonemap-algo=none`) to avoid forced HDR down-conversion behavior in the engine path.
- Keeps DeckLink ten-bit output path enabled.

Environment controls:

```bash
# Optional: disable content tolerance policy flags (default is enabled)
export PROJECTION_DISABLE_CONTENT_TOLERANCE="1"

# Optional: external command hook for DeckLink HDR ancillary metadata injection
# Invoked as:
#   <cmd> --file <path> --hdr <0|1> --primaries <name> --transfer <name> --matrix <name>
export DECKLINK_HDR_ANCILLARY_CMD="/usr/local/bin/decklink_hdr_ancillary_inject"
# export DECKLINK_SKIP_HDR_ANCILLARY="1"  # Optional: disable ancillary injection hook
```

Runtime checklist:

1. Start engine and verify initialization log contains `[ENGINE] LibVLC initialized with content-tolerance video policy`.

1. Run SDR BT.601 and Rec.709 assets; confirm stable projector timing and no resync flicker.

1. Run HDR/HDR10 assets; confirm no unexpected tone-map conversion behavior in the host pipeline.

1. Validate DeckLink/Christie chain behavior under mixed-content playlist transitions.

HDR ancillary integration status:

- Engine command hook is integrated (`DECKLINK_HDR_ANCILLARY_CMD`).
- DeckLink ancillary helper binary is included (`decklink_hdr_ancillary_inject`).
- On-target packet-level validation remains part of Linux runtime validation pass.

### 0.2 Media Signaling Validation Script

Use the flexible validator to enforce stream + cue + metadata minimums while keeping broad input compatibility:

```bash
cd src/
python3 tools/validate_media_standard.py /path/to/title.mp4

# Strict cue mode: require both [FEATURE] and [CREDITS]
python3 tools/validate_media_standard.py /path/to/title.mp4 --require-feature-credits

# Allow missing projection:av_sync_offset_ms as warning
python3 tools/validate_media_standard.py /path/to/title.mp4 --allow-missing-av-offset
```

### 1. Duration Auto-Calculation

When files are added to the playlist, the engine automatically:

- Opens each MP4 file
- Reads duration metadata using libavformat
- Calculates total show runtime
- Displays in marquee updates

**Function:** `metadata_get_duration_ms(file_path)` → Returns milliseconds

**Usage:** Automatic on ADD_FILE command; no manual intervention required

### 2. CalDAV Calendar Integration

Enable automatic show scheduling from CalDAV calendars (Google Calendar, Nextcloud, etc.):

**Setup:**

```bash
export CALDAV_URL="https://calendar.example.com/user/shows.ics"
export CALDAV_USER="username@example.com"
export CALDAV_PASS="password"
export CALDAV_RESOURCE_EMAIL="cinema_room@theater.com"  # Optional: cinema resource identifier

./sdi_cinema_engine
```

**Meeting Room Semantics:**

The cinema is registered as a **bookable meeting room resource** in your calendar system. When show requests arrive:

1. **Fetches pending invitations** from CalDAV every 60 seconds
2. **Validates schedule conflicts:**
   - Extracts MP4 file path from DESCRIPTION field
   - Reads actual video duration from file
   - Calculates end_time = start_time + duration
   - Checks against previously accepted bookings
3. **Auto-responds:**
   - **ACCEPTS** if no time overlap with accepted shows
   - **DECLINES** if requested time conflicts, with reason
4. **Tracks booking status:** PENDING → ACCEPTED/DECLINED → PLAYING → COMPLETED

**Event Format (iCalendar):**

```text
BEGIN:VEVENT
UID:show-20260704@venue.local
SUMMARY:Dune Part Two
DTSTART:20260704T200000Z
DESCRIPTION:file:///mnt/cinema/dune_part_two.mp4
ATTENDEE;CN="Cinema Room";PARTSTAT=NEEDS-ACTION:mailto:cinema_room@theater.com
END:VEVENT
```

**Conflict Detection Example:**

- Show A: Accepted 20:00–22:00 (2h duration)
- Show B: Requested 21:30–23:00 (1.5h duration)
- Result: DECLINED (overlaps Show A)

Engine logs both acceptance and conflict decisions:

```text
[CALDAV] ✓ ACCEPTED: Dune Part Two (UID: show-20260704@venue.local)
[CALDAV]   Start: 1751726400, End: 1751733600, Duration: 7200000ms
[CALDAV] ✗ DECLINED: The Matrix (UID: show-20260704-matrix@venue.local)
[CALDAV]   Reason: Conflict with accepted show 'Dune Part Two' (1751726400 - 1751733600)
```

**Cleaning Time Blocking (New):**

Between each show, an automatic cleaning/changeover buffer is enforced:

**Setup:**

```bash
export CLEANING_TIME_MINUTES="15"  # Default 15 minutes if not set
```

**How it works:**

- Each accepted show reserves time = `end_time + (CLEANING_TIME_MINUTES * 60 seconds)`
- Conflict detection checks against this expanded window
- Prevents back-to-back shows without changeover time

**Example with 15-minute cleaning:**

```text
Show A: 20:00–22:00 (booking end: 22:00 + 15 min cleaning = 22:15)
Show B Request: 22:10 start
Result: DECLINED - conflicts with Show A's cleaning buffer (22:00–22:15)

Show C Request: 22:15 start
Result: ACCEPTED - no overlap with Show A (22:15 ≥ 22:15)
```

**Intermission Blocks (Exception):**

Intermissions bypass cleaning time enforcement:

```bash
# Create intermission via CalDAV (example payload)
POST /caldav/
{
  "SUMMARY": "15-minute intermission",
  "DTSTART": 20260704T220000Z,
  "DURATION": "PT15M",
  "X-INTERMISSION": "true"  # Custom field
}
```

When an intermission is scheduled:

- `is_intermission` flag set to 1 in ShowEvent
- NO cleaning buffer added after intermission end
- Next show can start immediately after intermission ends

**Intermission Example:**

```text
Show A: 20:00–22:00 (booking end: 22:00 + 15 min cleaning = 22:15)
Intermission: 22:00–22:15 (no cleaning after)
Show B: 22:15 start
Result: ACCEPTED - both Show A cleaning and Intermission fit perfectly
```

**Engine logs:**

```text
[CALDAV] ✓ Intermission created: 15-minute intermission
[CALDAV]   Duration: 15 minutes (no cleaning time enforced after)
[CALDAV] ✓ ACCEPTED: Dune Part Two with cleaning buffer (22:00+15min=22:15)
[CALDAV] ✓ ACCEPTED: Inception (starts 22:15, no conflict with intermission)
```

### 2b. Live-Stage Events

Theater also hosts live stage performances (concerts, theater, comedy, etc.). These events require different setup than media playback:

**Live-Stage Event Characteristics:**

- **No media file:** Theater display driven by cover art + event text, not MP4 playback
- **No Christie projector:** Projector remains OFF (unnecessary for stage lighting)
- **No CP950A audio:** Audio system not used (live stage has its own PA)
- **Theater setup:** House lights, curtains control only
- **Marquee display:** Shows cover art image + event description text
- **Scheduling:** Subject to same calendar conflict detection + cleaning buffers as media events

**Creating a Live-Stage Event:**

Use `caldav_create_stage_event()` to book live stage time:

```c
// Example: Schedule live concert
time_t concert_start = time(NULL) + 86400;  // Tomorrow at same time
ShowEvent concert_event = {0};

int result = caldav_create_stage_event(
    caldav_ctx,
    "Live Jazz Quartet",                          // title
    "/mnt/artwork/jazz_concert_2600x1400.jpg",   // cover_art_path
    "Featuring world-renowned jazz quartet. "     // event_text
    "Two sets with 15-min intermission.",
    concert_start,
    120  // duration_minutes
);

if (result == 0) {
    fprintf(stderr, "Concert event created: %s\n", concert_event.title);
    fprintf(stderr, "Marquee image: %s\n", concert_event.cover_art_path);
    fprintf(stderr, "Event desc: %s\n", concert_event.event_text);
}
```

**Marquee Output for Live-Stage:**

MQTT publishes to `theater/marquee` with stage-specific JSON:

```json
{
  "title": "Live Jazz Quartet",
  "type": "live-stage",
  "cover_art": "/mnt/artwork/jazz_concert_2600x1400.jpg",
  "description": "Featuring world-renowned jazz quartet. Two sets with 15-min intermission.",
  "start_time": "2026-07-05T19:00:00Z"
}
```

Theater automation system should:

1. Load cover art image for marquee display
2. Render event description text below/overlay on image
3. Set house lights to 50–75% (audience preview state)
4. Open curtains at scheduled start time
5. Mute any background music/audio

**Conflict Detection for Live-Stage:**

Live-stage events subject to same rules as media playback:

```text
Live Concert: 19:00–21:00
Cleaning buffer: 15 minutes (21:00–21:15)

Show Booking Request: 20:00 start
Result: DECLINED (overlaps with concert 19:00–21:15)

Show Booking Request: 21:15 start
Result: ACCEPTED (no overlap after cleaning buffer)
```

**Power-On Behavior for Live-Stage:**

When live-stage event starts:

- Christie projector: NOT powered on
- CP950A: NOT unmuted
- House lights: Set to event preset (e.g., 75%)
- Curtains: Opened (ready for stage)
- Marquee: Live-stage cover art displayed

### 3. Projector Status Monitoring

The engine now queries actual Christie projector status via TCP:

- **Power Status:** Checks if projector is powered on (PWR query)
- **Ready State:** Monitors lamp stability and temperature (RDSM query)
- **Temperature:** Reads thermal sensor (TPMG query)
- **Power Control:** Can power projector on/off

**Power-On Sequence Flow:**

1. Power on projector
2. Poll ready status (up to 30 seconds)
3. Wait for ready confirmation
4. Unmute CP950A
5. Open curtains
6. Update marquee
7. Ready for playback

**If projector not ready after 30 seconds:** Continues anyway but logs warning

## Runtime Configuration

### Device Enumeration

Before running the engine, verify hardware detection:

```bash
# List audio devices
aplay -l

# Output should include RME card:
# card 0: RPCIe [RPCIe], device 0: RME RPCIe [RME RPCIe AES]

# List video devices (DeckLink)
lspci | grep -i blackmagic

# Output should include:
# 01:00.0 Video controller: Blackmagic Design Inc. DeckLink 8K Pro
```

### Environment Variables

Set hardware addresses if not using defaults:

```bash
export PROJECTION_RME_DEVICE="rme_cinema_map"
export PROJECTION_MQTT_BROKER="192.168.1.50"
export PROJECTION_CHRISTIE_IP="192.168.1.75"
```

## Running the Engine

### Basic playback

```bash
./sdi_cinema_engine /path/to/movie1.mp4
```

### Multiple files (automatic handover)

```bash
./sdi_cinema_engine /path/to/movie1.mp4 /path/to/movie2.mp4 /path/to/movie3.mp4
```

### With logging to file

```bash
./sdi_cinema_engine /path/to/movie.mp4 2>&1 | tee cinema_engine.log
```

## Remote Control (TCP Port 8080)

The engine listens on **port 8080** for playlist and transport commands. Send ASCII commands followed by `\r\n`.

### Running in Daemon Mode (Recommended)

Start with an empty playlist and queue files over network:

```bash
./sdi_cinema_engine
# Awaits network commands...
```

### Playlist Management

```bash
# Add files to queue
echo "ADD_FILE /path/to/movie1.mp4" | nc localhost 8080
echo "ADD_FILE /path/to/movie2.mp4" | nc localhost 8080

# List all queued files
echo "LIST_PLAYLIST" | nc localhost 8080

# Play file at index 0
echo "PLAY_TRACK 0" | nc localhost 8080

# Jump to next/previous file
echo "NEXT" | nc localhost 8080
echo "PREVIOUS" | nc localhost 8080

# Remove file at index 1
echo "REMOVE_FILE 1" | nc localhost 8080

# Clear entire playlist
echo "CLEAR_PLAYLIST" | nc localhost 8080
```

### Playback Transport

```bash
# Play (resume playback)
echo "PLAY" | nc localhost 8080

# Pause
echo "PAUSE" | nc localhost 8080

# Stop (halt playback)
echo "STOP" | nc localhost 8080
```

### Interactive Terminal

Use `telnet` for interactive commands:

```bash
telnet localhost 8080
# Type commands, press Enter after each
# Example:
#   ADD_FILE /mnt/media/title.mp4
#   PLAY_TRACK 0
#   PLAY
```

## Queue Management Features

### Empty Playlist Idle State

When the engine starts with no content in the playlist, it automatically enters **standby mode**:

- **House lights:** Set to 100% (full brightness)
- **Projector:** Shutter closed, laser power at 0
- **CP950A:** Muted
- **Curtains:** Closed
- **Status:** Waiting for network queue commands

### 5-Minute Idle Timeout

If the playlist remains empty for more than **5 minutes** (300 seconds), the engine will:

1. Power off the **Christie projector**
2. Power off the **Dolby CP950A** processor
3. Continue monitoring for new content additions

### Power-On Sequence

When the **first file is added to an empty playlist** via TCP command:

1. Power on Christie projector
2. Wait for projector ready state (up to 30 seconds)
3. Unmute CP950A
4. Open curtains
5. Update marquee display with file information
6. System ready for playback

### Adding Files with Timing

The `ADD_FILE` command now supports optional timing parameters:

```bash
# Simple: Add immediately
echo "ADD_FILE /path/to/movie.mp4" | nc localhost 8080

# With start delay (30 seconds before playback begins)
echo "ADD_FILE /path/to/movie.mp4 30000" | nc localhost 8080

# With title
echo "ADD_FILE /path/to/movie.mp4 0 \"Dune Part Two\"" | nc localhost 8080

# Response includes whether playlist was empty
# Example: STATUS_FILE_ADDED|index=0|was_empty=1
```

### Automatic CalDAV Synchronization

When content is **manually added via TCP ADD_FILE** and **CalDAV is configured**, the engine automatically:

1. **Reads video duration** from the media file
2. **Calculates show time slot** (now + optional delay = start, start + duration = end)
3. **Creates CalDAV event** to block off calendar time
4. **Posts event to calendar server** to prevent double-booking

**Benefits:**

- Operator adds content via TCP, calendar auto-updates
- Other scheduling systems see blocked time immediately
- Prevents conflicts between manual and calendar-driven bookings
- All shows synchronized across theater scheduling

**Workflow Example:**

```text
Operator runs: echo "ADD_FILE /mnt/cinema/dune.mp4 30000" | nc localhost 8080
                      ↓
Engine reads: 7200000ms duration (2 hours)
                      ↓
Calculate time slots: Start = now + 30s, End = (now + 30s) + 2h
                      ↓
CREATE VCALENDAR event and POST to CALDAV_URL
                      ↓
Calendar shows: "Dune" blocked 20:00–22:00 (starting in 30s)
```

**Engine Log Output:**

```text
[TCP] CalDAV event created for: dune.mp4
[CALDAV] Generated VEVENT for 'dune.mp4' (20260704T200000Z - 20260704T220000Z)
[CALDAV] ✓ Event posted successfully (HTTP 201)
[CALDAV] ✓ Event created and tracked locally: dune.mp4
```

**Requirements:**

- CalDAV environment variables set: CALDAV_URL, CALDAV_USER, CALDAV_PASS
- CalDAV server accepting POST requests on the configured URL
- CALDAV_RESOURCE_EMAIL configured (or uses default "<cinema_room@theater.local>")

**Conflict Checking (New):**

When files are added via TCP, the engine FIRST checks for CalDAV conflicts:

1. **Reads file duration** from MP4 metadata
2. **Calculates time window:** start_time = now + delay, end_time = start_time + duration
3. **Applies cleaning buffer:** effective_end = end_time + CLEANING_TIME_MINUTES
4. **Checks against accepted_shows[]:** Fails if any overlap found
5. **Returns error if conflict:** `ERROR_CALDAV_CONFLICT|reason=<conflict_reason>`
6. **Adds to playlist if OK:** Creates CalDAV event to track reservation

**Example - Conflict Rejection:**

```bash
# Show A already scheduled: 20:00–22:00 (with 15-min cleaning: 22:15)
echo "ADD_FILE /mnt/cinema/matrix.mp4 0 \"The Matrix\"" | nc localhost 8080
# Assuming 2h 20min content, starting now would end at 02:20 am - no immediate conflict

# But if we try to queue content for 22:00 (while Show A is running):
# This would fail because show ends at 22:00 + cleaning = 22:15
```

**Log output on conflict:**

```text
[ADD] File addition rejected: Conflict with 'Dune Part Two' plus 15-min cleaning (1751726400 - 1751733600+900)
[CALDAV] CONFLICT DETECTED: Conflict with 'Dune Part Two' plus 15-min cleaning...
ERROR_CALDAV_CONFLICT|reason=Conflict with 'Dune Part Two' plus 15-min cleaning...
```

**MQTT File Additions:**

When content is queued via MQTT messages, the engine applies the **same conflict validation**:

```json
{
  "topic": "content/add",
  "payload": {
    "file": "/mnt/cinema/inception.mp4",
    "delay_ms": 0,
    "title": "Inception"
  }
}
```

MQTT additions will:

- Read duration from file
- Check CalDAV conflicts (fails if overlaps with existing shows/cleaning)
- Create CalDAV event to reserve time
- Publish success/failure back to `content/add/response` topic

**MQTT Response Payloads:**

Success:

```json
{"status": "ok", "code": "STATUS_FILE_ADDED", "index": 3, "was_empty": 0}
```

Conflict:

```json
{"status": "error", "code": "ERROR_CALDAV_CONFLICT", "reason": "Conflict with Dune Part Two plus 15-min cleaning (...)"}
```

### Marquee Display (MQTT Topic)

When a file is added or playback begins, the engine updates the marquee via MQTT topic `theater/marquee`:

**Payload format (JSON):**

```json
{
  "title": "Dune Part Two",
  "format": "scope_2.39",
  "duration_sec": 10800,
  "start_time": "2026-07-04T20:00:00Z"
}
```

**Fields:**

- `title`: Movie/content title (from filename or metadata)
- `format`: Display format (e.g., "scope_2.39", "flat_1.85", "custom_1.37")
- `duration_sec`: Content runtime in seconds (calculated from file)
- `start_time`: Optional ISO8601 show start time (if scheduled via CalDAV)

**MQTT subscribe to marquee:**

```bash
mosquitto_sub -h 192.168.1.50 -t "theater/marquee"
```

### Projector Status Monitoring

The engine checks Christie projector status before playback:

- **Power status:** `christie_is_powered_on()` → Returns 1 if on, 0 if off
- **Ready status:** `christie_is_ready()` → Returns 1 if lamp stable and ready
- **Temperature:** `christie_get_temperature()` → Returns temp in Celsius

During power-on sequence, the engine polls projector ready status every 1 second (max 30 seconds).

### CalDAV Calendar Integration (Optional)

CalDAV scheduling is integrated. To enable:

1. Set up a CalDAV calendar with show events
2. Event format:
   - **SUMMARY:** Show title (e.g., "Dune Part Two")
   - **DTSTART:** Scheduled show time (ISO8601)
   - **DESCRIPTION:** File path or URL (e.g., "file:///mnt/cinema/dune.mp4")

3. Set environment variables before launching the engine:

- `CALDAV_URL`
- `CALDAV_USER`
- `CALDAV_PASS`
- `CALDAV_RESOURCE_EMAIL` (optional)

The engine will auto-queue content based on calendar and trigger cinema prep (power-on, curtain open, etc.) before scheduled show times.

## Systemd Service Installation

Create `/etc/systemd/system/projection.service`:

```ini
[Unit]
Description=Projection Cinema Media Server
After=network.target
Requires=mosquitto.service

[Service]
Type=simple
User=projection
WorkingDirectory=/opt/projection
ExecStartPre=/bin/sleep 5
ExecStart=/opt/projection/sdi_cinema_engine /mnt/cinema_media/playlist.m3u
Restart=on-failure
RestartSec=10
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl enable projection.service
sudo systemctl start projection.service
sudo systemctl status projection.service
```

View logs:

```bash
sudo journalctl -u projection.service -f
```

## Troubleshooting

### "libvlc not found"

```bash
pkg-config --cflags --libs libvlc
```

If empty, install: `sudo apt-get install libvlc-dev`

### "ALSA device hw:0,0 not found"

Run `aplay -l` to find correct device, then set:

```bash
export PROJECTION_RME_DEVICE="hw:1,0"  # Example for second card
```

### "cannot connect to MQTT broker"

Verify mosquitto is running:

```bash
mosquitto_version  # Should return version
ps aux | grep mosquitto
```

If not running:

```bash
sudo systemctl start mosquitto
sudo systemctl enable mosquitto
```

### Build fails with "multiple definition"

Clean and rebuild:

```bash
make clean
make
```

### Playback stutters or drops frames

Check system load and CPU scheduling:

```bash
top -p $(pgrep sdi_cinema_engine)
```

Ensure real-time scheduling is enabled:

```bash
# Set real-time priority (requires root)
sudo chrt -f 50 ./sdi_cinema_engine /path/to/media.mp4
```

## Performance Profiling

Monitor engine performance:

```bash
# In one terminal
./sdi_cinema_engine /path/to/media.mp4

# In another terminal
watch -n 0.5 'ps aux | grep sdi_cinema_engine; echo "---"; lsof -p $(pgrep sdi_cinema_engine) | grep -E "\.mp4|\.m3u"'
```

Check audio sync:

```bash
# Monitor ALSA underruns
cat /proc/asound/card0/stream0

# Monitor RME clock locking
cat /proc/asound/card0/state | grep -i clock
```

## Development Flags

For debugging builds:

```bash
# Verbose output and debug symbols
CFLAGS="-g -O0 -DDEBUG" make clean && make
```

## Next Steps

1. **Install hardware**: See [INTEGRATION.md](INTEGRATION.md) for wiring and calibration
2. **Configure metadata**: Encode MP4 files with Projection metadata atoms
3. **Test playback**: Verify SDI video and AES audio output
4. **Calibrate sync**: Use AV sync offset feature to phase-align audio/video
