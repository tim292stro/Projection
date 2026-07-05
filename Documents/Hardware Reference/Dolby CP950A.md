# Dolby CP950A — Cinema Processor Integration

> [!abstract] Source
> Protocol confirmed from `bitfocus/companion-module-dolby-cinemaprocessor` source files `src/constants.js`, `src/utils.js`, and `src/actions.js`. See [[GitHub Reference Index]] for full provenance.

---

## AES Digital Audio I/O — Physical Connectors

> [!danger] Custom Cable Required
> The CP950A's AES audio inputs use **2× RJ45 connectors** (labeled AES 1-8 and AES 9-16). The RME HDSPe AES outputs on **DB25 (Tascam pinout)**. These are physically and electrically incompatible — a custom interface cable is required.

### CP950A RJ45 AES Connector Layout

| Connector | Channels | AES Pairs |
| --- | --- | --- |
| RJ45 — AES 1-8 (Side A) | Ch 1–8 | Pairs 1–4 |
| RJ45 — AES 9-16 (Side B) | Ch 9–16 | Pairs 5–8 |

Each RJ45 carries 4 AES/EBU stereo pairs (8 channels) using the Dolby cinema RJ45 wiring convention — the same format used on Dolby IMB/IMS cinema servers.

### Dolby RJ45 AES Pinout (per connector)

From Dolby IMB & IMS RJ45-DB25 Adapter Guide (Part 7501670), as confirmed on Scribd:

| RJ45 Pin | Signal | AES Pair |
| --- | --- | --- |
| 1 | AES pair 1 (+) | Ch 1+2 hot |
| 2 | AES pair 1 (−) | Ch 1+2 cold |
| 3 | AES pair 2 (+) | Ch 3+4 hot |
| 4 | AES pair 2 (−) | Ch 3+4 cold |
| 5 | AES pair 3 (+) | Ch 5+6 hot |
| 6 | AES pair 3 (−) | Ch 5+6 cold |
| 7 | AES pair 4 (+) | Ch 7+8 hot |
| 8 | AES pair 4 (−) | Ch 7+8 cold |

> [!note] Ground / Shield
> AES/EBU signal ground is carried on the cable shield, not on a dedicated RJ45 pin in this Dolby convention. Use shielded Cat 5e/6 cable and ensure shield is grounded at one end to avoid ground loops.

### RME HDSPe AES DB25 Tascam Output Pinout

The RME HDSPe AES mainboard DB25 carries AES output pairs 1–4 (ch 1–8). The expansion board DB25 carries pairs 5–8 (ch 9–16).

Standard Tascam DB25 AES/EBU digital pinout (outputs):

| DB25 Pin | Signal | AES Pair |
| --- | --- | --- |
| 24 | AES out 1 (+) | Ch 1+2 hot |
| 12 | AES out 1 (−) | Ch 1+2 cold |
| 11 | AES out 2 (+) | Ch 3+4 hot |
| 23 | AES out 2 (−) | Ch 3+4 cold |
| 10 | AES out 3 (+) | Ch 5+6 hot |
| 22 | AES out 3 (−) | Ch 5+6 cold |
| 9 | AES out 4 (+) | Ch 7+8 hot |
| 21 | AES out 4 (−) | Ch 7+8 cold |
| 25 | Signal GND | — |

### Custom Cable Map — DB25 (RME) → RJ45 (CP950A AES 1-8)

| RME DB25 Pin | → | CP950A RJ45-A Pin | Signal |
| --- | --- | --- | --- |
| 24 | → | 1 | AES1 + (Ch 1+2 hot) |
| 12 | → | 2 | AES1 − (Ch 1+2 cold) |
| 11 | → | 3 | AES2 + (Ch 3+4 hot) |
| 23 | → | 4 | AES2 − (Ch 3+4 cold) |
| 10 | → | 5 | AES3 + (Ch 5+6 hot) |
| 22 | → | 6 | AES3 − (Ch 5+6 cold) |
| 9 | → | 7 | AES4 + (Ch 7+8 hot) |
| 21 | → | 8 | AES4 − (Ch 7+8 cold) |
| 25 | → | Shield | Signal GND |

The expansion board DB25 maps identically to the CP950A RJ45-B connector (AES 9-16) for pairs 5–8.

> [!warning] Verify Before Fabricating
> The Dolby adapter guide (Part 7501670) is designed for the **opposite direction** (IMB/IMS RJ45 out → processor DB25 in). The CP950A-specific input pinout should be confirmed against the CP950A installation manual (Dolby Part Number 8800298, Issue 13) before cable fabrication. The manual is available on Scribd: <https://www.scribd.com/document/994984119/Dolby-Cp950-Cp950a-Manual-Issue-13>

### Dolby Cat.862

The user references "CAT.862" as the Dolby catalog designation for the CP950A's AES wiring interface. In Dolby's naming convention, "CAT" denotes a catalog product number (e.g., Cat.701 = Digital Soundhead Reader, Cat.745 = IMB). Cat.862 likely refers to the specific breakout cable assembly or wiring harness for this AES interface. Contact Dolby Cinema Support (`cinemasupport@dolby.com`) or consult the CP950A installation manual for the exact Cat.862 specification.

---

> [!warning] Non-Standard AES67
> The CP950A's network audio output is called **Dolby Atmos Connect**. It is a proprietary Dolby implementation of AES67 — **not** standard Dante and **not** visible in Dante Controller without a compatibility bridge.
>
> Confirmed by Dolby professional support engineer (Jan 2024):
> *"The CP950 and CP950A are not Dante devices and the AES67 implementation, Dolby Atmos Connect, was originally designed for use with other Dolby products. That said, there are some AES67 devices that have specific implementations to support for CP950 and CP950A and some AES67 devices that can be configured to work."*
>
> Source: [Dolby Professional Support Community](https://dolby.my.site.com/professionalsupport/s/question/0D54u0000AF6f5vCQB/using-a-cp950-to-send-a-multicast-flow-to-a-dante-chip-via-aes67?language=en_US)

The confirmed working solution for bridging to a Dante/AES67 ecosystem is the **BSS BLU-DA (BLU-DAN)**. Community-confirmed:
> *"I used a BLU-DAN to convert from BLU-link to AES67 and the flows show up in Dante as expected."*

Signal path: **CP950A Dolby Atmos Connect (BLU link port) → BSS BLU-DA BLU link input → BSS BLU-DA AES67 output → DiGiCo DMI-DANTE**.

See [[BSS BLU-DA (BLU-DAN)]] for bridging configuration details.

---

## TCP Control Connection

| Parameter | Value |
| --- | --- |
| Protocol | TCP |
| Port | **61408** |
| Encoding | ASCII, UTF-8 |
| Terminator | `\r\n` |

> [!note] Port by Model
>
> | Model | TCP Port |
> | --- | --- |
> | CP650 | 61412 |
> | CP750 | 61408 |
> | CP850 | 61408 |
> | **CP950** | **61408** |

---

## Command Protocol

The CP950/CP850/CP750 share a common plaintext protocol. Commands are ASCII strings terminated with `\r\n`. The CP950 uses bare `sys.*` prefix (no model prefix). The CP750 uses `cp750.sys.*`.

### Fader (Output Level)

| Command | Description |
| --- | --- |
| `sys.fader <0–100>\r\n` | Set output level; internal scale 0–100 maps to display 0.0–10.0 |
| `sys.fader ?\r\n` | Query current fader level |

> [!tip] Reference Level
> Nominal cinema reference level is **85** (8.5 on display). The companion module initialises `FADER_LEVEL = 85`.

### Mute

| Command | Description |
| --- | --- |
| `sys.mute 1\r\n` | Mute audio output |
| `sys.mute 0\r\n` | Unmute audio output |
| `sys.mute ?\r\n` | Query mute state |

### Macro Presets

| Command | Description |
| --- | --- |
| `sys.macro_preset <1–8>\r\n` | Select macro preset by number |
| `sys.macro_name <name>\r\n` | Select macro preset by name string |
| `sys.macro_preset ?\r\n` | Query active macro preset |
| `sys.macro_name ?\r\n` | Query active macro name |

---

## Response Format

Responses mirror the command structure:

```text
sys.fader 85
sys.mute 0
sys.macro_preset 3
sys.macro_name Dolby Atmos
```

Parse by splitting on the first space: key = `sys.fader`, value = `85`.

---

## Role in This Project

The CP950A sits **passively** in the audio signal path in this architecture:

1. RME HDSPe AES PCIe card sends LPCM or bitstream over AES cables to CP950A inputs
2. CP950A decodes (for bitstreams) or passes (for LPCM) to BluLink output
3. BluLink feeds BSS BLU-DA → AES67 multicast → DiGiCo Quantum-7t

The cinema engine does **not** need to send routing commands to the CP950A under normal operation — the audio format is detected automatically. The `sys.mute` command is used during the transition state machine to mute output while the shutter is closed and hardware is repositioning.

### Automatic Stereo Upmix (PCM 2.0)

Current operating model uses CP950A macro + format sensing for automatic stereo upmix:

1. Create macro `PCM Stereo Upmix` in CP950A web UI.
2. Bind macro input source to RME digital AES input bank.
3. Set decode mode to Dolby Surround.
4. Configure auto-routing/format sensing so incoming PCM 2.0 triggers `PCM Stereo Upmix`.

Result:

- Dolby bitstreams (AC-3 family) are decoded natively by CP950A.
- PCM stereo content is auto-upmixed without host-side command logic.
- Engine remains responsible for mute/unmute and transport sequencing only.

### Transition State Integration

```c
// In execute_transition_logic():
// 1. Close Christie shutter
send_christie_cmd("(SHU 001)\r\n");

// 2. Mute CP950A (TCP to port 61408)
send_cp950_cmd("sys.mute 1\r\n");

// ... hardware repositioning ...

// 3. Open Christie shutter
send_christie_cmd("(SHU 000)\r\n");

// 4. Unmute CP950A
send_cp950_cmd("sys.mute 0\r\n");
```

---

## C Implementation

```c
#define CP950_IP   "192.168.1.XX"   // Set to actual CP950A IP
#define CP950_PORT 61408

void send_cp950_cmd(const char* cmd_string) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(CP950_PORT)
    };
    inet_pton(AF_INET, CP950_IP, &addr.sin_addr);

    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        send(sock, cmd_string, strlen(cmd_string), 0);
    }
    close(sock);
}
```

---

## Audio Path Details

### LPCM Discrete Mode

When the MP4 file contains uncompressed multi-channel PCM audio:

- LibVLC flags `--rematrix=0 --audio-filter=none` enforce 1:1 channel-to-pin mapping
- Channels stream linearly from RME card → AES cable → CP950A analogue/digital inputs
- CP950A operates in pass-through mode

### Dolby Bitstream Mode (Atmos / TrueHD / Dolby E)

When the MP4 file contains a Dolby-encoded bitstream:

- LibVLC flag `--spdif` enables digital encapsulation, bypasses internal software decoder
- Raw bitstream payload transmitted over AES cables
- CP950A senses packet structure on its digital inputs and decodes spatial metadata natively
- BluLink output carries decoded channels downstream

---

*See also: [[GitHub Reference Index]], [[BSS BLU-DA (BLU-DAN)]], [[Projection Spec]]*
