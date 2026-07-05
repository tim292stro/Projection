# Projection — Cinema Automation System Specification

---

## 1. System Overview

Projection is a **headless Ubuntu Server application** (`sdi_cinema_engine.c`) that replaces a commercial Digital Cinema Package (DCP) server with a fully open, programmable alternative for home use. It manages:

- **Playback** — gapless, frame-accurate presentation of cinema-packaged MP4 files
- **Hardware routing** — direct PCIe-level bypass of desktop audio/video subsystems
- **Enable Theater automation** — physical curtains, motorized lens sled, Christie shutter, and house lighting triggered by frame-accurate embedded metadata cues (media mode only)
- **Digital signage** — poster art and show metadata extracted atomically during pre-buffering and served to lobby display arrays

---

## 1.1 Current Implementation Baseline

### Runtime Status

- Fourth-gen cinema timing/output controls integrated
- DXD-16 preset orchestration integrated (A/B/C frame-rate families)
- DeckLink Quad-Link 2SI startup enforcement integrated through helper command path
- Live-stage safety isolation integrated (projector OFF, CP950A muted, curtain/lighting automation suppressed)
- MQTT ingest subscription integrated (`content/add` add-queue path with response publishing)
- Transition telemetry and deterministic fail-safe hold logic integrated (runtime validation pending)
- Content tolerance runtime policy flags integrated (including DeckLink ancillary helper command path)

### Live-Mode Isolation Rule

When a CalDAV event is classified as `SHOW_TYPE_LIVE_STAGE`, Projection enters **live mode** and becomes an OR-control system to stand-alone live theater control.

In live mode, this controller SHALL:

- Force projector power state to OFF
- Force Dolby CP950A to MUTED
- Suppress MQTT curtain and lighting automation commands from this engine

In live mode, this controller SHALL NOT:

- Power on the projector
- Unmute CP950A
- Issue curtain/lighting automation commands

### Runtime Defaults and Control Paths

- LibVLC sync policy: `--clock-synchro=0` and `--clock-jitter=0`
- ALSA target default: `rme_cinema_map` with fallback to `hw:0,0`
- DeckLink startup helper command: `DECKLINK_2SI_ENFORCER_CMD` (default `decklink_2si_enforcer`)
- DXD-16 control configured by `DXD16_URL` and optional preset-name/URL overrides

### Implemented Modules Beyond Original Spec Draft

- `src/modules/dxd16_control.{h,c}`: source frame-rate detection and DXD preset recall orchestration
- `src/modules/decklink_enforcer.{h,c}`: startup enforcement hook for DeckLink 2SI path
- `src/tools/decklink_2si_enforcer.cpp`: DeckLink SDK helper binary source
- `src/tools/decklink_hdr_ancillary_inject.cpp`: DeckLink HDR ancillary helper command source

### Remaining Work Tracking

Remaining implementation work pending runtime access:

- Linux validation pass on target Ubuntu host
- End-to-end live-stage flow test via CalDAV stage API
- Transition telemetry/fail-safe hardening runtime validation

### 1.2 Content Tolerance Policy Baseline

This section consolidates the project content-tolerance policy for mixed-source ingest.

Policy intent:

- Normalize legacy BT.601 SDR and standard Rec.709 SDR into Rec.2020 target gamut space.
- Preserve SDR transfer intent (no forced SDR-to-HDR remapping).
- Keep HDR/HDR10/HLG content in passthrough-oriented processing mode.
- Hold a fixed projector output timing profile to avoid hardware resync/flicker.

Target output profile:

| Parameter | Target | Purpose |
| --- | --- | --- |
| Resolution | 4096 × 2160 DCI | Prevents projector timing resyncs |
| Pixel format | 10-bit SDI output path | Deterministic ingest to Christie chain |
| Color primaries | Rec.2020 | Matches Christie RGB laser gamut target |
| Color matrix | bt2020nc | Standard UHD matrix handling |
| SDR transfer | Source-native SDR intent | Avoids cross-container SDR curve distortion |
| HDR transfer | ST-2084 / HLG passthrough intent | Preserves HDR mastering intent |

Dynamic media processing modes:

1. Legacy BT.601 SDR:
Transforms source gamut toward Rec.2020 while keeping SDR transfer behavior.

1. Rec.709 SDR:
Maps primaries into Rec.2020 workspace while preserving SDR transfer behavior.

1. Native HDR (PQ/HLG):
Follows passthrough-oriented path with no forced tone-map rewrite in engine policy mode.

Current implementation mapping:

- Runtime flags integrated in engine (`--video-filter=colorspace{all=bt2020nc:primaries=bt2020}`, `--target-prim=bt2020`, `--vlc-tonemap-algo=none`, `--swscale-mode=0`).
- Toggle available with `PROJECTION_DISABLE_CONTENT_TOLERANCE=1`.
- HDR ancillary metadata injection hook integrated in engine via external command path.
- Remaining work: wrapper-level DeckLink SDK implementation for ST 2086 and related ancillary packets on target Linux host.

---

## 2. Hardware Architecture

### 2.1 Signal Flow

```text
                    +------------------------------------------+
                    |  Brainstorm Electronics DXD-16 Clock     |
                    +------------------------------------------+
                  |                          |
               (Genlock / Tri-Level Sync)    (Word Clock Reference)
                  |                          |
                           v                          v
+---------------------+   +------------------------------------------+
|  Central SAN / NFS  |-->|  Ubuntu Server — sdi_cinema_engine (C)   |
+---------------------+   +------------------------------------------+
                      |                        |
                   (Dual/Quad 12G-SDI)    (16-Ch AES/EBU — custom DB25→2×RJ45 cable)
                      |                        |
                                  v                        v
                    +-------------------------+   +----------------------+
                    | Christie 4K-RGBH        |   | Dolby CP950A Atmos   |
                    | Sapphire Projector      |   | Processor            |
                    +-------------------------+   +----------------------+
                                                            |
                                          (Dolby Atmos Connect — BLU link)
                                                            |
                                                            v
                                                  +---------------------+
                                                  | BSS BLU-DA (BLU-DAN)|
                                                  | BLU link → AES67    |
                                                  | compatibility bridge|
                                                  +---------------------+
                                                            |
                                                  (63-Ch AES67 Multicast)
                                                            |
                                                            v
                                                  +---------------------+
                                                  | DiGiCo Quantum-7t   |
                                                  | (DMI-DANTE card)    |
                                                  +---------------------+
                                                            |
                                                  (Console Output /
                                                   Matrix Routing)
                                                            |
                                                            v
                                                  +---------------------+
                                                  | Meyer Sound Arrays  |
                                                  +---------------------+
```

> [!note] Diagram Notes
>
> - **"16-Ch AES/EBU — custom DB25→2×RJ45 cable"** — The RME HDSPe AES outputs on DB25 (Tascam pinout). The CP950A's AES digital input uses **2× RJ45 connectors** (labeled AES 1-8 and AES 9-16) — the same physical format used on Dolby's own IMB/IMS cinema servers. These are electrically and physically incompatible with DB25 Tascam. A custom breakout cable is required. Dolby Part 7501670 performs the reverse direction (Dolby IMB/IMS dual-RJ45 output → DB25 input on older processors); for this project the direction is inverted (RME DB25 output → CP950A dual-RJ45 input). See [[Dolby CP950A]] for full pinout and cable guidance.
> - **"Dolby Atmos Connect"** — the CP950A's AES67 implementation is a proprietary Dolby protocol, not standard Dante. It is not visible in Dante Controller without a compatibility bridge. Confirmed by Dolby professional support: *"The CP950 and CP950A are not Dante devices and the AES67 implementation, Dolby Atmos Connect, was originally designed for use with other Dolby products."* Source: [Dolby Professional Support Community, Jan 2024](https://dolby.my.site.com/professionalsupport/s/question/0D54u0000AF6f5vCQB/)
> - **BSS BLU-DA as bridge** — the BLU-DA receives the CP950A's Dolby Atmos Connect stream on its BLU link port and re-emits it as standard AES67 multicast. Confirmed working by community user: *"I used a BLU-DAN to convert from BLU-link to AES67 and the flows show up in Dante as expected."* This makes the BLU-DA an essential compatibility shim, not an optional component.

### 2.2 Clock Domain Management

| Clock Signal                 | Source            | Destination                  | Interface            |
| ---------------------------- | ----------------- | ---------------------------- | -------------------- |
| Tri-Level Sync / Black Burst | Brainstorm DXD-16 | DeckLink 8K Pro G2           | BNC Genlock input    |
| Word Clock                   | Brainstorm DXD-16 | RME HDSPe AES                | BNC Word Clock input |
| Software timeline policy     | Hardware-led sync | LibVLC (`--clock-synchro=0`) | Driver               |

> [!warning] Critical Rule
> The RME card and the DeckLink card **must** be locked to the same DXD-16 clock domain. Any variance between the two hardware clocks forces a software resample that degrades frame-accurate timer resolution.

**LibVLC software sync flags:**

| Flag | Value | Effect |
| --- | --- | --- |
| `--clock-synchro` | `0` | Disables software correction; hardware timing domain leads |
| `--clock-jitter` | `0` | Zero software PTS drift tolerance; LibVLC drops/micro-skips video frames to follow audio |
| `--cr-average` | `10000` | Maximizes smoothing index to stay aligned with external hardware clocking |

### 2.3 Video Pipeline

- **Output card:** Blackmagic Design DeckLink 8K Pro G2
- **LibVLC routing:** `--vout=decklink --decklink-ten-bits`
- **Display resolution:** 4096×2160 DCI 4K (fixed output to Christie projector)
- **Content scaling:** Any input resolution (SD, HD, 2K, 4K, or non-standard) is scaled to 4096×2160
- **Scaling behavior:** Hardware scaling fills display without letterboxing; horizontal/vertical scaling applied as needed
- **Pixel aspect:** 1:1 (square pixels); content aspect ratio detected from metadata or pixel dimensions

**Hardware Compensation**:

- **Lens anamorphic adjustment:** Infinitely variable aspect targeting, settable from detected content aspect ratio to correct horizontal distortion while scaling incoming video across active imager pixels.
- **Laser brightness compensation:** Calculated per Constant Image Height (CIH) formula using actual aspect ratio; brighter laser for wider aspect ratios (more beam spread) and dimmer laser for narrower aspect ratios (less beam spread) to maintain screen-surface illumination.

### 2.4 Audio Pipeline

**Discrete LPCM Mode** (uncompressed multi-channel source):

- `--audio-filter=none` — bypasses software DSP and volume normalization
- ALSA route policy (`rme_cinema_map`) handles CP950-compatible channel ordering
- Channels stream linearly across RME pins into the Dolby CP950A

**Compressed Bitstream Passthrough Mode** (Dolby E / TrueHD / Atmos):

- `--spdif` — enables digital encapsulation, bypasses internal software decoder
- Raw Dolby payload transmitted over AES cables; CP950A decodes spatial metadata natively

**ALSA device target:** `rme_cinema_map` (with `hw:0,0` fallback if route alias unavailable)

### 2.5 AV Sync Offset Control

The downstream audio path (Dolby CP950A → BSS BLU-DA → DiGiCo Quantum-7t → Meyer arrays) may introduce latency that slips relative to the video SDI output. The engine provides **granular, per-title audio delay compensation** to phase-align video and audio at the auditorium speakers.

**Adjustment range:** $\pm 3000$ milliseconds

**Granularity:** $1$ sample @ 48 kHz = $\frac{1}{48000}$ s = $20.833$ µs

**Maximum samples:** $3 \text{ s} \times 48000 \text{ Hz} = 144,000$ samples

**Semantics:**

- **Positive offset:** Audio is delayed (late relative to video). Must be advanced/pulled earlier during playback.
- **Negative offset:** Video is delayed (audio is early relative to video). Must be delayed/pushed later during playback.

**Configuration:** The offset is stored as a signed 32-bit sample count in the MP4 file's Projection metadata box during ingest:

```c
typedef struct {
    int32_t av_sync_offset_samples;  // Range: −144000 to +144000 samples
                                      // Positive = audio is late, needs advancement
                                      // Negative = audio is early, needs delay
    uint32_t reserved;
} ProjectionMetadata;
```

**Playback implementation:**

At the start of each media file, the engine reads `av_sync_offset_samples` from the metadata and applies it by:

1. **Positive offset (audio is late):** Advance the RME ALSA clock read pointer by $n$ samples, effectively pulling audio **forward** (earlier) in time relative to the video timeline.
2. **Negative offset (audio is early):** Delay audio by $|n|$ samples before transmission to RME. Implemented via a ring buffer pre-fill in the ALSA callback.

```c
// Pseudo-code: AV sync offset application
typedef struct {
    libvlc_media_player_t* player;
    int32_t av_sync_offset_samples;
    int64_t playback_start_sample_count;
    uint8_t audio_delay_buffer[4096];  // Temp buffer for negative delay
} MediaPlaybackContext;

void apply_av_sync_offset(MediaPlaybackContext* ctx) {
    if (ctx->av_sync_offset_samples < 0) {
        // Negative offset: delay audio by buffering samples before output
        int32_t delay_samples = -ctx->av_sync_offset_samples;
        // Fill delay buffer with silence, then normal audio
        memset(ctx->audio_delay_buffer, 0, delay_samples * sizeof(uint32_t));
        ctx->playback_start_sample_count = delay_samples;
    } else if (ctx->av_sync_offset_samples > 0) {
        // Positive offset: advance RME read pointer
        // ALSA snd_pcm_delay() will report fewer samples available,
        // causing LibVLC to skip forward in playback timeline
        ctx->playback_start_sample_count = -ctx->av_sync_offset_samples;
    }
}
```

**Calibration procedure:**

1. Play a tone sweep or audio reference signal through the system
2. Use an oscilloscope or audio analyzer to measure the phase difference between the SDI video sync pulse and the audio output at the speaker terminals
3. Calculate the measured delay in samples: $\text{delay}_{\text{samples}} = \text{measured delay (s)} \times 48000$
   - If audio arrives **after** video: `delay_samples` is positive (audio is late)
   - If audio arrives **before** video: `delay_samples` is negative (audio is early)
4. Set `av_sync_offset_samples` to this measured value directly — positive if audio is late, negative if early
5. Re-measure after applying the offset; the system will compensate to bring audio back into sync
6. Iterate until phase alignment is within ±1 sample (±20 µs)

**Limits:**

- Offsets exceeding ±3 seconds (±144,000 samples) are clamped to the boundary and logged as warnings
- Changes take effect on the **next title play**; mid-playback adjustments are queued for the next file
- Zero offset is the default; no behavior change if metadata is absent

---

## 3. Media Engine Architecture

### 3.1 Build & Dependencies

```bash
# Ubuntu Server package prerequisites
apt install libvlc-dev libavformat-dev libavutil-dev libasound2-dev libcurl4-openssl-dev

# Build
cd src && make clean && make
```

### 3.2 LibVLC Initialization Parameter Block

```c
const char* native_vlc_arguments[] = {
    "--intf=dummy",                   // Absolute headless — no GUI
    "--vout=decklink",
    "--decklink-ten-bits",            // Force 10-bit SDI output path
    "--force-aspect-ratio=17:9",      // Full 17:9 render canvas
    "--zoom=1.0",
    "--no-autoscale",
    "--aout=alsa",
    "--alsa-audio-device=rme_cinema_map", // Routed ALSA alias (fallback to hw:0,0 in engine)
    "--audio-channels=16",            // 16 discrete un-resampled channels
    "--audio-filter=none",
    "--no-audio-time-stretch",
    "--spdif",                        // Adaptive Dolby bitstream passthrough
    "--codec=a52",                    // Dolby passthrough path
    "--no-dts-passthrough",           // Decode DTS locally
    "--clock-synchro=0",              // Disable software correction
    "--clock-jitter=0",               // Zero PTS drift tolerance
    "--cr-average=10000",             // Max smoothing for external hardware timing
    "--no-osd",                       // Block system text on SDI output
    "--no-video-title-show",          // Block filename pop-ups
    "--network-caching=30000",        // 30-second look-ahead stream buffer
    "--file-caching=30000"            // Matching local storage cache
};
```

> [!warning] Audio Passthrough and Routing Integrity
> The flags `--audio-filter=none`, `--spdif`, and `--codec=a52` are critical to preserve Dolby passthrough behavior and avoid unintended software attenuation paths. Channel-order normalization is performed at ALSA route layer (`rme_cinema_map`) rather than by LibVLC remixing. This is essential for:
> Dolby Atmos: spatial object metadata embedded in discrete channels must pass bit-perfect to the Dolby CP950A decoder; bitstream passthrough: E-AC-3 Dolby audio packets must not be decoded and re-encoded; cinema audio fidelity: theatrical mixes are mastered for specific channel configurations and must not be altered.
> [!note] AV Sync Offset Not in LibVLC Parameters
> The AV sync offset is **not** a LibVLC command-line argument. Instead, it is extracted from per-file Projection metadata at load time and applied directly in the ALSA PCM callback before samples reach the RME hardware. This ensures frame-accurate timing and avoids LibVLC's internal sample-rate-conversion heuristics, which would undermine the offset granularity. See [[Section 8.5: Projection-Specific Metadata]] for storage and retrieval details.

### 3.3 Dual-Engine Ping-Pong Architecture

Standard sequential playlists introduce connection gaps over high-latency NFS shares. The engine maintains **two independent `libvlc_media_player_t` instances** (`player_A`, `player_B`) that leapfrog each other.

```text
Time ──────────────────────────────────────────────────────────────────>

[Player A]  ══════════════════ Playing ══════════════════> [STOP / RECLAIM]
                                         |
                           T-30s boundary hit
                                         |
                                         v
[Player B]              [ASYNC CONNECT & PRE-BUFFER] ══> [TAKE ACTIVE OUTPUT BUS]
```

**State variables:**

```c
libvlc_media_player_t* cinema_player_A  = NULL;
libvlc_media_player_t* cinema_player_B  = NULL;
int current_active_is_A                 = 1;
int next_engine_is_cached               = 0;
pthread_mutex_t playback_engine_mutex   = PTHREAD_MUTEX_INITIALIZER;
```

### 3.4 Look-Ahead Pre-Buffer Logic

1. A background monitor loop polls the active player's timeline position once per second (`usleep(1000000)`).
2. At **T−30 seconds** remaining, `execute_background_prebuffer()` spawns on the idle engine:
   - Opens the next file via `avformat_open_input` to extract metadata and poster art
   - Writes marquee assets atomically to `/var/www/marquee/coming_soon.png`
   - Loads the file into the idle LibVLC instance with `libvlc_media_player_set_pause(player, 1)` to fill the 30-second network/file cache
3. On `libvlc_Ended`, `execute_gapless_handover()` acquires the mutex, promotes `coming_soon.png` → `now_playing.png`, and unpauses the pre-loaded engine with no intervening black frame.

---

## 4. Playback Transition State Machine

Between playlist items, the engine gates all physical theater hardware before resuming playback:

```text
[Active File Ends]
       │
       ▼
┌──────────────────────────────────┐
│  TRANSITION STATE                │
├──────────────────────────────────┤
│ 1. Send Christie (SHU 1)         │──► Await socket ACK
│ 2. Mute Dolby CP950A output      │──► Keep RME synchronized
│ 3. ffprobe next file → A (float) │──► Compute laser slope
│ 4. Read Projection metadata      │──► AV sync offset (ms → samples)
│ 5. Send Christie (LPLV nnn)      │
│ 6. MQTT → lens_sled: set_aspect  │
│ 7. MQTT → masking_curtains: move │
└──────────────────────────────────┘
       │
       ▼ (block on MQTT feedback loop — 50ms micro-bursts)
       │
[CURTAIN_READY == true AND LENS_READY == true]
       │
       ▼
┌──────────────────────────────────┐
│  PLAYBACK READY                  │
├──────────────────────────────────┤
│ 1. Send Christie (SHU 0)         │
│ 2. Unmute Dolby CP950A           │──► Audio delivery resumes
│ 3. Apply AV sync offset to ALSA  │──► Audio delay compensation active
│ 4. Unpause pre-loaded engine     │──► First 2s: silent black (mechanical settle)
└──────────────────────────────────┘
```

> [!note] Mechanical Safety Buffer
> All media assets are pre-encoded with **2 seconds of black video + digital silence** at both head and tail, providing a 2000 ms window for motorized hardware (lens sled, curtain tracks, douser blade) to actuate completely before any image reaches the screen.

### 4.1 CIH Optical Calibration (Constant Image Height)

For Scope content, an anamorphic lens sled widens the DLP image horizontally. As the lens expands, light disperses over a larger surface area. The engine compensates with a pre-calculated linear laser power slope sent to Christie on every aspect ratio change.

**Calibration anchor points:**

| Aspect Ratio | Description | Required Power | Christie LPLV |
| --- | --- | --- | --- |
| 1.78:1 | Flat / HDTV baseline | 55% | 550 |
| 2.40:1 | Scope limit | 82% | 820 |

**Slope equation:**

$$P = m \cdot A + b$$

Where $A$ is the floating-point aspect ratio and $P$ is the Christie LPLV value (0–1000 scale):

$$P = 435.484 \cdot A - 225.154$$

**Safety envelope clamp:**

```c
int calculate_target_laser_power(float aspect_ratio) {
    int raw = (int)((435.484f * aspect_ratio) - 225.154f);
    return (raw < 200) ? 200 : (raw > 1000) ? 1000 : raw;
}
```

Aspect ratio is extracted from the next file via `ffprobe` before any shutter opens. The `display_aspect_ratio` stream tag is parsed first; width/height pixel division is used as a fallback.

### 4.2 Christie Projector TCP Protocol

| Command | Syntax | Effect |
| --- | --- | --- |
| Shutter close | `(SHU 1)\r\n` | Drops mechanical douser blade |
| Shutter open | `(SHU 0)\r\n` | Raises mechanical douser blade |
| Laser power | `(LPLV nnn)\r\n` | Sets laser drive level (0–1000) |

**Connection:** TCP socket to `192.168.1.75:3002`, `SO_TIMEOUT = 2.0s`, opened/closed per command.

### 4.3 MQTT Hardware Control Bus

**Broker:** `192.168.1.50:1883`

| Topic (publish) | Payload | Purpose |
| --- | --- | --- |
| `theater/hardware/cmd` | `{"device":"dolby_cp950a","action":"mute"}` | Mute Dolby CP950A output (RME continues streaming for sync) |
| `theater/hardware/cmd` | `{"device":"dolby_cp950a","action":"unmute"}` | Unmute Dolby CP950A output |
| `theater/hardware/cmd` | `{"device":"lens_sled","action":"set_aspect_target","value":2.39}` | Move anamorphic lens sled |
| `theater/hardware/cmd` | `{"device":"masking_curtains","action":"set_aspect_target","value":2.39}` | Move screen masking curtains |

| Topic (subscribe) | Expected payload | Meaning |
| --- | --- | --- |
| `theater/hardware/status/+` | `{"status":"READY"}` | Hardware reports mechanical alignment complete |

**MQTT QoS:** Commands use QoS 2; automation macro cues use QoS 1.

---

## 5. Frame-Accurate Timed Metadata Cues

Mid-show triggers (house lighting, practical effects) are embedded directly in the MP4 container as a **private binary timed metadata track** — completely decoupled from subtitle/caption rendering.

**Authoring approach:**

- Cue payloads are JSON strings written inside an SRT/WebVTT file
- Muxed as an ISOBMFF Timed Metadata track tagged `-c:d bin_data`
- Tagged as a data track so LibVLC passes raw binary packets to the application logic loop without rendering them as subtitles

**Engine interception hook:**

```c
void on_elementary_metadata_frame(const libvlc_event_t* event, void* data) {
    // Hook registered via:
    // libvlc_event_attach(mgr, libvlc_MediaPlayerElementaryStreamReceived,
    //                     on_elementary_metadata_frame, NULL);
    //
    // Production: parse event->u.media_player_es_data.p_packet->p_buffer
    // Example: if (raw_bytes[0] == 0xA1) trigger HOUSE_LIGHTS_DOWN
}
```

Packets are published to `theater/macros` on the MQTT bus for downstream hardware controllers.

---

## 6. TCP Remote Command Interface

The engine binds a control server on **port 8080** (`INADDR_ANY`) in a dedicated `pthread`.

| Command string | Response | Action |
| --- | --- | --- |
| `PLAY` | `STATUS_OK` | Resume active player |
| `PAUSE` | `STATUS_OK` | Pause active player |
| `NEXT` | `STATUS_HANDOVER_FORCED` | Immediately trigger gapless handover |
| `SET_SUB_TRACK=n` | `STATUS_SUBTRACK_CHANGED` | Select subtitle track by index |
| `SUB_OFF` | `STATUS_SUBTRACK_DISABLED` | Disable all subtitles |

Commands are null-terminated ASCII, `\r\n` stripped. Each connection handles one command and closes.

---

## 7. Digital Marquee System

At T−30s pre-buffer, the engine extracts metadata and poster art from the **upcoming** file and writes to the filesystem. On handover, the "coming soon" asset atomically becomes "now playing."

### 7.1 Asset Extraction (libavformat)

```c
AVFormatContext* ctx = NULL;
avformat_open_input(&ctx, file_path, NULL, NULL);
avformat_find_stream_info(ctx, NULL);

// Metadata atoms (raw, un-clobbered)
AVDictionaryEntry* title   = av_dict_get(ctx->metadata, "title",   NULL, 0);
AVDictionaryEntry* specs   = av_dict_get(ctx->metadata, "comment", NULL, 0);
AVDictionaryEntry* story   = av_dict_get(ctx->metadata, "synopsis",NULL, 0);

// Poster art
for (int i = 0; i < ctx->nb_streams; i++) {
    if (ctx->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC) {
        AVPacket pkt = ctx->streams[i]->attached_pic;
        // fwrite(pkt.data, pkt.size, 1, img_file);
    }
}
```

> [!warning] libavformat vs libvlc_media_get_meta
> `libvlc_media_get_meta` collapses `comment`, `description`, and `synopsis` into the same `libvlc_meta_Description` string. **Always use raw libavformat dict calls** to read these fields independently.

### 7.2 File Layout

| Path | Contents | Updated |
| --- | --- | --- |
| `/var/www/marquee/now_playing.png` | Poster art for active presentation | On handover (atomic rename) |
| `/var/www/marquee/coming_soon.png` | Poster art for next item | At T−30s pre-buffer |
| `/var/www/marquee/signage_data.txt` | Title, start time, specs, synopsis | At T−30s pre-buffer |

`signage_data.txt` format:

```text
TITLE: <value>
ESTIMATED_START: HH:MM:SS
HARDWARE_SPECS: <comment atom>
SYNOPSIS: <synopsis atom>
```

---

## 8. Media Package Specification

### 8.1 MP4 Container Track Layout

| Track | Content | Format |
| --- | --- | --- |
| Track 1 | Video | H.264 / HEVC / ProRes Cinema Profile |
| Track 2 | Master audio | Discrete LPCM or Dolby Bitstream |
| Track 3+ | Subtitle languages | Selectable Timed Text (SRT/WebVTT) |
| Metadata track | Automation cue array | Private binary timed metadata (`bin_data`) |
| Disposition `attached_pic` | Poster art | High-resolution PNG |

### 8.2 Metadata Atom Schema

| FFmpeg key | QuickTime atom | Purpose |
| --- | --- | --- |
| `title` | `©nam` | Lobby marquee display name |
| `comment` | `©cmt` | Technical A/V specifications string |
| `description` | `desc` | Brief plot description |
| `synopsis` | `ldes` | Full theatrical narrative block |
| *(image)* | `covr` (`AV_DISPOSITION_ATTACHED_PIC`) | Binary poster art export |

### 8.3 Content Rating Schema (iTunEXTC)

Stored in the iTunes custom box `----:com.apple.iTunes:iTunEXTC`. Pipe-separated: `Board|Rating|SeverityCode|`.

**Universal severity matrix:**

| Code | Meaning | US (MPAA) | UK (BBFC) | Europe (PEGI) | Australia (ACB) |
| --- | --- | --- | --- | --- | --- |
| 100 | General audiences | G | U | 3 | G |
| 200 | Parental guidance | PG | PG | 7 | PG |
| 300 | Early teen | PG-13 | 12A / 12 | 12 | M |
| 400 | Restricted / mature | R | 15 | 16 | MA15+ |
| 500 | Adult only | NC-17 | 18 | 18 | R18+ |
| 600 | Unrated / explicit | UNRATED | E | X | X18+ |

**FFmpeg injection examples:**

```bash
# MPAA R rating
ffmpeg -i input.mp4 -c copy -metadata iTunEXTC="mpaa|R|400|" output.mp4

# BBFC 15 rating
ffmpeg -i input.mp4 -c copy -metadata iTunEXTC="uk-movie|15|400|" output.mp4
```

**C parser (severity extraction at T−30s):**

```c
char board[16] = {0}, rating[16] = {0};
int severity = 0;
if (sscanf(tag_rating->value, "%15[^|]|%15[^|]|%d", board, rating, &severity) >= 3) {
    if (severity >= 400)
        system("/usr/local/bin/update_marquee_layout --overlay=restricted_advisory");
}
```

### 8.4 Macro Cues: Theater Automation Triggers

Macro cues are frame-accurate automation triggers embedded in the MP4 file that synchronize theater hardware (house lights, curtains, marquee) with video playback.

**Storage method:** MP4 chapter markers (native FFmpeg/VLC support)

**Cue types relevant to cinema:**

- `[FEATURE]` — Triggers at ~100-500ms after file start; dims house lights to 5%, initiates projector ramp-up
- `[CREDITS]` — Triggers at ~30 seconds before file end; raises house lights to 50%, begins end-of-show sequence

**Reserved (not used in cinema context):**

- `[INTRO]` — Pre-feature content
- `[RECAP]` — Post-credit summary

**Storage format:**

MP4 chapters are stored in the container's chapter track (`chap` atom). Each chapter entry contains:

| Field | Type | Example |
| ------- | ------ | --------- |
| Timecode | uint64 (milliseconds) | 500 |
| Chapter title | UTF-8 string | `[FEATURE]` |
| Chapter description | UTF-8 string (optional JSON) | `{"action":"dim_house_lights","target_level":5}` |

**Creating chapters with FFmpeg:**

```bash
# Method 1: From text file (timecode format HH:MM:SS.mmm = Chapter Title)
cat > chapters.txt << 'EOF'
00:00:00.500 [FEATURE]
00:19:58.000 [CREDITS]
EOF

ffmpeg -i source.mp4 -i chapters.txt -map 0 -map_chapters 1 -codec copy -y output.mp4
```

```bash
# Method 2: Direct FFmpeg metadata injection
ffmpeg -i source.mp4 -codec copy \
  -metadata:c:s title="[FEATURE]" \
  -metadata:c:s timecode="00:00:00.500" \
  -y output.mp4
```

```bash
# Method 3: MP4Box
mp4box -add-chapter 0:00:00.500="[FEATURE]" \
       -add-chapter 0:19:58.000="[CREDITS]" \
       source.mp4 -o output.mp4
```

**Verification with FFprobe:**

```bash
ffprobe -v error -show_chapters -of json title.mp4

# Output shows chapter start times and titles:
# [
#   {
#     "id": 1,
#     "start": 500,
#     "start_time": "0.500000",
#     "tags": { "title": "[FEATURE]" }
#   },
#   {
#     "id": 2,
#     "start": 1198000,
#     "start_time": "1198.000000",
#     "tags": { "title": "[CREDITS]" }
#   }
# ]
```

**Playback implementation:**

During playback, the engine monitors LibVLC's current timecode once per second. When the playback position exceeds a macro cue timecode:

1. Extract cue title from MP4 chapter metadata
2. Parse cue type (`[FEATURE]`, `[CREDITS]`, etc.)
3. Publish MQTT event on topic `theater/macro_cue`:

   ```json
   {
     "cue_type": "feature",
     "timecode_ms": 500,
     "action": "dim_house_lights",
     "target_level_percent": 5,
     "mqtt_topic": "theater/automation"
   }
   ```

4. Automation system responds with `READY` acknowledgment
5. Engine logs event to systemd journal with microsecond precision

**MQTT topics for theater automation:**

| Topic | Payload | Notes |
| ------- | --------- | ------- |
| `theater/macro_cue` | `{"cue_type":"feature"}` | Feature start detected |
| `theater/macro_cue` | `{"cue_type":"credits"}` | Credits start detected |
| `theater/house_lights/dim` | `{"target_level":5,"ramp_ms":2000}` | Dim to 5% over 2 seconds |
| `theater/house_lights/raise` | `{"target_level":50,"ramp_ms":1000}` | Raise to 50% over 1 second |

**Timing precision:**

Macro cues are frame-accurate (±1 frame @ 24fps = ±41.67ms). The engine compares:

- LibVLC playback position (polled once per second at 100ms granularity)
- Chapter timecodes (extracted from MP4 metadata)

For sub-second precision (±100ms or better), the optional JSON description payload can encode a callback hook that triggers downstream MQTT subscribers to perform fine-grained synchronization with their own audio timeline.

---

### 8.5 Projection-Specific Metadata: AV Sync Offset

The AV sync offset is stored as a custom metadata atom in the MP4 file to enable per-title audio delay compensation.

**FFmpeg metadata key:** `projection:av_sync_offset_ms`

**Type:** Signed integer (milliseconds)

**Range:** −3000 to +3000 milliseconds

**Semantic:**

- **Positive values:** Audio is delayed/late relative to video (needs to be advanced/pulled earlier)
- **Negative values:** Audio is early relative to video (needs to be delayed/pushed later)

**FFmpeg injection:**

```bash
# Store an AV sync offset of +150ms (audio is 150ms late, needs to be advanced)
ffmpeg -i input.mp4 -c copy \
  -metadata "projection:av_sync_offset_ms=150" \
  output.mp4

# Or: audio is 80ms early (needs to be delayed)
ffmpeg -i input.mp4 -c copy \
  -metadata "projection:av_sync_offset_ms=-80" \
  output.mp4
```

**C parser (engine initialization on file load):**

```c
#include <libavformat/avformat.h>

typedef struct {
    int32_t av_sync_offset_samples;  // Converted from milliseconds at 48 kHz
} ProjectionMetadata;

ProjectionMetadata read_projection_metadata(AVFormatContext* fmt_ctx) {
    ProjectionMetadata meta = {0};
    
    AVDictionaryEntry* entry = av_dict_get(
        fmt_ctx->metadata, 
        "projection:av_sync_offset_ms", 
        NULL, 
        AV_DICT_IGNORE_SUFFIX
    );
    
    if (entry) {
        int32_t offset_ms = (int32_t)atoi(entry->value);
        
        // Clamp to ±3000 ms
        if (offset_ms > 3000) offset_ms = 3000;
        if (offset_ms < -3000) offset_ms = -3000;
        
        // Convert ms → samples @ 48 kHz
        // 1 ms = 48 samples (48000 samples/sec ÷ 1000 ms/sec)
        meta.av_sync_offset_samples = offset_ms * 48;
        
        const char* direction = (offset_ms > 0) ? "late (advance audio)" : "early (delay audio)";
        fprintf(stderr, "[PROJECTION] AV sync offset: %d ms = %d samples (audio is %s)\n",
                offset_ms, meta.av_sync_offset_samples, direction);
    }
    
    return meta;
}
```

**Playback integration:**

The offset is applied in the ALSA PCM callback before samples are transmitted to the RME hardware:

```c
// In the LibVLC audio output callback:
snd_pcm_sframes_t alsa_callback(
    snd_pcm_t* handle,
    snd_pcm_uframes_t offset,  // Current write position
    void* opaque
) {
    MediaContext* ctx = (MediaContext*)opaque;
    
    // If AV sync offset is positive (audio late), advance audio by skipping samples
    // If negative (audio early), delay audio by inserting silence/buffering
    if (ctx->av_sync_offset_samples != 0) {
        if (ctx->av_sync_offset_samples > 0) {
            // Audio is late: advance it by skipping samples in the RME output buffer
            snd_pcm_uframes_t skip_count = (snd_pcm_uframes_t)ctx->av_sync_offset_samples;
            snd_pcm_uframes_t skipped = snd_pcm_forward(handle, skip_count);
            fprintf(stderr, "[ALSA] Advanced audio by %lu samples (compensation for late audio)\n", skipped);
        } else {
            // Audio is early: delay it by inserting silence before normal audio output
            snd_pcm_uframes_t silence_count = (snd_pcm_uframes_t)(-ctx->av_sync_offset_samples);
            // (Implementation: pre-fill output buffer with zero samples, then normal audio)
            fprintf(stderr, "[ALSA] Delaying audio by %lu samples (compensation for early audio)\n", silence_count);
        }
        // Clear the offset after first application
        ctx->av_sync_offset_samples = 0;
    }
    
    return snd_pcm_writei(handle, buf, frames);
}
```

**Calibration and adjustment:**

At T−2s pre-buffer (before any media plays), the engine logs the AV sync offset to the system journal for audit:

```text
[2026-07-04T14:32:15.123Z] PROJECTION: Loading file_1234.mp4
[2026-07-04T14:32:15.125Z] PROJECTION: AV sync offset: +150 ms = +7200 samples
[2026-07-04T14:32:15.125Z] PROJECTION: Audio will be delayed 150 ms relative to video
```

Offset changes take effect on the **next file** in the playlist; mid-playback adjustments are queued but not applied until the transition state machine completes.

**Compensation logic summary:**

- Measured audio delay = +150ms (audio is 150ms late)
- Stored offset = +150ms
- Playback compensation = Advance audio by 150ms (pull it earlier to sync with video)
- Result = Audio and video phase-aligned

- Measured audio delay = −80ms (audio is 80ms early, arriving before video)
- Stored offset = −80ms
- Playback compensation = Delay audio by 80ms (push it later to sync with video)
- Result = Audio and video phase-aligned

### 8.6 FFmpeg Assembly Pipeline

```bash
#!/bin/bash
# Cinema Package Multiplexer
# Usage: ./make_package.sh <video> <audio_and_subs> <poster.png> <output.mp4>

VIDEO_SRC=$1
AUDIO_SRC=$2
POSTER_SRC=$3
OUTPUT_PKG=$4

ffmpeg -i "$VIDEO_SRC" -i "$AUDIO_SRC" -i "$POSTER_SRC" \
  -map 0:v:0 \
  -map 1:a \
  -map 1:s? \
  -map 2:v:0 \
  -c copy \
  -disposition:v:1 attached_pic \
  -metadata title="Feature Title" \
  -metadata date="2026" \
  -metadata genre="Cinema Feature" \
  -metadata comment="Atmos Bitstream Mode, 4K Projector Master" \
  -metadata description="Brief plot summary." \
  -metadata synopsis="Full theatrical narrative block." \
  -metadata iTunEXTC="mpaa|R|400|" \
  "$OUTPUT_PKG"
```

> [!tip] Re-encode vs Stream Copy
> All tracks use `-c copy` (stream copy). Re-encoding is never needed at the packaging stage — source masters already carry the correct codec profile. Re-encoding at this stage would corrupt frame-accurate PTS timestamps and degrade bitstream integrity.

---

## 9. Project File Layout

```text
Projection/
├── src/
│   └── sdi_cinema_engine.c  ← Core C engine
├── scripts/
│   └── make_package.sh      ← FFmpeg cinema packaging pipeline
├── systemd/
│   └── sdi_cinema.service   ← systemd unit for auto-start
└── README.md                ← This document
```

---
