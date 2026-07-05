# Blackmagic Design DeckLink 8K Pro G2 — Linux Integration

> [!abstract] Source
> Blackmagic Design Desktop Video SDK (v16.0, released 08 Apr 2026). SDK Manual PDF: <https://documents.blackmagicdesign.com/UserManuals/DeckLinkSDKManual.pdf> (requires developer registration). See [[GitHub Reference Index]] for full provenance.

---

## Hardware Overview

| Spec | Value |
| --- | --- |
| Form factor | Full-length PCIe |
| Interface | PCIe x4 (Gen 2) |
| Video outputs | Dual/Quad 12G-SDI (BNC) |
| Max resolution | 8K (7680×4320) |
| Target output for this project | DCI 4K (4096×2160) via Quad 12G-SDI |
| Genlock input | BNC (Tri-Level Sync / Black Burst) |
| Reference input | Locks to Brainstorm DXD-16 Tri-Level Sync |

---

## Linux Driver Installation

### Desktop Video Package

```bash
# Download Desktop Video 16.0.1 for Linux from:
# https://www.blackmagicdesign.com/developer/products/capture-and-playback/sdk-and-software
# (Requires free developer account)

# Install the .deb package
sudo dpkg -i desktopvideo_16.0.1_amd64.deb
sudo apt-get install -f   # Fix dependencies if needed

# Load the kernel module
sudo modprobe blackmagic

# Verify card is detected
BlackmagicFirmwareUpdater status
```

### Verify ALSA-independent routing

```bash
# The DeckLink driver does NOT use ALSA — it uses its own kernel module.
# Verify with:
lsmod | grep blackmagic

# List detected DeckLink devices:
BlackmagicFirmwareUpdater status
```

---

## Quad-Link 2SI Enforcement (Current Engine State)

The runtime now enforces Quad-Link 2SI at startup through a helper command hook.

Engine behavior:

- Startup executes `decklink_2si_enforcer` via `DECKLINK_2SI_ENFORCER_CMD`
- Failures are non-fatal (engine logs warning and continues)

Relevant environment variables:

```bash
export DECKLINK_2SI_ENFORCER_CMD="/usr/local/bin/decklink_2si_enforcer"
# export DECKLINK_SKIP_2SI_ENFORCE="1"   # Optional: disable startup enforcement
```

Helper build path in this project:

```bash
cd src/
make decklink-enforcer \
    DECKLINK_SDK_INCLUDE=/opt/blackmagic/DesktopVideo/include \
    DECKLINK_SDK_LIB=/opt/blackmagic/DesktopVideo/lib
sudo make install-decklink-enforcer
```

Source file:

- `src/tools/decklink_2si_enforcer.cpp`

---

## LibVLC Integration

### Current Engine Flags

```c
const char* vlc_video_args[] = {
    "--vout=decklink",           // Route video output to DeckLink hardware
    "--decklink-ten-bits",       // Force 10-bit SDI output path
    "--force-aspect-ratio=17:9", // Force full 17:9 canvas mapping
    "--zoom=1.0",
    "--no-autoscale",
};
```

### Runtime Behavior Notes

- The current engine does **not** pass `--decklink-card` or explicit `--decklink-mode=*` arguments.
- Timing-domain switching is handled through DXD-16 preset orchestration (A/B/C families), not per-playback LibVLC mode strings.
- Quad-Link 2SI is enforced before LibVLC init via the DeckLink SDK helper command path.

> [!warning] Frame-Rate Family Must Match DXD Preset
> Source content frame-rate family must match the active DXD-16 domain (Fractional / Integer / PAL). A mismatch can force downstream resync and visible transition artifacts.

### Per-Player Aspect Ratio Lock

```c
// Enforce DCI 4K geometry on every player instance
libvlc_video_set_aspect_ratio(player, "256:135");
```

---

## Genlock / Reference Sync

The DeckLink 8K Pro G2 must be genlock-locked to the Brainstorm DXD-16 before the first frame plays:

1. Connect DXD-16 **Tri-Level Sync output** → DeckLink **Genlock BNC input**
2. In the Blackmagic Desktop Video Setup utility, set **Reference Input** to `External`
3. Verify lock indicator shows green before launching the engine

> [!danger] Unlocked Playback
> If the DeckLink is not locked to the DXD-16 reference when `libvlc_media_player_play()` is called, the Christie projector will not see a stable SDI signal and the shutter sequence will fail. Always verify hardware lock in the boot health check.

---

## Kernel Module Persistence (Boot)

Add to `/etc/modules-load.d/blackmagic.conf`:

```text
blackmagic
blackmagic-io
```

This ensures the DeckLink driver loads before the cinema engine's systemd service starts.

---

## Known Issues / Notes

- The DeckLink driver and the `pulseaudio`/`pipewire` daemons can conflict on some Ubuntu builds. Since this is a headless server with no desktop audio, both should be disabled (see [[Ubuntu Server Boot Configuration]]).
- After a kernel update, the Blackmagic Desktop Video package must be reinstalled — the driver is an out-of-tree module and does not persist through kernel version changes automatically.
- Use `BlackmagicFirmwareUpdater` to verify card firmware version matches the installed driver version.
- If helper build fails, confirm `DeckLinkAPI.h` include path and `libDeckLinkAPI` library path from your Desktop Video SDK install.

---

*See also: [[GitHub Reference Index]], [[RME HDSPe AES]], [[Brainstorm DXD-16]], [[Projection Spec]]*
