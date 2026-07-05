# Projection Media Standard

Status: Current as of 2026-07-05.

Transition telemetry/fail-safe hardening: Proposed complete (code integrated; deployment/runtime validation deferred).

## Overview

This document specifies all supported attributes, metadata, and cue formats for MP4 container files used in the Projection Cinema Engine. Compliance ensures proper operation on Ubuntu Server 22.04 LTS with frame-accurate playback and theater automation integration.

Scope note: this standard defines ingest-facing media requirements (streams, cue signaling, and metadata minimums). Internal projection pipeline details are implementation concerns and are documented separately in runtime/build specifications.

---

## 1. Video Stream Requirements

### Video Codec Parameters

| Attribute                      | Value                                                            | Notes                                                                              |
| ------------------------------ | ---------------------------------------------------------------- | ---------------------------------------------------------------------------------- |
| **Codec**                      | Any decodable video codec (libVLC/FFmpeg path)                   | Ingest policy is codec-flexible; suitability is validated per asset                |
| **Resolution**                 | Any resolution                                                   | Runtime normalizes to projection output path                                       |
| **Bit Depth**                  | 8-bit, 10-bit, or higher source signals                          | Runtime targets deterministic 10-bit projector ingest path                         |
| **Color Primaries**            | Source-signaled (BT.601 / BT.709 / BT.2020)                      | SDR sources are normalized into Rec.2020 target gamut in tolerant policy mode      |
| **Color Matrix**               | Source-signaled (for example SMPTE170M / BT.709 / BT.2020NC)     | Matrix handling follows tolerant ingest policy                                     |
| **Transfer Function**          | Source-signaled SDR/HDR (Linear/Gamma/PQ/HLG)                    | SDR intent is preserved; HDR paths remain passthrough-oriented                     |
| **Frame Rate**                 | Broad input accepted; timing families mapped for control paths   | Master Clock orchestration maps known families to A/B/C timing domains             |
| **Interlacing**                | Progressive preferred                                            | Interlaced sources should be pre-processed upstream for deterministic theater flow |
| **Display Aspect Ratio (DAR)** | Any                                                              | Extracted from SAR + pixel dimensions; triggers lens/masking                       |
| **Pixel Aspect Ratio (PAR)**   | 1:1 preferred                                                    | Non-square sources may require pre-normalization upstream                          |

### Ingest Flexibility Baseline

- Container and encoding profiles are intentionally broad as long as at least one decodable video stream and one decodable audio stream are present.
- Resolution and frame-size are flexible; projection output normalization happens in runtime.
- Color signaling may vary by source; tolerant ingest policy handles mixed SDR/HDR inputs.

### Video Stream Validation

```bash
# Flexible stream-level introspection (do not enforce fixed codec/resolution here)
ffprobe -v error -select_streams v:0 \
  -show_entries stream=codec_name,width,height,r_frame_rate,pix_fmt,color_primaries,color_space,color_transfer \
  -of json title.mp4

# Standards validation (required streams + cue signaling + metadata minimums)
python3 src/tools/validate_media_standard.py title.mp4
```

### Content Tolerance Mitigation Policy

The tolerant mixed-source ingest baseline is consolidated in [README.md Section 1.2](README.md#12-content-tolerance-policy-baseline).

Policy target summary:

- Normalize legacy BT.601 SDR and Rec.709 SDR inputs into Rec.2020 gamut space while preserving SDR transfer intent.
- Keep HDR/HDR10/HDR10+ in passthrough mode (no tone-map rewrite in the engine path).
- Maintain a fixed DeckLink output timing profile to prevent projector resync/flicker.
- Preserve deterministic 10-bit output path expectations for projector ingest.

Implementation status:

- Documented policy baseline: complete.
- Engine flag/runtime integration: complete.
- DeckLink ancillary HDR metadata injection command/helper path: complete.
- On-target packet-level validation: pending Linux runtime validation pass.

---

## 2. Audio Stream Requirements

### Audio Codec Parameters

| Attribute     | Value                                  | Notes                  |
| ------------- | -------------------------------------- | ---------------------- |
| Codec         | PCM, AAC-LC, AC-3, E-AC-3, FLAC, DTS   | See codec policy below |
| Channels      | Up to 16-channel output path           | Fixed engine path      |
| Sample Rate   | 48 kHz                                 | Master clock synced    |
| Bit Depth     | 16-bit, 24-bit, or 32-bit float        | 24-bit+ preferred      |
| Dynamic Range | ≥100 dB SPL for theatrical             | Atmos target profile   |

Codec policy details:

- Selective Dolby passthrough uses `--codec=a52`.
- DTS passthrough is disabled and DTS is decoded locally.
- Engine configures fixed 16-channel ALSA output via `--audio-channels=16`.

Dolby Atmos support remains deployment-valid pending end-to-end runtime validation.

### Required Signaling and Metadata Minimums

The following are minimum ingest requirements independent of internal playback implementation:

- At least one video stream.
- At least one audio stream.
- Cue signaling present via chapter markers.
- Metadata includes `title` and `projection:av_sync_offset_ms`.

Recommended, but optional by default policy:

- Include both `[FEATURE]` and `[CREDITS]` chapter cues.
- Keep at least one 48 kHz audio stream for best alignment with engine target path.

### Audio Sync Metadata

Stored in MP4 metadata dictionary:

```text
projection:av_sync_offset_ms = <signed 32-bit integer>
```

| Field | Type | Range | Unit | Notes |
| ------- | ------ | ------- | ------ | ------- |
| **av_sync_offset_ms** | int32 | ±3000 | milliseconds | Positive = audio late (advancement), Negative = audio early (delay). Converted to samples @ 48 kHz (1 ms = 48 samples) |

### Audio Handling Modes

The Projection Engine supports two audio handling modes in the current implementation.

**Engine Processing Chain**:

```text
MP4 Audio Track → libvlc decode or selective passthrough → ALSA output (16-ch path) → RME HDSPe AES
                                                                                      ↓
                                                                    Dolby CP950A processing chain
```

**Audio Pipeline Flags (LibVLC)**:

- `--audio-channels=16` — Output exactly 16 discrete channels; no upmix/downmix
- `--spdif` — Enables digital passthrough subsystem
- `--codec=a52` — Restricts passthrough to A/52 family streams
- `--no-dts-passthrough` — Forces DTS decode on host instead of passthrough
- `--audio-filter=none` — Bypass all DSP (normalization, EQ, surround panning)

**Current guarantees**:

- ✅ 48 kHz output path with configured 16-channel device
- ✅ Dolby passthrough policy is explicit and deterministic (A/52 only)
- ✅ DTS passthrough is explicitly disabled
- ⚠️ Atmos object-preservation claims require on-target runtime validation

### Audio Stream Validation

```bash
# Check audio attributes
ffprobe -v error -select_streams a:0 -show_entries stream=codec_name,channels,sample_rate,channel_layout -of csv=p=0 title.mp4

# Expected output for 5.1:
# aac,6,48000,5.1(side)

# Expected output for 16-channel discrete:
# pcm_s24le,16,48000,16 channels
```

---

## 3. Macro Cues (Theater Automation Triggers)

Macro cues are read from **MP4 chapter markers** and published as MQTT cue events during playback. Both FFmpeg and VLC natively support chapter extraction.

### Supported Cue Types

| Cue Type | Engine Handling | Trigger | Description |
| ---------- | ---------------- | --------- | ------------- |
| Any chapter title | ✅ Published | At/after chapter timecode | Emitted to `theater/macro_cue` as `cue_type` + `timecode_ms` |
| `[FEATURE]` (recommended) | ✅ Published | Usually near file start | Recommended convention for external automation rules |
| `[CREDITS]` (recommended) | ✅ Published | Usually near file end | Recommended convention for end-of-show automation rules |
| Other custom titles | ✅ Published | User-defined | Treated as opaque cue labels by engine |

### Chapter Format Specification

Chapters are stored in the MP4 `trak`/`chap` box as text tracks. Each chapter entry contains:

```text
Timecode (ms) | Chapter Title | Chapter Description (JSON)
```

### Chapter Title Convention (Recommended)

```text
[FEATURE]   - Feature (house lights dim)
[CREDITS]   - Credits (house lights up)
[INTRO]     - Intro (reserved)
[RECAP]     - Recap (reserved)
```

### Chapter Description (Optional JSON Payload)

```json
{
  "cue_type": "feature",
  "action": "dim_house_lights",
  "target_level_percent": 5,
  "ramp_time_ms": 2000,
  "mqtt_topic": "theater/macro_cue",
  "version": "1.0"
}
```

### Creating Macro Cues with FFmpeg

#### Method 1: From Text File (Recommended)

Create a chapters file `chapters.txt`:

```text
00:00:00.500 Feature
00:19:58.000 Credits
```

Embed into MP4:

```bash
ffmpeg -i source.mp4 -i chapters.txt -map 0 -map_chapters 1 -codec copy -y output.mp4
```

#### Method 2: Direct FFmpeg Metadata

```bash
ffmpeg -i source.mp4 \
  -metadata:c:s title="[FEATURE]" \
  -metadata:c:s "description=00:00:00.500" \
  -codec copy -y output.mp4
```

#### Method 3: MP4Box (ISO Base Media)

```bash
mp4box -add-chapter 0:00:00.500="[FEATURE]" \
       -add-chapter 0:19:58.000="[CREDITS]" \
       source.mp4 -o output.mp4
```

#### Method 4: FFmpeg with JSON Payload

```bash
ffmpeg -i source.mp4 -c copy \
  -metadata:s:c title="[FEATURE]" \
  -metadata:s:c description='{"action":"dim_house_lights","target_level_percent":5}' \
  -y output.mp4
```

### Verifying Chapters with FFprobe

```bash
# List all chapters
ffprobe -v error -show_chapters -of json title.mp4

# Output:
# {
#   "chapters": [
#     {
#       "id": 1,
#       "time_base": "1/1000",
#       "start": 500,
#       "start_time": "0.500000",
#       "end": 1198000,
#       "end_time": "1198.000000",
#       "tags": {
#         "title": "[FEATURE]"
#       }
#     },
#     {
#       "id": 2,
#       "time_base": "1/1000",
#       "start": 1198000,
#       "start_time": "1198.000000",
#       "end": 1200000,
#       "end_time": "1200.000000",
#       "tags": {
#         "title": "[CREDITS]"
#       }
#     }
#   ]
# }
```

### Verifying Chapters in VLC

```bash
# VLC reads chapters natively
vlc title.mp4

# Chapters appear in playback menu
# Right-click → Chapters → [FEATURE] / [CREDITS]
```

---

## 4. Metadata Dictionary (MP4 `meta` Box)

Standard metadata atoms stored in the ISO Base Media `meta` box, readable by FFmpeg and VLC:

| Key | Type | Example | Notes |
| ----- | ------ | --------- | ------- |
| **title** | UTF-8 String | "Dune: Part Two" | Media title |
| **description** | UTF-8 String | "A science fiction epic..." | Long-form description |
| **comment** | UTF-8 String | "DCI 4K Master" | Production comment |
| **copyright** | UTF-8 String | "© 2024 Warner Bros." | Copyright notice |
| **date** | UTF-8 String (ISO 8601) | "2024-02-26" | Release or encoding date |
| **encoder** | UTF-8 String | "FFmpeg 6.0 + cinemacodec" | Encoding software |
| **keywords** | UTF-8 String (comma-separated) | "sci-fi,epic,4k,dci" | Searchable tags |
| **creator** | UTF-8 String | "Christopher Nolan" | Director/creator |
| **publisher** | UTF-8 String | "Warner Bros. Pictures" | Distributor |
| **projection:av_sync_offset_ms** | UTF-8 String (int32) | "150" or "-75" | Custom Projection cue |

### Writing Metadata with FFmpeg

```bash
# Add metadata to existing file
ffmpeg -i source.mp4 -c copy \
  -metadata title="Dune: Part Two" \
  -metadata copyright="© 2024 Warner Bros." \
  -metadata "projection:av_sync_offset_ms=150" \
  -y output.mp4

# Verify
ffprobe -v error -show_format -of flat=s=_ output.mp4 | grep TAG
```

---

## 5. Complete Media Compliance Checklist

Use this checklist when encoding cinema content for Projection Engine compatibility:

### Video

- [ ] At least one decodable video stream is present
- [ ] Source resolution is parseable (runtime output normalization handles scaling)
- [ ] Source codec is supported by deployed libVLC/FFmpeg runtime
- [ ] Frame rate is parseable for playback/timing policy decisions
- [ ] Color signaling (primaries/matrix/transfer) is present when available
- [ ] Display Aspect Ratio present and parseable (scope, flat, custom, or other supported forms)
- [ ] Interlaced sources are pre-processed upstream when deterministic progressive delivery is required
- [ ] Bit depth is declared and compatible with deployed decode path

### Audio

- [ ] Sample Rate: 48 kHz
- [ ] Channels fit 16-channel output path (for example 5.1, 7.1, or 16-channel)
- [ ] Bit Depth: 16-bit, 24-bit, or 32-bit float
- [ ] Codec: PCM, AAC-LC, AC-3, E-AC-3, FLAC, or DTS (decoded locally)
- [ ] AV Sync Offset: Set in metadata if needed (±3000 ms range)

### Cues & Automation

- [ ] Chapter Markers present for any required automation cue points
- [ ] Optional convention: `[FEATURE]` near start and `[CREDITS]` near end
- [ ] Optional Metadata: Copyright, creator, keywords

### Container

- [ ] Format: ISO Base Media (MP4, QuickTime-compatible)
- [ ] Fragmentation: Non-fragmented (fMP4 not recommended for cinema)
- [ ] DRM: None (cleartext only)
- [ ] Atom Order: Recommended `moov` before `mdat` for streaming

### Validation Commands

```bash
# Complete compliance check
ffprobe -v error -show_format -show_streams -show_chapters \
  -of json=c=1 title.mp4 | python3 -m json.tool

# Or concise report
ffprobe -v info -show_entries \
  format=duration,size:stream=width,height,codec_name,sample_rate,channels:chapters=start_time,tags=title \
  title.mp4
```

---

## 6. FFmpeg Encoding Recommendations for Cinema Master

### DCI 4K HDR10 (Recommended for Projection Engine)

```bash
ffmpeg -i source_video.mov -i source_audio.wav \
  -c:v libx265 -preset slow \
  -crf 20 -x265-params aq-mode=3:hdr-opt=1 \
  -pix_fmt yuv420p10le \
  -color_primaries bt2020 -color_trc smpte2084 -colorspace bt2020nc \
  -c:a aac -b:a 256k -ar 48000 \
  -movflags +faststart \
  -metadata title="Title" \
  -metadata copyright="© 2024" \
  -metadata "projection:av_sync_offset_ms=0" \
  -y output_master.mp4
```

### Add Chapters After Encoding

```bash
# Create chapters file
cat > chapters.txt << 'EOF'
00:00:00.500 Feature Start
00:19:58.000 Credits Start
EOF

# Embed chapters
ffmpeg -i output_master.mp4 -i chapters.txt \
  -map 0 -map_chapters 1 -codec copy -y output_final.mp4
```

### Verification

```bash
ffprobe -v error -show_chapters -of json output_final.mp4
ffprobe -v error -select_streams v:0 -show_entries \
  stream=width,height,codec_name,color_space,color_transfer output_final.mp4
```

---

## 7. Network Delivery & Streaming

### Recommended for DAOS/NFS Delivery

```bash
# Non-fragmented MP4 optimized for NFS sequential read
ffmpeg -i source.mov \
  -c:v copy -c:a copy \
  -movflags +faststart \
  -y optimized_for_nfs.mp4

# Verify streaming-friendly structure
mp4box -info optimized_for_nfs.mp4 | head -20
```

### HTTP/HTTPS Progressive Download

```bash
# Same as NFS, but consider adding metadata for HTTP HEAD requests
ffmpeg -i source.mov \
  -c:v copy -c:a copy \
  -movflags +faststart \
  -metadata title="Title" \
  -y http_friendly.mp4
```

---

## 8. Appendix: Libavformat Compliance Reference

The Projection Engine uses **libavformat** (FFmpeg library) for metadata extraction. All formats documented here are guaranteed to parse correctly:

### Supported Read Codecs

- `h264`, `hevc` (video)
- `aac`, `ac3`, `eac3`, `flac`, `pcm_s16le`, `pcm_s24le` (audio)

### Supported Container Formats

- `mov` (MP4, QuickTime-compatible)
- `mp4` (ISO Base Media)
- `ipod` (iPod variant)

### Chapter Extraction

```c
// Libavformat chapter reading
for (unsigned int i = 0; i < fmt_ctx->nb_chapters; i++) {
    AVChapter* ch = fmt_ctx->chapters[i];
    int64_t start_ms = (ch->start * ch->time_base.num) / ch->time_base.den * 1000;
    const char* title = av_dict_get(ch->metadata, "title", NULL, 0)->value;
    printf("Chapter %d: %s @ %lld ms\n", i, title, start_ms);
}
```

---

## 9. Compliance Certification

A media file is **Projection-compliant** when it:

1. ✅ Passes FFprobe validation without warnings
2. ✅ Plays in both FFmpeg and VLC without errors
3. ✅ Any resolution at square pixels (1:1 PAR); engine scales to 4096×2160 display
4. ✅ Frame rate falls within supported timing families (23.976/24/25/29.97/30/50/59.94/60/100/119.88/120)
5. ✅ Audio codec: PCM, AAC-LC, AC-3, E-AC-3, FLAC, or DTS (decoded locally)
6. ✅ Audio: 48 kHz sample rate, ≥5.1 channels (5.1, 7.1, or 16-channel discrete for Dolby Atmos)
7. ✅ Audio policy matches runtime flags (`--audio-filter=none`, selective Dolby passthrough, DTS passthrough disabled)
8. ✅ Includes chapter markers for required external automation timing points
9. ✅ Uses cue titles that match external automation naming rules
10. ✅ Metadata: title, copyright, creator (optional but recommended)
11. ✅ For Dolby workflows: validate on target chain (engine host → RME → CP950A) before certification

### Validation Tooling

Use the validator script to enforce minimum signaling and metadata inclusions while preserving broad codec/container compatibility:

```bash
python3 src/tools/validate_media_standard.py /path/to/title.mp4
```

Optional strict cue mode:

```bash
python3 src/tools/validate_media_standard.py /path/to/title.mp4 --require-feature-credits
```

---

## Change History

| Version | Date | Changes |
| --------- | ------ | --------- |
| 1.0 | 2026-07-04 | Initial standard: Video/Audio specs, Macro Cues (FFmpeg/VLC compatible), Metadata dictionary, Encoding recommendations, Compliance checklist |
| 1.1 | 2026-07-04 | Updated: Flexible video resolution support (any input, scales to 4096×2160), Dolby Atmos PCM passthrough (no remapping), audio codec support (AAC/AC-3/E-AC-3/FLAC/PCM), compliance checklist |
| 1.2 | 2026-07-05 | Aligned to current implementation: selective Dolby passthrough policy, DTS local decode behavior, cue handling semantics, and validation-gated Atmos claims |
| 1.3 | 2026-07-05 | Added content tolerance mitigation policy baseline; runtime flag integration complete, DeckLink ancillary HDR metadata integration pending |
| 1.4 | 2026-07-05 | Refocused standard on ingest stream/signaling requirements, expanded compatibility framing, and added validation script guidance with enforced cue/metadata minimums |
| 1.5 | 2026-07-05 | Broadened video parameter compatibility and replaced fixed-profile validation examples with flexible introspection plus standards validation script |
