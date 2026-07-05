# Brainstorm Electronics DXD-16 — Reference Clock

> [!abstract] Source
> Brainstorm Electronics product page: <https://www.brainstormelectronics.com/products/dxd-16/> (content not publicly fetchable at time of writing; page confirmed to exist). Hardware specifications sourced from manufacturer product literature and consolidated project notes. See [[GitHub Reference Index]] for full provenance.

---

## Role in This System

The DXD-16 is the **single clock master** for the entire signal chain. It generates all reference signals distributed to both PCIe hardware cards. Every downstream device is slaved to the DXD-16 — no other device in the chain acts as a clock source.

```text
                    DXD-16 (Master)
                         │
            ┌────────────┴────────────┐
      Tri-Level Sync           Word Clock
            │                        │
            ▼                        ▼
  DeckLink 8K Pro G2          RME HDSPe AES
  (Genlock BNC input)     (Word Clock BNC input)
```

---

## Reference Outputs Used

| Output Type | Signal | Destination | Purpose |
| --- | --- | --- | --- |
| Tri-Level Sync | HD Black Burst (1080i) | DeckLink Genlock BNC | Locks video output frame timing |
| Word Clock | 48 kHz square wave | RME Word Clock BNC | Locks audio sample clock |

---

## Configuration Notes

The DXD-16 is configured via its **front panel** and stores settings non-volatilely. No host PC or network connection is required during operation.

**Boot sequence dependency:** The DXD-16 must be powered on and locked **before** the cinema server boots. This is enforced by:

1. Powering the DXD-16 from the rack UPS on a separate, earlier-priority power circuit, or
2. Adding a pre-start `ExecStartPre` sleep to the systemd service (see [[Ubuntu Server Boot Configuration]])

> [!warning] Cold Boot Order
> If the DXD-16 is not stable when the Ubuntu server initializes the DeckLink and RME kernel modules, neither card will establish hardware lock. The cinema engine will launch but the SDI output will be unlocked, causing the Christie projector to display a "No Signal" or "Sync" error.

---

## Runtime Preset Orchestration (Current Engine State)

The engine now performs automatic DXD-16 preset recall by detected source frame-rate family.

Preset map used by runtime:

| Preset | Domain | Source FPS Families |
| --- | --- | --- |
| A (Fractional) | 59.94 Hz timeline | 23.976, 29.97, 59.94, 119.88 |
| B (Integer) | 60.00 Hz timeline | 24.00, 30.00, 60.00, 120.00 |
| C (PAL) | 50.00 Hz timeline | 25.00, 50.00, 100.00 |

Integration points:

- Invoked during power-on for first queued file
- Invoked during transition before next-title playback

Environment controls:

```bash
export DXD16_URL="http://192.168.1.60"
export DXD16_USER="admin"                 # Optional
export DXD16_PASS="password"              # Optional
export DXD16_PRESET_A_NAME="A"            # Optional default A
export DXD16_PRESET_B_NAME="B"            # Optional default B
export DXD16_PRESET_C_NAME="C"            # Optional default C
export DXD16_LOCK_WAIT_MS="1500"          # Optional lock settle wait

# Optional explicit recall URLs (override derived paths)
# export DXD16_PRESET_A_URL="http://192.168.1.60/custom/recallA"
# export DXD16_PRESET_B_URL="http://192.168.1.60/custom/recallB"
# export DXD16_PRESET_C_URL="http://192.168.1.60/custom/recallC"
```

> [!note] Non-fatal Control Path
> If DXD-16 recall endpoint(s) are unavailable, playback continues and the engine logs a timing profile warning.

---

## Recommended Front Panel Settings

| Parameter | Setting | Rationale |
| --- | --- | --- |
| Master clock source | Internal | DXD-16 is the grandmaster; no upstream reference |
| Output frequency | 48 kHz | Matches DCI cinema audio standard |
| Video reference format | Tri-Level Sync (1080i) | Matches DeckLink genlock expectation |
| Word clock level | Standard (TTL, 5V into 75Ω) | RME input expects standard BNC word clock |

---

## Cabling

- All BNC cables carrying sync signals should be **75Ω impedance-matched** coaxial cable (RG-59 or equivalent)
- Terminate unused word clock outputs with 75Ω BNC terminators to prevent reflections
- Keep sync cable runs as short as practical (under 10m is ideal)

---

*See also: [[GitHub Reference Index]], [[Blackmagic DeckLink 8K Pro G2]], [[RME HDSPe AES]], [[Projection Spec]]*
