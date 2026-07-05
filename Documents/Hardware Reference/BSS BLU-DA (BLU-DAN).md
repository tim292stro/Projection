# BSS BLU-DA (BLU-DAN) — Dante / AES67 Bridge

> [!abstract] Source
> BSS Audio product page: <https://bssaudio.com/en-US/products/blu-dan>. Bitfocus companion module: <https://github.com/bitfocus/companion-module-bss-soundweb>. See [[GitHub Reference Index]] for full provenance.

---

## Hardware Overview

| Spec | Value |
| --- | --- |
| Form factor | 1RU, half-rack |
| Current model name | **BLU-DA** (formerly BLU-DAN) |
| Dante I/O | 64 in × 64 out @ 48 kHz |
| AES67 | Yes (AES67 compatible) |
| BLU link | 256 channels, low-latency, Cat 5e |
| Control port | Separate Ethernet port (HiQnet) |
| RS-232 | Yes |
| Dante Domain Manager | Supported (Dante 4.0 firmware) |
| Fault tolerance | Primary + Secondary Dante ports |

---

## Role in the Signal Path

The BLU-DA is an **essential compatibility bridge** between the Dolby CP950A and the DiGiCo Quantum-7t. The CP950A's network audio output uses a proprietary protocol called **Dolby Atmos Connect**, which is not visible as a standard AES67/Dante flow to most third-party devices.

The BLU-DA's BLU link port can receive the CP950A's Dolby Atmos Connect stream and re-emit it as standard AES67 multicast, which the DiGiCo DMI-DANTE card can then subscribe to normally.

```text
Dolby CP950A  ──(Dolby Atmos Connect / BLU link)──►  BSS BLU-DA  ──(AES67 Multicast)──►  DiGiCo Quantum-7t
                                                                       (63-channel)
```

> [!important] Why BLU-DA is Required
> Confirmed by Dolby support (Jan 2024) and community user testing:
>
> - CP950A AES67 (Dolby Atmos Connect) does **not** appear in Dante Controller without a bridge
> - Direct connection to Allen & Heath, Yamaha, Q-SYS, and Audinate AVIO devices all failed
> - BSS BLU-DAN was the confirmed working solution: *"I used a BLU-DAN to convert from BLU-link to AES67 and the flows show up in Dante as expected"*
> - Source: [Dolby Professional Support Community, Jan 2024](https://dolby.my.site.com/professionalsupport/s/question/0D54u0000AF6f5vCQB/)

The cinema engine does **not** send real-time TCP commands to the BLU-DA. It is a static bridge device configured once during system setup and then left in normal operation.

---

## One-Time Configuration (HiQnet Audio Architect)

Configuration is performed on a Windows PC running **HiQnet Audio Architect** (free download from Harman/BSS):

1. Connect the BLU-DA control port to the same network as the configuration PC
2. Launch Audio Architect — the device auto-discovers via HiQnet
3. Assign a static IP in Audio Architect
4. Map Dante/AES67 channels to BLU link channel slots using the drag-and-drop matrix
5. Set the AES67 multicast group address (must match DiGiCo DMI-DANTE subscription)
6. Save and lock the device configuration
7. Disconnect the config PC — the BLU-DA operates standalone from this point

> [!tip] Configuration Persistence
> Like the RME card, the BLU-DA stores its configuration non-volatilely. Once programmed, it boots into operational state automatically with no host PC required.

---

## AES67 Multicast Configuration

The DiGiCo Quantum-7t's **DMI-DANTE** card subscribes to the AES67 multicast stream. Both devices must agree on:

| Parameter | Value |
| --- | --- |
| Multicast group | Configure in Dante Controller / Audio Architect |
| Sample rate | 48 kHz (cinema standard) |
| Bit depth | 24-bit |
| Packet time | 1 ms (recommended for low latency) |
| Channel count | 63 (max per the spec) |

---

## Network Topology

```text
[Server NIC - Control]
    │
    ├──────────────── Christie Sapphire (192.168.1.75:3002)
    ├──────────────── Dolby CP950A     (192.168.1.XX:61408)
    ├──────────────── MQTT Broker      (192.168.1.50:1883)
    └──────────────── BLU-DA Control   (192.168.1.XX - HiQnet only)

[Dedicated AES67 / Dante Switch]
    │
    ├──────────────── BLU-DA Primary Dante port
    ├──────────────── BLU-DA Secondary Dante port (redundancy)
    └──────────────── DiGiCo DMI-DANTE Primary/Secondary
```

> [!warning] Network Separation
> Dante/AES67 traffic is extremely time-sensitive. The Dante network **must** be on a separate VLAN or physical switch from general IT traffic. Do not route AES67 multicast through a managed switch that is not configured for multicast (IGMP snooping must be enabled).

---

## Dante vs AES67 Mode

The BLU-DA supports both Dante and AES67. For this project, **AES67 mode** is preferred because:

- AES67 is an open standard, interoperable with the DiGiCo DMI-DANTE at 96kHz/48kHz
- AES67 multicast avoids unicast licensing constraints
- The DiGiCo DMI-DANTE card natively subscribes to AES67 multicast flows

---

*See also: [[GitHub Reference Index]], [[Dolby CP950A]], [[DiGiCo Quantum-7t]], [[Projection Spec]]*
