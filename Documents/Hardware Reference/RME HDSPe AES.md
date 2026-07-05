# RME HDSPe AES — ALSA Configuration

> [!abstract] Source
> RME HDSPe AES product archive page: <https://archiv.rme-audio.de/en/products/hdspe_aes.php>. Manual PDF: <https://archiv.rme-audio.de/download/hdspeaes_e.pdf>. See [[GitHub Reference Index]] for full provenance.

---

## Hardware Overview

| Spec | Value |
| --- | --- |
| Form factor | Short PCIe x1 |
| I/O | 8× AES/EBU in (16 ch), 8× AES/EBU out (16 ch) |
| Max sample rate | 192 kHz |
| Word Clock I/O | BNC (Single/Double/Quad Speed auto-detect) |
| MIDI | 2× MIDI I/O (4× 5-pin DIN via breakout) |
| Sync sources | 8× AES input, Word Clock, Internal |
| Settings storage | **Non-volatile on-card** — survives power cycle |

> [!tip] Boot Behavior
> The card stores its last-used sample rate, master/slave configuration, and AES format on the card itself. On system boot, it activates these settings **immediately** — before the OS driver even loads. This eliminates startup noise and prevents clock network instability during cold boot of the cinema server.

---

## ALSA Device Addressing

```bash
# List all ALSA capture/playback devices
aplay -l

# Expected output for HDSPe AES (example):
# card 0: HDSPe [RME HDSPe AES], device 0: HDSPe AES [HDSPe AES]

# Direct hardware address used in LibVLC:
hw:0,0   # card=0, device=0
# or using the card name:
hw:HDSPe,0
```

> [!warning] Verify on Target Hardware
> The card index (`0`) may differ if other audio devices are present. Always run `aplay -l` on the deployed server and update `HARDWARE_RME_TARGET` accordingly. Do not assume `hw:0,0` is correct without verification.

---

## LibVLC Configuration

```c
#define HARDWARE_RME_TARGET "rme_cinema_map"   // Preferred routed alias

const char* vlc_audio_args[] = {
    "--aout=alsa",                               // Use ALSA output (bypasses PulseAudio/PipeWire)
    "--alsa-audio-device=" HARDWARE_RME_TARGET,  // Direct to RME card
    "--audio-channels=16",                       // Open all 16 output channels
    "--spdif",                                   // Enable Dolby bitstream passthrough
    "--rematrix=0",                              // Disable internal channel remixing
    "--audio-filter=none",                       // Bypass all DSP/normalization
};
```

Runtime fallback behavior in engine:

- Preferred device: `rme_cinema_map`
- Fallback device: `hw:0,0` when routed alias is unavailable

Environment override:

```bash
export PROJECTION_RME_DEVICE="rme_cinema_map"
```

### Required ALSA Route Alias

The runtime expects an ALSA route alias that remaps VLC channel ordering to CP950 expected channel positions.

Create `/etc/asound.conf` on deployment host:

```conf
pcm.rme_cinema_map {
    type route
    slave.pcm "hw:0,0"
    slave.channels 16

    ttable.0.0 1.0
    ttable.1.1 1.0
    ttable.2.2 1.0
    ttable.4.3 1.0
    ttable.5.4 1.0
    ttable.3.5 1.0

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

---

## Clock Synchronization

### Hardware Lock (Word Clock)

1. Connect Brainstorm DXD-16 **Word Clock output** → RME HDSPe AES **Word Clock BNC input**
2. In RME's TotalMix FX or settings dialog, set **Sync Source** to `Word Clock`
3. Set **Sample Rate** to match the asset sample rate (48 kHz for DCI cinema)
4. Verify the **SyncCheck** indicator shows `Lock` on the Word Clock input

### Clock Domain Rule

The RME card and DeckLink card **must** share the same clock domain rooted at the DXD-16:

```text
DXD-16 ──┬── Tri-Level Sync → DeckLink Genlock BNC
          └── Word Clock     → RME Word Clock BNC
```

Both cards must show stable lock before the engine starts playback. Any clock mismatch between the two PCIe cards forces LibVLC to software-resample audio, which:

- Degrades frame-accurate timer resolution
- Introduces micro-jitter that shifts MQTT cue timing
- Can cause audible artifacts at the Dolby CP950A

---

## ALSA Daemon Conflicts

On Ubuntu Server, ALSA direct hardware access (`hw:`) is blocked if `pulseaudio` or `pipewire` has the device open. Disable both:

```bash
# Disable PulseAudio
sudo systemctl --global disable pulseaudio.service pulseaudio.socket
sudo systemctl mask pulseaudio.service pulseaudio.socket

# Disable PipeWire (if present on Ubuntu 22.04+)
sudo systemctl --global disable pipewire.service pipewire-pulse.service
sudo systemctl mask pipewire.service pipewire-pulse.service

# Verify no process holds the RME device open
fuser /dev/snd/pcmC0D0p
```

---

## Channel Pinout Reference

The HDSPe AES mainboard 25-pin D-sub connector carries AES pairs 1–4 (channels 1–8). The expansion board adds AES pairs 5–8 (channels 9–16).

| AES Pair | Channels | DB25 Pin (+) | DB25 Pin (−) | Destination |
| --- | --- | --- | --- | --- |
| AES 1 | Ch 1–2 | 24 | 12 | CP950A RJ45-A Pin 1 / Pin 2 |
| AES 2 | Ch 3–4 | 11 | 23 | CP950A RJ45-A Pin 3 / Pin 4 |
| AES 3 | Ch 5–6 | 10 | 22 | CP950A RJ45-A Pin 5 / Pin 6 |
| AES 4 | Ch 7–8 | 9 | 21 | CP950A RJ45-A Pin 7 / Pin 8 |
| AES 5 | Ch 9–10 | 24\* | 12\* | CP950A RJ45-B Pin 1 / Pin 2 |
| AES 6 | Ch 11–12 | 11\* | 23\* | CP950A RJ45-B Pin 3 / Pin 4 |
| AES 7 | Ch 13–14 | 10\* | 22\* | CP950A RJ45-B Pin 5 / Pin 6 |
| AES 8 | Ch 15–16 | 9\* | 21\* | CP950A RJ45-B Pin 7 / Pin 8 |

\* Pins on the **expansion board DB25** (second connector, ch 9–16).

> [!danger] Custom Cable Required
> The RME HDSPe AES uses **DB25 (Tascam pinout)**. The CP950A uses **2× RJ45** for its AES input. These are physically incompatible. A custom breakout cable must be fabricated: DB25 (Tascam) → 2× RJ45 (Dolby cinema pinout). Dolby Part 7501670 performs the reverse direction; the CP950A installation manual (Scribd: <https://www.scribd.com/document/994984119/Dolby-Cp950-Cp950a-Manual-Issue-13>) should be consulted before cable fabrication to verify the exact RJ45 input pinout.

---

## SteadyClock™

RME SteadyClock regenerates a clean, low-jitter internal reference from the incoming Word Clock, even when the source has significant jitter (up to 100 ns PLL tolerance). This means:

- The RME card effectively re-clocks the Word Clock from the DXD-16
- The downstream Dolby CP950A receives clean, jitter-free AES timing
- LibVLC's `--clock-jitter=0` flag works reliably because the RME ALSA clock is already intrinsically stable

---

*See also: [[GitHub Reference Index]], [[Blackmagic DeckLink 8K Pro G2]], [[Brainstorm DXD-16]], [[Projection Spec]]*
