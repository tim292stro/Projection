# Christie Sapphire — TruLife+ Serial Command Reference

> [!abstract] Source
> Commands confirmed from Bitfocus Companion module source `bitfocus/companion-module-christie-projector` ([`src/actions.js`](https://github.com/bitfocus/companion-module-christie-projector/blob/master/src/actions.js)) and the Christie CineLife+ Technical Reference (document `020-102714-01`). See [[GitHub Reference Index]] for full provenance.

---

## Connection

| Parameter | Value |
| --- | --- |
| Protocol | TCP |
| Port | **3002** |
| Encoding | ASCII |
| Terminator | `\r\n` |
| Timeout (recommended) | 2.0 s |

Commands are stateless — open socket, send, close. No persistent session is required.

---

## Command Syntax

All commands follow the parenthesis-wrapped format:

```text
(COMMAND [VALUE])\r\n
```

Integer values are **zero-padded** to a fixed width:

| Pad Width | Function | Example |
| --- | --- | --- |
| `pad2` | 2-digit int | `01`, `50` |
| `pad3` | 3-digit int | `001`, `100` |
| `pad4` | 4-digit int | `0000`, `1000` |

Query variant appends `?` inside the parens: `(LPH?)`

---

## Core Commands Used by This Project

### Power

| Command | Description |
| --- | --- |
| `(PWR 1)` | Power on / wake from standby |
| `(PWR 0)` | Power off / enter standby |

### Shutter / Douser

| Command | Description |
| --- | --- |
| `(SHU 000)` | Open shutter — audience sees image |
| `(SHU 001)` | Close shutter — douser blade drops, screen goes black |

> [!warning] Pad Format
> Shutter command uses **pad3** (3-digit): `SHU 000` / `SHU 001`.

### Laser Power (CineLife+ / Sapphire RGB Series)

| Command | Range | Description |
| --- | --- | --- |
| `(LPLV nnnn)` | 0–1000 | Laser Power Level — 0=0%, 1000=100% |

> [!important] LPLV vs LPI
> `LPLV` (Laser Power Level) is specific to **CineLife+ RGB laser** projectors and is documented in Christie technical reference `020-102714-01`. It is distinct from `LPI` (Lamp Intensity, 0–9999) used on older lamp-based models. The Sapphire 4K-RGBH uses `LPLV`.

CIH calibration formula used in this project:
$$\text{LPLV} = \text{clamp}(435.484 \cdot A - 225.154,\ 200,\ 1000)$$

Where $A$ is the floating-point aspect ratio of the next file.

### Brightness / Image

| Command | Range | Description |
| --- | --- | --- |
| `(BRT nnnn)` | 0–1000 | Brightness (pad4; 505 = 50.5%) |
| `(CON nnnn)` | 0–1000 | Contrast (pad4) |
| `(GAM nnn)` | 100–280 | Gamma |

### Input Selection

| Command | Description |
| --- | --- |
| `(SIN n)` | Select input by number |
| `(SIN+MAIN nn nn)` | Select input by slot and input number (main) |
| `(CHA nn)` | Channel select 1–50 (pad2) |

### Lens Motors (ILS / J-Series)

| Command | Description |
| --- | --- |
| `(LHO nnnn)` | Lens shift horizontal absolute position (−2050 to 2050, pad4) |
| `(LVO nnnn)` | Lens shift vertical absolute position (−2050 to 2050, pad4) |
| `(FCS nnnn)` | Focus (−1200 to 1200, pad4) |
| `(ZOM nnnn)` | Zoom (−1200 to 1200, pad4) |
| `(LMV+HRUN 1)` | Start moving horizontal motor to positive max |
| `(LMV+HRUN-1)` | Start moving horizontal motor to negative max |
| `(LMV+HRUN 0)` | Stop horizontal motor |

### Query / Diagnostics

| Command | Description |
| --- | --- |
| `(LPH?)` | Query lamp/laser hours of use |
| `(FYI)` | General status query (returns FYI packet) |
| `(EME n)` | Error message enable (3 = all errors reported) |

### Miscellaneous

| Command | Description |
| --- | --- |
| `(OSD 1)` | Enable on-screen display |
| `(OSD 0)` | Disable on-screen display |
| `(FRZ 1)` | Freeze image |
| `(FRZ 0)` | Unfreeze image |
| `(ITP n)` | Display internal test pattern |
| `(ASU)` | Auto setup |
| `(ALC n)` | Automatic lens calibration on/off |
| `(DEF 111)` | Factory defaults (confirmation required) |

---

## C Implementation (sdi_cinema_engine.c)

```c
#define CHRISTIE_IP   "192.168.1.75"
#define CHRISTIE_PORT 3002

void send_christie_cmd(const char* cmd_string) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(CHRISTIE_PORT)
    };
    inet_pton(AF_INET, CHRISTIE_IP, &addr.sin_addr);

    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        send(sock, cmd_string, strlen(cmd_string), 0);
    }
    close(sock);
}

// Usage examples:
// send_christie_cmd("(SHU 001)\r\n");   // Close shutter
// send_christie_cmd("(SHU 000)\r\n");   // Open shutter
// send_christie_cmd("(LPLV 0820)\r\n"); // 82% laser power (2.40:1 scope)
// send_christie_cmd("(PWR 1)\r\n");     // Power on
```

---

## Network Notes

- The Christie Sapphire uses a **dedicated control network port** separate from its content input ports. Assign a static IP on the same subnet as the server's control NIC.
- The projector responds with ASCII status strings; for most automation commands the response can be ignored.
- If `(EME 3)` is enabled, the projector will proactively push error strings — the TCP listener should handle unsolicited data gracefully.

---

*See also: [[GitHub Reference Index]], [[Projection Spec]]*
