# AGENTS.md — tuya_rf ESPHome Component & RF Bridge Reference

This documents everything learned across a long debugging session working with the
`tuya_rf` custom ESPHome component on a **NAS-IR02W6-Pro** (Tuya IR+RF bridge:
CBU/BK7231N module + SH4 RF module containing a **CMT2300A** Sub-GHz transceiver).
Future sessions should read this before modifying the component or troubleshooting
RF capture/transmit issues.

---

## 1. Hardware Overview

- **MCU**: Beken BK7231N (CBU module), running LibreTiny/ESPHome.
- **RF chip**: CMT2300A (inside the Tuya SH4 module), 26 MHz crystal.
- **RF front-end**: No external PA; CMT2300A internal PA drives the antenna directly
  through the SH4's LC matching network + host PCB trace antenna.
- **Antenna**: PCB trace antenna tuned for **433.92 MHz**. The SH4 module exposes
  an IPEX/U.FL socket (on some board variants). At 303/315 MHz, the matching network
  and antenna cause significant mismatch loss (several dB) → reduced range.
- **SPI pins** (CMT2300A config): SCLK=P14, MOSI(P16)/SDIO, CSB=P6, FCSB=P26.
- **TX pin**: P20 (inverted by default). **RX pin**: P22 (inverted by default).
- **IR**: TX=P7, RX=P8 (standard remote_transmitter/receiver, unrelated to RF).

### CMT2300A Frequency Capability
- Chip supports **127–1020 MHz** (multiple sub-bands via VCO + output divider).
- 26 MHz crystal confirmed by register reverse-engineering.
- Frequency formula: `f_rf = 26 MHz × (N + K/2²⁰) / DIV` where DIV ∈ {2,4,6,8,12}.
- VCO_BANK=1 for f_VCO ∈ 1680–2040 MHz; VCO_BANK=6 for 1516–1680 MHz.
- 303 MHz → ÷6/N=69; 315 MHz → ÷6/N=72; 433.92 MHz → ÷4/N=66; 868 MHz → ÷2/N=66.

### Variant A vs Variant B Boards
- **Variant A**: SH4's SPI lines wired to MCU → can fully reprogram CMT2300A registers
  (frequency changes work). The NAS-IR02W6-Pro and similar boards are Variant A.
- **Variant B**: SPI not connected → CMT2300A factory-locked to 433.92 MHz. Cannot
  change frequency. If TX works at all, it's Variant A.

---

## 2. Component File Structure

```
tuya_rf/components/tuya_rf/
├── __init__.py           # ESPHome Python codegen: config schema, actions, codegen
├── tuya_rf.h             # Component class declaration (members, methods)
├── tuya_rf.cpp           # Stub (TAG only)
├── tuya_rf_libretiny.cpp # Main implementation: setup, send_internal, loop, learn mode
├── automation.h          # Action classes: set_frequency, replay_last_capture, etc.
├── automation.cpp        # Stub
├── radio.h / radio.c     # C radio interface: StartTx, StartRx, RF_SetFrequency
├── cmt2300a.h / .c       # CMT2300A driver (CMOSTEK SDK)
├── cmt2300a_defs.h       # Register definitions
├── cmt2300a_hal.h / .c   # SPI HAL
├── cmt2300a_params_captured.h  # Default register banks (433.92 MHz)
├── cmt_spi3.h / .c       # 3-wire SPI bit-bang
├── globals.h             # extern SPI pin numbers
└── radio.h               # RF_SetFrequency declaration
```

### Key Config Options (in YAML `tuya_rf:` block)
| Option | Default | Description |
|--------|---------|-------------|
| `frequency` | `433.92MHz` | Carrier frequency (127–1020 MHz, validated) |
| `learn_mode` | `false` | Protocol-agnostic capture (any signal, noise-filtered). The receiver only runs while learn mode is active. |
| `rssi_floor` | `-70` | dBm threshold; bursts below this are treated as noise |
| `receive_timeout` | `50ms` | Idle period that finalizes a learn-mode capture |
| `raw_capture` | `false` | Bypass cleanup; dump the raw burst |
| `invert_signal` | `true` | Mark/space polarity (see §3) |
| `dump` | `[]` | Dumper type (`raw` for learn mode) |

### Actions
- `tuya_rf.set_frequency` — change carrier frequency at runtime (templatable).
- `tuya_rf.replay_last_capture` — transmit the most recent captured code.
- `tuya_rf.turn_on_receiver` / `tuya_rf.turn_off_receiver` — toggle RX.

---

## 3. Signal Polarity (CRITICAL GOTCHA)

**The tuya_rf component's `send_internal` uses INVERTED polarity** relative to both
standard ESPHome and the Flipper Zero `.sub` format:

| Convention | Positive value | Negative value |
|---|---|---|
| Standard ESPHome | mark (carrier ON) | space (carrier OFF) |
| Flipper `.sub` | mark (carrier ON) | space (carrier OFF) |
| **tuya_rf component** | **space (carrier OFF)** | **mark (carrier ON)** |

This is because the SH4 module's TX pin is inverted (0=pulse, 1=space). The
component compensates by flipping the code-array interpretation in `send_internal`:
```cpp
bool mark = this->invert_signal_ ? (item < 0) : (item > 0);
```
With `invert_signal: true` (default), **negative code values = marks (carrier ON)**.

### Consequence for code generation
- **Flipper `.sub` captures** (positive=mark) must be **NEGATED** for this component.
  `sub_to_esphome.py` does this by default (`--polarity inverted`).
- **On-device learn-mode captures** are already in the component convention
  (negative=mark) — they can be used directly in `transmit_raw` without negation.
- The `invert_signal: false` config option disables the inversion (for use with
  standard-polarity codes).

---

## 4. Frequency Programming (RF_SetFrequency)

Implemented in `radio.c`. Computes the CMT2300A Frequency Bank registers (0x18–0x1F)
for any target frequency. Key points:

- `g_cmt2300aFrequencyBank` is declared `extern` in `cmt2300a_params_captured.h` and
  **defined in `radio.c`** (not the header). This avoids multiple-definition link
  errors when the header is included by multiple translation units (the original
  `const` arrays survived via C++ internal linkage, but the mutable frequency bank
  would not).
- The function writes both RX (indices 0–3) and TX (indices 4–7) banks. RX uses a
  low-IF offset of `f_xtal/92 ≈ 283 kHz`.
- Byte packing: index 3 holds `PALDO_SEL[7] | DIVX_CODE[6:4] | RX_K_hi[3:0]`;
  index 7 holds `FSK_SWT[7] | VCO_BANK[6:4] | TX_K_hi[3:0]`. The mask for index 3
  must be `& 0x80` (preserve only PALDO_SEL), NOT `& 0x8F` (which would OR-corrupt
  the RX K-high nibble). This was a bug that was caught and fixed.
- The new frequency is written to the chip on the next `RF_Init()` (called by
  `StartTx`/`StartRx`). `set_frequency()` does NOT need to restart the receiver —
  doing so (a `set_receiver(false)/set_receiver(true)` right before `StartTx`) was
  found to **degrade the transmission**. The retune was removed.

### DIVX_CODE → Divider table (from CMOSTEK AN199)
| DIVX_CODE | 0 | 1 | 2 | 3 | 5 |
|---|---|---|---|---|---|
| Divider | 2 | 4 | 8 | 12 | 6 |
(Codes 4, 6, 7 are reserved.)

---

## 5. Learn Mode (On-Device Capture)

The `learn_mode` config option enables protocol-agnostic RF capture. It captures
any signal (no fixed start/end-pulse window) and gates it on RSSI + structural
cleanup.

### Architecture
1. The ISR fills a ring buffer with edge timestamps (GPIO interrupts on the RX pin).
2. `loop()` processes the buffer: samples RSSI, accumulates edges, and flushes when
   the signal goes quiet (`receive_timeout`) or the buffer nears overflow.
3. On flush, the raw edge deltas are built into `RemoteReceiverBase::temp_`.
4. **RSSI gate**: if peak RSSI < `rssi_floor`, the burst is discarded (noise).
5. **Cleanup** (`clean_capture_`): splits at large gaps (>2 ms), finds clean OOK
   segments (alternating marks/spaces, ≤4 glitches, ≤9 distinct 100µs timing
   buckets, ≥16 values), **averages** the repeated segments, prepends the inter-
   repeat gap.
6. The cleaned code is dumped (ESPHome dumper) and logged (chunked, complete).
7. It's stored in `last_capture_` for `replay_last_capture`.

### RSSI Sampling (CRITICAL)
The RSSI must be sampled while the signal is actually present. The condition was
changed from `dist >= 6` to **`dist > 0`** (sample as soon as any edge arrives),
throttled to ~2 ms. Waiting for 6 edges risks `loop()` running after the brief
signal ends, reading background noise instead. The user's noise sits at ~-103 dBm,
real remotes at ~-40 dBm — a 60 dB gap that makes RSSI a reliable discriminator.

### Three-Tier Logging
- Peak RSSI ≥ floor → `RF burst captured (clean/raw)` (DEBUG) + dump + store.
- Floor > peak ≥ floor-30 → `RF signal below floor` (DEBUG, visible for tuning).
- Peak < floor-30 → `RF noise discarded` (VERBOSE, silent at DEBUG).

### Cleanup Constants
- `SPLIT = 2000` µs (split segments at gaps > 2 ms)
- `MIN_LEN = 16` values
- `MAX_GLITCH = 4` (tolerated alternation violations — CMT2300A is noisy)
- `MAX_DISTINCT = 9` (distinct 100µs timing buckets; real OOK reuses few durations,
  noise has many)
- `GAP_LO = 2000`, `GAP_HI = 20000` µs (valid inter-repeat gap range)

### The Truncation Bug (RESOLVED)
The ESPHome `Received Raw` dumper **drops the final value** on longer codes. Codes
copied from the log into YAML were truncated (65 values instead of 66), causing
saved buttons to fail while replay (full in-memory capture) worked. **Fixed** by
adding a dedicated chunked log (`ESP_LOGI`) that prints all values in groups of 12.

---

## 6. CMT2300A Demodulator Limitations

The CMT2300A's OOK demodulator is **not a precision capture device**:

1. **Systematic timing offset**: Captured mark/space durations differ from the
   original signal (~30–50 µs shift). Averaging (in cleanup) reduces random jitter
   but does NOT remove this systematic offset.

2. **Occasional bit corruption**: At certain mark/space transitions, the demodulator
   shifts the boundary by ~600 µs, truncating a long mark into a short one and
   extending the space. This produces wrong bits. This was observed on a ceiling-fan
   remote where 2 of 32 bits were consistently flipped. The resulting code is invalid.

3. **Consequence**: On-device captures are **less reliable** than Flipper captures
   for permanent buttons. They work for ad-hoc replay (if the capture happens to be
   correct) but should be verified against a Flipper capture before committing to a
   YAML button.

### Reliable Code Sources (in order of reliability)
1. **Flipper Zero `.sub` + `sub_to_esphome.py`** — faithful capture (CC1101),
   averaged by the script. Best for permanent buttons.
2. **On-device learn mode → "Captured code (...)" log** — complete (after the
   truncation fix), but may have the CMT2300A offset/corruption. Verify before use.
3. **On-device learn mode → `replay_last_capture`** — volatile (RAM only, lost on
   reboot). Works for ad-hoc use within a session.

---

## 7. The `sub_to_esphome.py` Script

Located at the project root. Converts Flipper `.sub` RAW captures into ESPHome
`transmit_raw` code arrays for the tuya_rf component.

### Usage
```bash
# From a Flipper .sub file (default: inverted polarity for tuya_rf)
./sub_to_esphome.py '*.sub'
./sub_to_esphome.py --name "Light Up" --repeat 7 Light_Up.sub

# From a tuya_rf learn-mode log dump
./sub_to_esphome.py --learn captured.txt

# Just the code array (no YAML)
./sub_to_esphome.py --format code Light_Up.sub

# Standard polarity (generic remote_transmitter, not tuya_rf)
./sub_to_esphome.py --polarity standard some.sub
```

### What it does
1. Parses the `.sub` file (or learn-mode log with `--learn`).
2. Splits at large gaps, finds clean OOK segments (start with mark, alternate).
3. Averages repeated captures position-by-position (denoise).
4. Prepends the inter-repeat gap.
5. Negates all values (for the tuya_rf component's inverted polarity, unless
   `--polarity standard`).
6. Emits a ready-to-paste YAML button snippet or code array.

### Key options
- `--polarity {inverted,standard}` — inverted (default) for tuya_rf; standard for
  generic `remote_transmitter`.
- `--learn` — input is a tuya_rf learn-mode log dump (not a `.sub` file). The
  script negates the input (learn dumps are already in component convention) to
  match the Flipper extraction, then negates back on output.
- `--repeat N` — set the repeat count in the YAML (default 5).
- `--all` — emit every distinct clean signal found (not just the dominant one).

---

## 8. Replay Mechanism

`tuya_rf.replay_last_capture` transmits the most recent captured code:
- Stores `last_capture_` (the cleaned code) and `last_capture_frequency_` on each
  successful capture.
- On replay, restores the capture's frequency (if different from current), sets
  `RemoteTransmitterBase::temp_` via `set_data()`, and calls `send_internal()`.
- Volatile: `last_capture_` is in RAM, lost on reboot.

### send_internal flow
1. `InterruptLock` (disable interrupts).
2. `StartTx()` → `RF_Init()` (writes all register banks including frequency) →
   `GoSleep → GoStby → GoTx`.
3. Leading `space_(2500)` (fixed, ~4700-2200 µs).
4. For each repeat: iterate `temp_`, call `mark_(dur)` or `space_(dur)` based on
   sign and `invert_signal_`. `mark_/space_` use a busy-loop (`await_target_time_`)
   for precise GPIO timing.
5. Trailing `space_(2000)`.
6. Return chip to RX (or standby if receiver disabled).

### Diagnostic logging (currently active)
`send_internal` logs at DEBUG: the repeat count, item count, first 6 and last 3
values of `temp_`. This was instrumental in diagnosing the truncation bug (replay
showed 66 items, saved buttons showed 65).

---

## 9. Key Lessons Learned (Debugging Journey)

### Lesson 1: Polarity inversion
The #1 gotcha. The component interprets code arrays with inverted polarity. All
codes from Flipper captures must be negated. Symptom: transmitted signal is the
mirror image of the intended signal (marks/spaces swapped).

### Lesson 2: Frequency mismatch causes garbled captures AND failed replays
If the device's `frequency:` doesn't match the remote's actual frequency (within
~50 kHz), the CMT2300A's IF filter attenuates/distorts the signal. Symptoms:
- Inconsistent captures (sometimes clean, mostly garbage).
- `RF signal below floor` with 999 timings (buffer overflow from garbled demod).
- Replay at the wrong frequency doesn't reach the receiver.
**Always set `frequency:` to the remote's exact value** (from the Flipper capture's
`Frequency:` line). The CMT2300A's IF filter is narrow (~100–300 kHz bandwidth).

### Lesson 3: RSSI must be sampled during the signal
`loop()` may not run frequently enough to catch a brief signal. Sampling RSSI only
when `dist >= 6` misses short signals. Changed to `dist > 0`. The noise floor (-103
dBm) vs real signal (-40 dBm) gap is ~60 dB — RSSI is the most reliable
signal/noise discriminator for this hardware.

### Lesson 4: Structure-based noise filtering is insufficient alone
The cleanup's distinct-timing-level check (≤9 buckets) rejects most noise, but some
noise bursts happen to have few distinct levels. RSSI gating (the floor) is needed
as the primary filter; structure checks are secondary. Both are used together.

### Lesson 5: Log truncation corrupts copied codes
The ESPHome `Received Raw` dumper drops the final value. Every code copied from the
log was one value short, causing saved buttons to fail while replay (full in-memory
data) worked. Fixed with a dedicated chunked log. **Never copy from `Received Raw`;
use the "Captured code (...)" block or the Flipper+script.**

### Lesson 6: set_frequency should not retune the receiver
Restarting the receiver (`set_receiver(false)/set_receiver(true)`) right before a
transmit degrades the TX. The frequency bank is applied by `RF_Init()` on the next
`StartTx`/`StartRx` — no manual retune needed. For runtime RX retuning (e.g.,
learning at a new frequency), the user must toggle the receiver explicitly.

### Lesson 7: On-device captures are less reliable than Flipper captures
The CMT2300A's demodulator has a timing offset and occasionally corrupts bits. For
permanent buttons, use Flipper+script. On-device captures are for convenience/ad-hoc
replay. Always verify a learned code against a Flipper capture.

### Lesson 8: The antenna limits non-433 frequencies
The SH4 module's matching network + PCB trace antenna are tuned for 433.92 MHz.
At 303/315 MHz, expect reduced TX power and RX sensitivity (several dB mismatch
loss). A 315 MHz FPC IPEX antenna or a λ/4 wire (~24.7 cm at 303 MHz) can improve
range. No external PA — the internal PA is broadband but the passive matching
network is the bottleneck.

---

## 10. Current State of the Component

### Working features
- ✅ TX at 433.92 MHz (and programmable 303/315/868/915 MHz via `set_frequency`).
- ✅ Per-button frequency selection (`tuya_rf.set_frequency` action).
- ✅ Learn mode with RSSI gating, cleanup (averaging), and complete chunked logging.
- ✅ Replay last capture.
- ✅ `invert_signal` config option.
- ✅ `raw_capture` config option (bypass cleanup for debugging).
- ✅ Diagnostic logging in `send_internal` (item count, first6/last3).

### Known limitations
- On-device captures may have CMT2300A timing offset / occasional bit corruption.
- The `Received Raw` dumper truncates (use the "Captured code" log instead).
- RX at non-433 frequencies requires the frequency to be set before capturing.
- The receiver only runs while `learn_mode` is active (capture is protocol-agnostic).
- No persistent storage for captured codes (RAM only, lost on reboot).

### Diagnostic log levels
- **DEBUG**: `RF burst captured (clean/raw): N timings, peak RSSI X dBm`
- **DEBUG**: `RF signal below floor: N timings, peak RSSI X dBm (floor Y)` (weak signal)
- **DEBUG**: `Sending remote code: N times, M items, first6 [...] last3 [...]`
- **INFO**: `Captured code (N items) -- copy into transmit_raw code:` + chunked values
- **VERBOSE**: `RF noise discarded: N timings, peak RSSI X dBm (floor Y)`
