# Projection Cinema Engine — Source Code

Status: Current as of 2026-07-05.

Transition telemetry/fail-safe hardening: Proposed complete (code integrated; deployment/runtime validation deferred).

A frame-accurate, headless cinema media server for Ubuntu Server 22.04 LTS. Outputs DCI 4K video over 12G-SDI and 16-channel audio over AES digital with sample-granular AV sync offset compensation.

## Architecture

```text
┌─────────────────┐
│  sdi_cinema_    │  Main playback engine
│  engine.c       │  - Dual-engine ping-pong architecture
│ (500+ lines)    │  - TCP remote control (port 8080)
└────────┬────────┘
         │
         ├─ libvlc          (video/audio demux & output)
         ├─ libavformat     (metadata extraction)
         ├─ ALSA            (RME audio I/O)
         └─ modules/*       (hardware control)
```

## Module Breakdown

| Module | Purpose | Features |
| -------- | --------- | ---------- |
| **metadata_extractor** | MP4 metadata reading | AV sync offset extraction, poster images |
| **alsa_context** | RME audio interface | 16-ch output, AV sync offset application |
| **mqtt_client** | Dolby CP950A control | Mute/unmute during transitions |
| **christie_control** | Projector TCP commands | Shutter, laser power, queries |
| **playlist_manager** | File queue management | Sequence tracking, pre-buffering hints |

## Build & Run

### Quick Start

```bash
# Install dependencies
make install-deps

# Build
make

# Run with test media
./sdi_cinema_engine /path/to/movie.mp4

# Control via TCP (in another terminal)
echo "PLAY" | nc localhost 8080
```

### Full Documentation

- [BUILD.md](BUILD.md) — Compilation, dependencies, systemd service
- [INTEGRATION.md](INTEGRATION.md) — Hardware wiring, Ubuntu config, AV sync calibration

## Key Features

✅ **Frame-Accurate Playback**

- RME Word Clock synchronized to Brainstorm DXD-16 master
- LibVLC clock-synchro=0 (software correction disabled; hardware timeline leads)
- Zero tolerance for dropped frames (--clock-jitter=0)

✅ **AV Sync Offset**

- Sample-granular precision (20.8 µs @ 48 kHz)
- Range: ±3000 milliseconds (±144,000 samples)
- Per-file calibration via MP4 metadata atoms
- Automatic compensation during playback

✅ **Gapless Transitions**

- Dual LibVLC player instances (A/B handover)
- 30-second pre-buffering look-ahead
- Mechanical hardware settling window (2 seconds)

✅ **Daemon Mode + Network Playlist**

- Starts with empty queue, awaits TCP commands
- Dynamic playlist: add/remove/clear files over network
- Bootstrap mode: Optional initial files on command line
- Systemd service runs unattended at boot

✅ **Theater Automation**

- Christie projector TCP control (shutter, laser power)
- Dolby CP950A mute/unmute via MQTT
- Lens sled & masking curtain commands via MQTT
- Binary timed metadata cues from MP4 containers

✅ **Remote Control (TCP Port 8080)**

- Playlist management: add, list, remove, clear files
- Transport controls: play, pause, stop, next, previous
- Track selection: jump to any queued file
- Works from any network client (CLI, GUI, web dashboard)

## File Layout

```text
src/
├── Makefile                      # Build automation
├── sdi_cinema_engine.c          # Main engine (750 lines)
│
├── BUILD.md                     # Compilation guide
├── INTEGRATION.md               # Hardware setup guide
├── README.md                    # This file
│
└── modules/
    ├── metadata_extractor.c/h   # MP4 metadata + AV sync offset reading
    ├── alsa_context.c/h         # RME ALSA device wrapper
    ├── mqtt_client.c/h          # MQTT publish (CP950A control)
    ├── christie_control.c/h     # Christie TCP protocol
    └── playlist_manager.c/h     # Playlist + pre-buffer management
```

## Hardware Requirements

### Mandatory

- **Video Output**: Blackmagic DeckLink 8K Pro G2 (PCIe x4 Gen2)
  - Quad 12G-SDI outputs
  - Genlock BNC input (DXD-16 sync)

- **Audio Output**: RME HDSPe AES (PCIe)
  - 16-channel AES/EBU on dual DB25 Tascam connectors
  - Word Clock BNC input (DXD-16 sync)

- **Master Clock**: Brainstorm Electronics DXD-16
  - Tri-Level Sync output (→ DeckLink genlock)
  - Word Clock output (→ RME clock input)

### Control Network

- **MQTT Broker** @ 192.168.1.50:1883 (e.g., Mosquitto)
- **Christie Projector** @ 192.168.1.75:3002 (TCP)
- **Dolby CP950A** (controlled via MQTT, audio input)
- **BSS BLU-DA** (Dolby Atmos Connect bridge)
- **DiGiCo Quantum-7t** (Dante subscriber, passive)

See [INTEGRATION.md](INTEGRATION.md) for complete wiring diagram.

## Configuration

### Environment Variables

```bash
export PROJECTION_RME_DEVICE="rme_cinema_map" # ALSA routed device (recommended)
export PROJECTION_MQTT_BROKER="192.168.1.50"  # MQTT server IP
export PROJECTION_CHRISTIE_IP="192.168.1.75"  # Projector IP
```

### MP4 Metadata Encoding

```bash
# Encode media with AV sync offset (example: audio 150ms late)
ffmpeg -i input.mov \
  -c:v libx264 -crf 18 \
  -c:a aac -b:a 320k \
  -metadata "projection:av_sync_offset_ms=-150" \
  -metadata title="Movie Title" \
  output.mp4
```

The engine reads `projection:av_sync_offset_ms` and applies compensation during playback.

## Playback Control

### TCP Remote Commands (Port 8080)

**Playlist Management:**

```text
ADD_FILE <path>       → Add file to end of queue
LIST_PLAYLIST         → Show all queued files (with indices)
PLAY_TRACK <index>    → Jump to file at index and start playback
REMOVE_FILE <index>   → Delete file from queue
CLEAR_PLAYLIST        → Empty entire queue
```

**Transport Controls:**

```text
PLAY                  → Resume playback
PAUSE                 → Pause current file
STOP                  → Stop playback (file remains loaded)
NEXT                  → Skip to next file (gapless handover)
PREVIOUS              → Skip to previous file
```

Example:

```bash
# Queue files
echo "ADD_FILE /mnt/media/movie1.mp4" | nc localhost 8080
echo "ADD_FILE /mnt/media/movie2.mp4" | nc localhost 8080

# List queue
echo "LIST_PLAYLIST" | nc localhost 8080
# Response: PLAYLIST_INFO|count=2|current_index=-1
#           0|/mnt/media/movie1.mp4
#           1|/mnt/media/movie2.mp4

# Start playback from index 0
echo "PLAY_TRACK 0" | nc localhost 8080
# Response: STATUS_PLAYING_TRACK|index=0

# Control playback
echo "PAUSE" | nc localhost 8080
echo "PLAY" | nc localhost 8080
echo "NEXT" | nc localhost 8080
```

### Transition State Machine

During `NEXT`, `PREVIOUS`, or `PLAY_TRACK`:

1. Close projector shutter (Christie TCP)
2. Mute Dolby CP950A (MQTT)
3. Read next file metadata (libavformat)
4. Calculate laser power (CIH formula)
5. Set projector laser power (Christie TCP)
6. MQTT lens sled & masking commands (optional)
7. Wait 50ms for mechanical settle
8. Open shutter + unmute CP950A
9. Apply AV sync offset to ALSA
10. Resume playback (LibVLC play)

## AV Sync Offset Calibration

### Measurement

1. Play test tone sweep (1 Hz–20 kHz) on all AES channels
2. Trigger oscilloscope on SDI sync edge
3. Measure audio onset time in analyzer
4. Calculate: `offset_ms = measured_delay_ms`

### Re-encoding

```bash
# If audio arrives 150ms late:
ffmpeg -i original.mp4 \
  -c copy \
  -metadata "projection:av_sync_offset_ms=-150" \
  calibrated.mp4

# Negative offset advances audio during playback
```

### Verification

```bash
ffprobe calibrated.mp4 -show_format | grep projection
```

Expected output:

```text
projection:av_sync_offset_ms=-150
```

See [INTEGRATION.md Section 7](INTEGRATION.md#7-av-sync-calibration) for detailed procedure.

## Logging & Diagnostics

### Real-Time Logs

```bash
# While engine running
journalctl -u projection.service -f
```

### Engine Output Example

```text
[ENGINE] LibVLC initialized
[PLAYLIST] Created playlist with 3 tracks
[ALSA] Initialized: device=rme_cinema_map, channels=16, rate=48000 Hz
[MQTT] Initialized: broker=192.168.1.50:1883, client_id=projection_engine_12345
[CHRISTIE] Connected to 192.168.1.75:3002
[ENGINE] Initialization complete
[TCP] Control server listening on port 8080

[ENGINE] Creating media player for: /mnt/cinema_media/title_1.mp4
[METADATA] AV sync offset: -150 ms = -7200 samples (audio is late)
[ALSA] AV sync offset set: -7200 samples (audio is late)
[ENGINE] Playback started

[TRANSITION] Beginning state machine...
[CHRISTIE] Shutter CLOSED
[MQTT] Mute command sent to CP950A
[METADATA] Loaded: title='Cinema Title', av_offset=-7200 samples
...
```

## Dependencies

**Ubuntu 22.04 LTS Package**:

```bash
sudo apt-get install libvlc-dev libavformat-dev libavutil-dev libasound2-dev build-essential
```

**Runtime Requirements**:

- libvlc.so.5 (VLC media framework)
- libavformat.so.60 (FFmpeg container demux)
- libasound.so.2 (ALSA audio)
- libpthread.so.0 (POSIX threads)

## Known Limitations

- **No hardware present on this dev host**: full projector/audio/control validation requires target Ubuntu deployment hardware
- **DeckLink output**: Requires Blackmagic Desktop Video SDK 16.0.1 installed on deployment machine
- **Clock synchronization**: Depends on external Brainstorm DXD-16 master clock (not simulated)
- **AES/EBU wiring**: Custom DB25 → 2× RJ45 cable required (RME output to CP950A input)
- **MQTT transport implementation**: Uses Paho MQTT C transport when available at build-time (`paho-mqtt3c`), with documented fallback non-broker mode when unavailable

## Future Enhancements

- Deferred backlog: broker-auth hardening and reconnect policy tuning for Paho MQTT C transport
- Deferred backlog: Dante clock input support (optional alternative to DXD-16)
- Deferred backlog: Per-frame metadata cue injection (automation scripting)
- Deferred backlog: Web UI dashboard (systemd user service + nginx)
- Deferred backlog: Multi-instance support (multiple auditoriums)
- Deferred backlog: Compressed bitstream passthrough (DTS-HD, TrueHD, Atmos)

## Testing

Without hardware, validate compilation and basic functionality:

```bash
# Build in debug mode
CFLAGS="-g -O0 -DDEBUG" make clean && make

# Run with test file
./sdi_cinema_engine /path/to/test.mp4

# Monitor in another terminal
ps aux | grep sdi_cinema_engine
lsof -p $(pgrep sdi_cinema_engine)

# Send TCP commands
echo "PLAY" | nc localhost 8080
echo "PAUSE" | nc localhost 8080
echo "NEXT" | nc localhost 8080
```

## Support & References

- [Blackmagic DeckLink Linux Driver](https://www.blackmagicdesign.com/support/)
- [LibVLC Documentation](https://www.videolan.org/developers/vlc/)
- [FFmpeg libavformat](https://ffmpeg.org/libavformat.html)
- [RME HDSPe AES Manual](https://www.rme-audio.de/en/products/rme/hdspe_aes)
- [ALSA Project](https://www.alsa-project.org/)
- [Dolby CP950A Specs](../../Documents/Hardware%20Reference/Dolby%20CP950A.md)
- [Christie Sapphire Manual](../../Documents/Hardware%20Reference/Christie%20Sapphire%20TruLife%2B%20Commands.md)

## License

Open source cinema automation project. See parent README for licensing details.

---

**Last Updated**: 2026-07-04  
**Status**: Ready for integration testing  
**Maintainer**: Projection Team
