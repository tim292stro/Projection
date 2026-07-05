# Hardware Integration & Ubuntu Server Configuration

Status: Current as of 2026-07-05.

Transition telemetry/fail-safe hardening: Proposed complete (code integrated; deployment/runtime validation deferred).

## 1. Ubuntu Server 22.04 LTS Setup

### Initial System Configuration

Boot fresh Ubuntu Server 22.04 LTS with:

- Static IP: `192.168.1.100/24` (control network)
- Secondary network (optional): `192.168.10.100/24` (Dante audio)
- Hostname: `cinema-server`
- No GUI, headless operation

### Network Configuration (netplan)

Edit `/etc/netplan/00-installer-config.yaml`:

```yaml
network:
  version: 2
  ethernets:
    enp3s0:
      dhcp4: false
      addresses: [192.168.1.100/24]
      gateway4: 192.168.1.1
      nameservers:
        addresses: [8.8.8.8, 8.8.4.4]
    enp4s0:
      optional: true
      dhcp4: false
      addresses: [192.168.10.100/24]
```

Apply:

```bash
sudo netplan apply
```

### Network Storage: DAOS (Distributed Asynchronous Object Storage)

The cinema engine can ingest MP4 files from network storage systems including DAOS, NFS, and other distributed filesystems.

#### DAOS NFS Gateway Setup

If your DAOS deployment includes an NFS gateway, mount it on the cinema server:

```bash
# Create mount point
sudo mkdir -p /mnt/daos

# Mount DAOS via NFS (requires NFS gateway to be running)
sudo mount -t nfs -o vers=3,noatime,nodiratime daos-gateway-server:/cinema /mnt/daos

# Verify mount
ls /mnt/daos/

# Make mount permanent (optional)
# Add to /etc/fstab:
# daos-gateway-server:/cinema /mnt/daos nfs vers=3,noatime,nodiratime 0 0

# Then mount with:
sudo mount -a
```

#### Using DAOS-Mounted Media

Once mounted, queue files via TCP:

```bash
# List available files
ls /mnt/daos/DCI_4K/

# Queue file from DAOS mount
echo "ADD_FILE /mnt/daos/DCI_4K/title_20min.mp4" | nc localhost 8080

# Start playback
echo "PLAY_TRACK 0" | nc localhost 8080
```

#### DAOS Performance Tuning

For optimal cinematic playback over DAOS:

```bash
# Mount with optimized NFS options
sudo mount -t nfs -o vers=3,noatime,nodiratime,rsize=65536,wsize=65536,actimeo=0 daos-gateway-server:/cinema /mnt/daos

# Check mount status and latency
nfsstat -m
cat /proc/mounts | grep daos
```

#### Supported Network Filesystems

| Filesystem | Method | Notes |
| ----------- | -------- | ------- |
| DAOS (NFS gateway) | NFS mount | Distributed, fault-tolerant, optimized for parallel I/O |
| DAOS (HTTP gateway) | HTTP URL | URL format: `http://daos-gateway:port/path/to/file.mp4` |
| NFS (standard) | NFS mount | Traditional network filesystem |
| SMB/CIFS | mount -t cifs | Windows shares, Samba servers |
| HTTP/HTTPS | URL in ADD_FILE | `ADD_FILE http://nas:8080/cinema/file.mp4` |
| Local SSD/NVMe | Direct path | `/mnt/cinema/file.mp4` |

**Recommendation:** For frame-accurate cinema playback, **NFS-mounted DAOS on local network** (same subnet as cinema engine) is preferred due to:

- Lower latency (NFS protocol is optimized for sequential I/O)
- Reduced network jitter (local gigabit/multi-gigabit link)
- Full metadata extraction support (libavformat can read mounted files directly)

## 2. Hardware Connectivity Map

### Physical Wiring

```text
┌─────────────────────────────────────────────────────────────┐
│ Brainstorm DXD-16 Master Clock                              │
│ ├─ Tri-Level Sync (BNC) ──> DeckLink 8K Pro G2 Genlock BNC │
│ └─ Word Clock (BNC)      ──> RME HDSPe AES Clock BNC       │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│ Ubuntu Server (PCIe x4 Gen2)                                │
├─ Slot 1: Blackmagic DeckLink 8K Pro G2                      │
│   ├─ SDI Out 1-4 (12G x4) ──> Christie 4K-RGBH Quad SDI  │
│   └─ Genlock (BNC)         ──> DXD-16 Tri-Level Sync      │
│                                                              │
├─ Slot 2: RME HDSPe AES                                      │
│   ├─ DB25 Output (2x Tascam) ──> Custom Cable ──>         │
│   │                            Dolby CP950A (2x RJ45)      │
│   └─ Word Clock (BNC)        ──> DXD-16 Word Clock        │
│                                                              │
└─ Slot 3: (optional) Second storage controller               │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│ Network Infrastructure                                      │
├─ Managed Switch 192.168.1.0/24 (Control)                   │
│  ├─ Server: 192.168.1.100                                   │
│  ├─ MQTT Broker: 192.168.1.50                              │
│  ├─ Christie: 192.168.1.75                                  │
│  └─ DiGiCo: 192.168.1.80 (optional)                        │
│                                                              │
├─ Dante Network 192.168.10.0/24 (Audio, optional)            │
│  ├─ Server Dante Port: 192.168.10.100                      │
│  ├─ BSS BLU-DA: 192.168.10.50                              │
│  └─ DiGiCo: 192.168.10.80                                  │
└─────────────────────────────────────────────────────────────┘
```

### Cabling Reference

**RME Output to CP950A Custom Cable:**

See [README.md Section 2.1](../../README.md#21-signal-flow) for complete pinout.

Quick reference:

- RME DB25 Pin 24 → CP950A RJ45 AES 1-8, Pin 1 (AES1 +)
- RME DB25 Pin 12 → CP950A RJ45 AES 1-8, Pin 2 (AES1 -)
- ...and 6 more pairs for AES 2-4

## 3. Linux Kernel & Driver Setup

### Disable Audio Subsystems

Prevent PulseAudio/PipeWire from interfering:

```bash
sudo systemctl mask pulseaudio.service
sudo systemctl mask pulseaudio.socket
sudo systemctl mask pipewire.service
sudo systemctl mask pipewire.socket
```

### Blackmagic DeckLink Driver (Desktop Video SDK)

Download from [Blackmagic Support](https://www.blackmagicdesign.com/support/):

```bash
# Extract Desktop Video SDK 16.0.1 (latest at time of writing)
cd ~/Downloads
tar -xzf Blackmagic_Desktop_Video_Linux_16.0.1.tar.gz
cd Blackmagic_Desktop_Video_Linux_16.0.1/
sudo ./install.sh

# Verify installation
which DaVinci_Resolve  # Optional (DaVinci symlink)
lspci | grep -i blackmagic  # Should show card
```

**Persist across kernel updates:**

Create `/etc/initramfs-tools/hooks/blackmagic`:

```bash
#!/bin/sh
PREREQ=""
prereqs() { echo "$PREREQ"; }
case $1 in prereqs) prereqs; exit 0 ;; esac

. /usr/share/initramfs-tools/hook-functions

# Copy DeckLink modules
copy_modules_dir drivers/media/pci/blackmagic
```

```bash
sudo chmod +x /etc/initramfs-tools/hooks/blackmagic
sudo update-initramfs -u
```

### RME HDSPe AES ALSA Configuration

The RME driver comes with mainline Linux kernel (alsa-driver). Verify detection:

```bash
aplay -l
# Output should show:
# card 0: RPCIe [RPCIe], device 0: RME RPCIe [RME RPCIe AES]

# Check detailed capabilities
cat /proc/asound/card0/info
cat /proc/asound/card0/stream0
```

**ALSA Settings (non-volatile, stored on RME card):**

Use `hdspmixer` to configure:

```bash
sudo apt-get install alsa-tools alsa-tools-gui

# Run GUI mixer (SSH X-forward or local terminal)
hdspmixer

# Or configure via command-line:
amixer -c 0 sset "Word Clock Sync" "Master"
amixer -c 0 sset "Clock Source" "WordClock"
```

Critical settings:

- **Input**: Word Clock from DXD-16
- **Output**: All 16 channels enabled
- **Sample Rate**: 48 kHz (locked to DXD-16)

### ALSA Channel Remap Matrix (CP950-Compatible Ordering)

To match VLC channel ordering to Dolby CP950 AES expectations, create `/etc/asound.conf`:

```conf
pcm.rme_cinema_map {
  type route
  slave.pcm "hw:0,0"
  slave.channels 16

  # L, R, C
  ttable.0.0 1.0
  ttable.1.1 1.0
  ttable.2.2 1.0

  # LS, RS, LFE (swap LFE to CP950 channel 6)
  ttable.4.3 1.0
  ttable.5.4 1.0
  ttable.3.5 1.0

  # Pass-through channels 7-16
  ttable.6.6 1.0
  ttable.7.7 1.0
  ttable.8.8 1.0
  ttable.9.9 1.0
  ttable.10.10 1.0
  ttable.11.11 1.0
  ttable.12.12 1.0
  ttable.13.13 1.0
  ttable.14.14 1.0
  ttable.15.15 1.0
}
```

Use `PROJECTION_RME_DEVICE=rme_cinema_map` so both ALSA and libVLC target this routed device.

### Boot Order Dependencies

The hardware must be initialized before Ubuntu starts cinema services:

Edit `/etc/systemd/system-sleep/projection-hwprep`:

```bash
#!/bin/bash
case $1 in
  pre)
    # Pause cinema services before sleep
    systemctl stop projection.service
    ;;
  post)
    # Allow hardware to re-lock after wake
    sleep 5
    systemctl start projection.service
    ;;
esac
```

## 4. Control Network Setup

### CP950A Dolby Surround Upmix Auto-Macro

Configure CP950A in its web UI so PCM stereo inputs automatically invoke a Dolby Surround decode macro:

1. Create macro `PCM Stereo Upmix` using your AES digital input bank.
2. Set decode mode to Dolby Surround.
3. Map format sensing so PCM 2.0 triggers `PCM Stereo Upmix`.

Result: AC-3 stays bitstream decoded on CP950A; PCM 2.0 auto-upmix engages without host-side command logic.

### MQTT Broker (Mosquitto)

Install:

```bash
sudo apt-get install mosquitto mosquitto-clients

# Configure /etc/mosquitto/mosquitto.conf:
# listener 1883
# max_connections -1
# allow_anonymous true
```

Start:

```bash
sudo systemctl enable mosquitto
sudo systemctl start mosquitto

# Verify
mosquitto_sub -h 192.168.1.50 -t "#" &
# (In another terminal) mosquitto_pub -h 192.168.1.50 -t "test" -m "hello"
```

### Christie Projector TCP Connection

Verify network reachability:

```bash
ping 192.168.1.75
telnet 192.168.1.75 3002

# Once connected, type:
# (SHU ?)
# (PWR ?)
# (LPLV ?)

# Type Ctrl+] then quit to exit telnet
```

## 5. Cinema Engine Deployment

### Directory Structure

```bash
# Create deployment folders
sudo mkdir -p /opt/projection /mnt/cinema_media /var/log/projection

# Copy engine and config
sudo cp src/sdi_cinema_engine /opt/projection/
sudo cp src/modules/*.h /opt/projection/modules/

# Create media library symlink
sudo ln -s /mnt/cinema_media /opt/projection/media

# Set permissions
sudo chown -R projection:projection /opt/projection /mnt/cinema_media /var/log/projection
```

### Systemd Service Unit

Create `/etc/systemd/system/projection.service`:

```ini
[Unit]
Description=Projection Cinema Media Server
After=network-online.target mosquitto.service
Wants=network-online.target
Requires=mosquitto.service

[Service]
Type=simple
User=projection
Group=projection
WorkingDirectory=/opt/projection

# Pre-flight health check: Wait for hardware to lock
ExecStartPre=/bin/bash -c 'sleep 5; aplay -l | grep -q RPCIe || (echo "RME not detected"; exit 1)'
ExecStartPre=/bin/bash -c 'lspci | grep -q Blackmagic || (echo "DeckLink not detected"; exit 1)'

# Start engine in daemon mode (no initial playlist)
# Engine will await playlist commands over TCP port 8080
ExecStart=/usr/bin/chrt -f 50 /opt/projection/sdi_cinema_engine

# Alternatively, start with bootstrap playlist:
# ExecStart=/usr/bin/chrt -f 50 /opt/projection/sdi_cinema_engine /mnt/cinema_media/file1.mp4 /mnt/cinema_media/file2.mp4

# High restart aggressiveness for mission-critical operation
Restart=on-failure
RestartSec=3
StartLimitInterval=300
StartLimitBurst=5

# Logging
StandardOutput=journal
StandardError=journal
SyslogIdentifier=projection

# Resource limits
MemoryLimit=2G
CPUQuota=80%

[Install]
WantedBy=multi-user.target
```

Enable:

```bash
sudo systemctl daemon-reload
sudo systemctl enable projection.service
sudo systemctl start projection.service
```

### Network Playlist Management

The cinema engine listens on **TCP port 8080** for playlist management and transport control. All commands are ASCII strings followed by `\r\n`.

#### Playlist Commands

| Command | Example | Response | Notes |
| --------- | --------- | ---------- | ------- |
| **ADD_FILE** | `ADD_FILE /mnt/media/title.mp4` | `STATUS_FILE_ADDED\|index=5` | Append file to queue |
| **LIST_PLAYLIST** | `LIST_PLAYLIST` | See below | Returns all queued files |
| **PLAY_TRACK** | `PLAY_TRACK 3` | `STATUS_PLAYING_TRACK\|index=3` | Jump to file at index |
| **REMOVE_FILE** | `REMOVE_FILE 2` | `STATUS_FILE_REMOVED` | Delete file from queue |
| **CLEAR_PLAYLIST** | `CLEAR_PLAYLIST` | `STATUS_PLAYLIST_CLEARED` | Empty entire queue |

#### Transport Commands

| Command | Response | Notes |
| --------- | ---------- | ------- |
| **PLAY** | `STATUS_OK` | Resume playback |
| **PAUSE** | `STATUS_OK` | Pause current file |
| **STOP** | `STATUS_OK` | Stop playback (keep file loaded) |
| **NEXT** | `STATUS_HANDOVER_FORCED` | Jump to next file |
| **PREVIOUS** | `STATUS_PREVIOUS_TRACK` | Jump to previous file |

#### Example Usage

**Add files to queue:**

```bash
echo "ADD_FILE /mnt/media/movie_1.mp4" | nc localhost 8080
echo "ADD_FILE /mnt/media/movie_2.mp4" | nc localhost 8080
echo "ADD_FILE /mnt/media/movie_3.mp4" | nc localhost 8080
```

**List all files:**

```bash
echo "LIST_PLAYLIST" | nc localhost 8080
```

Response format:

```text
PLAYLIST_INFO|count=3|current_index=-1
0|/mnt/media/movie_1.mp4
1|/mnt/media/movie_2.mp4
2|/mnt/media/movie_3.mp4
```

**Play first file:**

```bash
echo "PLAY_TRACK 0" | nc localhost 8080
```

**Transport controls:**

```bash
echo "PLAY" | nc localhost 8080       # Start playback
echo "PAUSE" | nc localhost 8080      # Pause
echo "NEXT" | nc localhost 8080       # Next file
echo "PREVIOUS" | nc localhost 8080   # Previous file
```

#### Playlist Format (Optional Bootstrap Mode)

You can optionally pass files as command-line arguments to load them at startup:

```bash
/opt/projection/sdi_cinema_engine /mnt/cinema_media/file1.mp4 /mnt/cinema_media/file2.mp4
```

Or via systemd ExecStart:

```ini
ExecStart=/usr/bin/chrt -f 50 /opt/projection/sdi_cinema_engine /mnt/cinema_media/file1.mp4
```

However, **daemon mode** (no arguments) is recommended for production:

```bash
/opt/projection/sdi_cinema_engine  # Starts with empty playlist, awaits TCP commands
```

## 6. Media Asset Encoding

### MP4 with Projection Metadata

Encode with AV sync offset example (assume 150ms audio delay measured):

```bash
ffmpeg -i input_video.mov \
  -i input_audio.wav \
  -c:v libx264 -crf 18 -preset slow \
  -c:a aac -b:a 320k \
  -metadata title="Cinema Title" \
  -metadata "projection:av_sync_offset_ms=-150" \
  -attach poster.png -metadata:s:v disposition=attached_pic \
  output.mp4
```

**Metadata atoms added:**

- `title`: Display name for marquee
- `projection:av_sync_offset_ms`: AV sync calibration (-150ms = advance audio by 150ms)
- Attached image: Poster art for lobby display

Verify metadata:

```bash
ffprobe output.mp4 -show_format
```

### Macro Cues: Theater Automation Triggers

Macro cues synchronize theater automation (house lights, marquee updates) with video playback using MP4 chapter markers.

#### Creating Macro Cues

#### Step 1: Define timecodes (text format)

Create a chapters file with timecodes for Feature and Credits cues:

```text
00:00:00.500 [FEATURE]
00:19:58.000 [CREDITS]
```

- `[FEATURE]`: ~100-500ms after file start (dims house lights, initiates projector ramp)
- `[CREDITS]`: ~30 seconds before file end (raises house lights, starts end-of-show)

#### Step 2: Embed chapters into MP4

```bash
ffmpeg -i source.mp4 -i chapters.txt \
  -map 0 -map_chapters 1 \
  -codec copy \
  -y output_with_chapters.mp4
```

#### Step 3: Verify chapters with FFprobe

```bash
ffprobe -v error -show_chapters -of json output_with_chapters.mp4

# Output:
# {
#   "chapters": [
#     {
#       "id": 1,
#       "start": 500,
#       "start_time": "0.500000",
#       "tags": { "title": "[FEATURE]" }
#     },
#     {
#       "id": 2,
#       "start": 1198000,
#       "start_time": "1198.000000",
#       "tags": { "title": "[CREDITS]" }
#     }
#   ]
# }
```

#### Complete Encoding Pipeline

Encode DCI 4K with AV sync offset, metadata, and macro cues:

```bash
#!/bin/bash
# Cinema Master Package Builder

VIDEO_SRC="$1"           # source.mov
AUDIO_SRC="$2"           # audio.wav
POSTER="$3"              # poster.png
CHAPTERS_FILE="$4"       # chapters.txt
AV_SYNC_MS="$5"          # -150 (or 0 for none)
OUTPUT="$6"              # output.mp4

# Encode video + audio + attach poster
ffmpeg -i "$VIDEO_SRC" \
       -i "$AUDIO_SRC" \
       -i "$POSTER" \
  -map 0:v:0 \
  -map 1:a:0 \
  -map 2 \
  -c:v libx264 -crf 18 -preset slow \
  -c:a aac -b:a 320k \
  -disposition:v:2 attached_pic \
  -metadata title="Title Here" \
  -metadata copyright="© 2024 Studio" \
  -metadata "projection:av_sync_offset_ms=$AV_SYNC_MS" \
  -c:s copy \
  temp_no_chapters.mp4

# Add chapter markers
ffmpeg -i temp_no_chapters.mp4 \
       -i "$CHAPTERS_FILE" \
  -map 0 -map_chapters 1 \
  -codec copy \
  -y "$OUTPUT"

# Verify
ffprobe -v error -show_chapters "$OUTPUT"
ffprobe -v error -show_format "$OUTPUT" | grep projection

rm temp_no_chapters.mp4
```

Usage:

```bash
./build_cinema_package.sh \
  dune_video.mov \
  dune_audio.wav \
  poster.png \
  chapters.txt \
  "-150" \
  dune_master.mp4
```

---

## 7. AV Sync Calibration

### Measurement Setup

1. Prepare a test tone sweep: 1 Hz to 20 kHz, 5 seconds, at -20dBFS
2. Encode as 16-ch discrete LPCM (all channels identical)
3. Add MP4 metadata with `projection:av_sync_offset_ms=0` (no offset)

### Calibration Procedure

1. **Route video & audio to test equipment:**
   - SDI output → Oscilloscope (video sync trigger)
   - AES output → Audio Interface + Analyzer

2. **Play test file:**

   ```bash
   /opt/projection/sdi_cinema_engine /mnt/cinema_media/test_sync.mp4
   ```

3. **Measure delay:**
   - Trigger oscilloscope on SDI Tri-Level Sync (falling edge of 0H line)
   - Measure audio onset in audio analyzer relative to sync edge
   - Example: If audio starts 150ms after sync → audio is 150ms late

4. **Re-encode with offset:**
   - Calculate offset: If audio is 150ms late, store `-150` (negative = advance audio)
   - Re-encode media with updated metadata
   - Re-test and iterate until ±1 sample (±20µs) alignment

5. **Deploy calibrated file:**

   ```bash
   cp /path/to/calibrated_media.mp4 /mnt/cinema_media/
   ```

### Production Workflow

- Calibrate once per auditorium
- Store calibration offset in library management system
- Apply via FFmpeg during ingest pipeline
- Re-calibrate annually or after equipment changes

## 8. Monitoring & Maintenance

### Health Check Script

Create `/opt/projection/health_check.sh`:

```bash
#!/bin/bash
echo "=== Projection Engine Health Check ==="
echo ""
echo "[Hardware]"
lspci | grep -E "Blackmagic|RME" || echo "WARNING: Hardware not detected"
aplay -l | grep RPCIe && echo "✓ RME Audio" || echo "✗ RME not found"
echo ""
echo "[Network]"
ping -c 1 -W 2 192.168.1.50 && echo "✓ MQTT Broker" || echo "✗ MQTT unreachable"
ping -c 1 -W 2 192.168.1.75 && echo "✓ Christie" || echo "✗ Christie unreachable"
echo ""
echo "[Clock Synchronization]"
cat /proc/asound/card0/state | grep -i "clock\|rate" | head -3
echo ""
echo "[Services]"
systemctl status projection.service --no-pager | head -5
```

```bash
sudo chmod +x /opt/projection/health_check.sh
./health_check.sh
```

### Log Monitoring

```bash
# Real-time logs
sudo journalctl -u projection.service -f

# Last 50 lines
sudo journalctl -u projection.service -n 50
```

### Performance Metrics

```bash
# CPU & memory
ps aux | grep sdi_cinema_engine

# ALSA buffer statistics
cat /proc/asound/card0/stream0

# Network traffic (if using Dante)
iftop -i enp4s0 -n  # Requires iftop package
```

## Next Steps

1. **Install hardware** per wiring diagram above
2. **Load Linux drivers** (DeckLink, RME)
3. **Verify connectivity** to MQTT and Christie via `ping` and `telnet`
4. **Build cinema engine** following [BUILD.md](BUILD.md)
5. **Calibrate AV sync** following procedure in Section 7
6. **Deploy test content** and verify SDI video + AES audio output
7. **Enable systemd service** and configure for automatic startup
