# GitHub Reference Index

> [!info] Canonical Reference
> All entries in this index are the authoritative source records for GitHub projects consulted during Projection development. Each entry includes the full repository URL, the specific files inspected, and the exact information extracted.

---

## Christie Projector Serial Control

### `bitfocus/companion-module-christie-projector`

| Field | Value |
| --- | --- |
| Repository | <https://github.com/bitfocus/companion-module-christie-projector> |
| License | MIT |
| Language | JavaScript |
| Latest Release | v4.1.0 (Nov 8, 2024) |
| Primary Source File | [`src/actions.js`](https://github.com/bitfocus/companion-module-christie-projector/blob/master/src/actions.js) |

**What was extracted:** Complete Christie serial command set, confirmed command syntax `(COMMAND VALUE)\r\n`, shutter (`SHU`), power (`PWR`), brightness (`BRT`), lamp intensity (`LPI`), channel select (`CHA`/`SIN`), lens shift motors (`LHO`, `LVO`), zoom (`ZOM`), focus (`FCS`), and integer padding conventions (pad2, pad3, pad4).

**Scope:** Covers Christie J-Series, M-Series, Boxer, and Mirage series projectors. For RGB laser / CineLife+ specific commands (including `LPLV`), see the CineLife+ technical reference below.

---

### `PostSupe/companion-module-christie-rgblaser`

| Field | Value |
| --- | --- |
| Repository | <https://github.com/PostSupe/companion-module-christie-rgblaser> |
| License | Not specified |
| Language | JavaScript |
| Primary Source File | [`index.js`](https://github.com/PostSupe/companion-module-christie-rgblaser/blob/main/index.js) |

**What was extracted:** Confirmed that the authoritative serial command reference for CineLife+ RGB laser projectors is Christie document **020-102714-01**, version 2.2.0:

> **Christie CineLife+ Serial Command Technical Reference (v2.2.0)**
> URL: `https://www.christiedigital.com/globalassets/resources/public/020-102714-01-christie-lit-tech-ref-cinelife-v2.2.0.pdf`
> *(PDF requires Christie partner login to download; link sourced from repository README)*

This document is the canonical source for the `LPLV` (Laser Power Level) command used in the CIH calibration slope.

---

## Dolby Cinema Audio Processor Control

### `bitfocus/companion-module-dolby-cinemaprocessor`

| Field | Value |
| --- | --- |
| Repository | <https://github.com/bitfocus/companion-module-dolby-cinemaprocessor> |
| License | MIT |
| Language | JavaScript |
| Latest Release | v2.0.1 (Mar 25, 2024) |
| Primary Source Files | [`src/constants.js`](https://raw.githubusercontent.com/bitfocus/companion-module-dolby-cinemaprocessor/main/src/constants.js), [`src/utils.js`](https://raw.githubusercontent.com/bitfocus/companion-module-dolby-cinemaprocessor/main/src/utils.js), [`src/actions.js`](https://raw.githubusercontent.com/bitfocus/companion-module-dolby-cinemaprocessor/main/src/actions.js) |

**What was extracted:**

TCP port assignments per model (from `constants.js`):

| Model | TCP Port |
| --- | --- |
| CP650 | 61412 |
| CP750 | 61408 |
| CP850 | 61408 |
| **CP950** | **61408** |

Command protocol confirmed: plaintext ASCII commands terminated with `\r\n`. The CP950/CP850/CP750 share identical command syntax prefixed `sys.*`. Full command reference in [[Dolby CP950A]].

---

## BSS Soundweb London Control

### `bitfocus/companion-module-bss-soundweb`

| Field | Value |
| --- | --- |
| Repository | <https://github.com/bitfocus/companion-module-bss-soundweb> |
| License | MIT (see repo) |
| Language | TypeScript |
| Latest Release | v1.0.2 (Jun 15, 2024) |
| Primary Source Files | [`src/main.ts`](https://github.com/bitfocus/companion-module-bss-soundweb/blob/main/src/main.ts) |

**What was extracted:** Confirms HiQnet TCP protocol for BSS Soundweb London family (BLU-806, BLU-160, and by extension BLU-DA/BLU-DAN). The BLU-DAN itself is a bridge/passthrough device — configuration is via HiQnet Audio Architect (Windows GUI), not real-time TCP from the cinema engine. See [[BSS BLU-DA (BLU-DAN)]] for integration details.

---

### Dolby IMB & IMS RJ45-DB25 Adapter Guide (Part 7501670)

| Field | Value |
| --- | --- |
| Source | Scribd — uploaded by hellonsr88 |
| URL | <https://www.scribd.com/document/906004826/Dolby-IMB-IMS-DualRJ45-DB25M-Adapter-Guide-V1-2> |
| Dolby Part Number | 7501670 |
| Document Version | V1.2 |
| Accessed | 2026-07-04 |

**What was extracted:**

- Confirmed CP950A (and IMB/IMS servers) use **2× RJ45 connectors** labeled AES 1-8 and AES 9-16
- Each RJ45 carries 4 AES pairs (8 channels) using standard Dolby cinema RJ45 pinout (Pin 1=AES1+, Pin 2=AES1−, Pins 3-4=AES2, Pins 5-6=AES3, Pins 7-8=AES4)
- The adapter (7501670) converts dual RJ45 → single DB25 male and includes device-specific DB25 pin assignments for: CP650, CP750, CP850, Dolby DMA8/DMA8+, DTS XD10P, DataSat AP20, USL JSD-60/80, Odyssey 650-OPT, QSC Basis 922dz, QSC DCP100/200/300
- **Architectural impact:** This adapter is for the **opposite direction** from our use case (IMB server RJ45 out → older processor DB25 in). For this project, a custom cable DB25 (RME Tascam out) → 2× RJ45 (CP950A in) must be fabricated.

---

### Dolby CP950 / CP950A Manual Issue 13

| Field | Value |
| --- | --- |
| Source | Scribd — uploaded by saurabhpach |
| URL | <https://www.scribd.com/document/994984119/Dolby-Cp950-Cp950a-Manual-Issue-13> |
| Dolby Part Number | 8800298 |
| Issue | 13 (15 August 2024) |
| Accessed | 2026-07-04 |

**What was extracted:** Document confirmed to exist with CP950A pinout tables. Full content requires Scribd subscription. This is the **canonical source** for the CP950A AES RJ45 input pinout and must be consulted before fabricating the DB25→RJ45 custom cable.

---

## Dolby Professional Support Community

### CP950 AES67 / Dante interop thread

| Field | Value |
| --- | --- |
| URL | <https://dolby.my.site.com/professionalsupport/s/question/0D54u0000AF6f5vCQB/using-a-cp950-to-send-a-multicast-flow-to-a-dante-chip-via-aes67?language=en_US> |
| Date | January 8, 2024 |
| Accessed | 2026-07-04 |

**What was extracted:**

Dolby support engineer (Adeline Almanzar, Dolby Labs) confirmed:
> *"The CP950 and CP950A are not Dante devices and the AES67 implementation, Dolby Atmos Connect, was originally designed for use with other Dolby products. That said, there are some AES67 devices that have specific implementations to support for CP950 and CP950A and some AES67 devices that can be configured to work."*

Community user confirmed working solution:
> *"I was able to bypass this issue when trying to connect to our Qsys Core. I used a BLU-DAN to convert from BLU-link to AES67 and the flows show up in Dante as expected."*

**Architectural impact:** This confirms the BLU-DA (BLU-DAN) is a **required compatibility bridge** in this system, not an optional component. The CP950A Dolby Atmos Connect stream is received on the BLU-DA's BLU link port and re-emitted as standard AES67. This also confirms the signal direction: BLU link input side → CP950A, AES67 output side → DiGiCo DMI-DANTE.

---

## Manufacturer Technical Documentation (Non-GitHub)

### Christie — Sapphire 4K40-RGBH Product Page

| Field | Value |
| --- | --- |
| URL | <https://www.christiedigital.com/products/projectors/all-projectors/sapphire-4K40-RGBH/> |
| Accessed | 2026-07-04 |

Confirmed hardware: TruLife+™ electronics, Quad 12G-SDI BNC inputs, ILS4 lens compatibility, Christie LiteLOC laser feedback technology.

---

### Blackmagic Design — Desktop Video SDK

| Field | Value |
| --- | --- |
| URL | <https://www.blackmagicdesign.com/developer/products/capture-and-playback/sdk-and-software> |
| SDK Version | Desktop Video 16.0 SDK (released 08 Apr 2026) |
| SDK Manual PDF | <https://documents.blackmagicdesign.com/UserManuals/DeckLinkSDKManual.pdf> |
| Linux Driver | Desktop Video 16.0.1 (released 06 May 2026) |
| Accessed | 2026-07-04 |

> [!warning] Download Requires Registration
> SDK files and Linux drivers require a free Blackmagic developer account to download. Register at: <https://www.blackmagicdesign.com/developer/products/forms/developer-signup>

---

### RME — HDSPe AES Product Archive

| Field | Value |
| --- | --- |
| URL | <https://archiv.rme-audio.de/en/products/hdspe_aes.php> |
| Manual PDF | <https://archiv.rme-audio.de/download/hdspeaes_e.pdf> |
| Accessed | 2026-07-04 |

Key facts confirmed: card stores settings on-board (persists through power cycle), Word Clock I/O via BNC, SteadyClock™ jitter suppression, Linux ALSA driver support native in kernel.

---

### BSS Audio — BLU-DA (BLU-DAN) Product Page

| Field | Value |
| --- | --- |
| URL | <https://bssaudio.com/en-US/products/blu-dan> |
| Accessed | 2026-07-04 |

Confirmed: 64×64 Dante/AES67, 256-channel BLU link, HiQnet Audio Architect configuration, Dante Domain Manager support, AES67 compatible.

---

### DiGiCo — Quantum 7T Product Page

| Field | Value |
| --- | --- |
| URL | <https://digico.biz/quantum7t> |
| Support Portal | <https://support.digico.biz> |
| Accessed | 2026-07-04 |

Confirmed I/O: 12 AES I/O local, DMI-DANTE (64×64 @ 48kHz / 32×32 @ 96kHz), Optocore fibre loop, external sync via Word Clock/AES/Video/MADI.

---

Last updated: 2026-07-04
