# DiGiCo Quantum-7t — Console Integration

> [!abstract] Source
> DiGiCo Quantum 7T product page: <https://digico.biz/quantum7t>. Support portal: <https://support.digico.biz>. See [[GitHub Reference Index]] for full provenance.

---

## Role in This System

The DiGiCo Quantum-7t is the **final stage** of the audio signal chain. It receives 63 channels of AES67 multicast from the BSS BLU-DA and routes them directly to the Meyer Sound arrays. The cinema engine does **not** control the Quantum-7t in real time — it is configured by the sound engineer and runs autonomously.

```text
BSS BLU-DA ──(63-ch AES67 Multicast)──► DiGiCo DMI-DANTE card
                                                  │
                                        63-ch Direct Passthrough
                                                  │
                                                  ▼
                                       Meyer Sound Arrays
```

---

## Hardware Connectivity

| Interface | Spec | Use in This Project |
| --- | --- | --- |
| Local AES I/O | 12 mono AES I/O | Not used (AES67/Dante is primary) |
| **DMI-DANTE** | 64 in × 64 out @ 48 kHz | **Primary AES67 input** (63 channels from BLU-DA) |
| DMI-DANTE (96kHz) | 32 in × 32 out | Available if 96kHz upgrade needed |
| Optocore | 504 ch fibre loop (standard) | Available for stage rack expansion |
| UBMADI | 48-ch USB MADI | Recording/broadcast feed (optional) |
| External sync | Word Clock, AES, Video, MADI, Optocore | Sync to DXD-16 via AES or Word Clock |

---

## DMI-DANTE Card Configuration

The **DMI-DANTE** card provides the AES67 subscription interface:

1. Install Dante Controller software on a Windows PC on the Dante/AES67 network
2. In Dante Controller, subscribe the DMI-DANTE **receivers** to the BLU-DA **transmitter** channels
3. Confirm channel count: 63 channels (or up to 64 at 48 kHz)
4. Verify sample rate lock: 48 kHz on both BLU-DA and DMI-DANTE

> [!note] Dante Domain Manager
> The BLU-DA supports Dante Domain Manager (DDM) for managed Dante networks. If DDM is in use, the DMI-DANTE card must also be registered in the same DDM domain before subscriptions can be made.

---

## Clock Sync

The Quantum-7t must be clocked synchronously with the rest of the system:

| Option | Path |
| --- | --- |
| **AES sync** (preferred) | Subscribe one AES67 channel from BLU-DA as the clock reference |
| Word Clock | Connect DXD-16 Word Clock output to Quantum-7t Word Clock input |
| Dante clock | Allow Dante to elect a clock master (BLU-DA preferred as grandmaster) |

The Quantum-7t's internal clock is set in **Ext Sync** mode (Word Clock / AES / MADI / Optocore).

---

## Show File / Snapshot

The Quantum-7t runs a static **show file** configured by the system operator. For an automated cinema application:

- All 63 incoming Dante channels are routed to the appropriate output busses
- Snapshots are not needed (no dynamic routing changes during a film)
- VCA masters may be used for overall level management

---

## Specifications Relevant to This Project

| Parameter | Value |
| --- | --- |
| Sample rates | 48 kHz / 96 kHz |
| Processing delay | ~2 ms @ 48 kHz (through L/R buss) |
| Internal processing | Up to 40-bit floating point |
| Maximum output level | +22 dBu |
| Noise floor | Better than −126 dB EIN (mic input) |

---

*See also: [[GitHub Reference Index]], [[BSS BLU-DA (BLU-DAN)]], [[Projection Spec]]*
