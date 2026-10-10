# Lane result display prototype

Receive-only ESP-NOW result display for LN-1 through LN-4, with an ESP32-C3 and a four-module MAX7219 32×8 FC16 matrix. This is a buildable prototype, not validated hardware. It depends on the ST-1 scheduled Reveal implementation; start/finish sensor firmware is still outstanding.

`include/lane_display.hpp` is hardware-independent state logic using the shared wire decoder/timebase. `src/main.cpp` provides the C3 radio and matrix adapter. Alternative digit hardware can replace the adapter without changing result handling.

## Build and board choice

Arduino C++ / PlatformIO 6.2.0; dependencies are pinned in `platformio.ini`.

```sh
pio run -d firmware/lane-display -e c3_lane1
pio run -d firmware/lane-display -e c3_lane1 -t upload
pio device monitor -d firmware/lane-display -b 115200
```

| Environment | Behaviour |
| --- | --- |
| `c3_lane1`–`c3_lane4` | Live ESP-NOW, lane fixed at build time |
| `c3_bench` | Isolated static-pattern cycle, Wi-Fi off |

The reference board is **Espressif ESP32-C3-DevKitM-1**, not an arbitrary C3 SuperMini or S3 board. Check the board, pinout and USB/upload arrangement before flashing. The final lane switch remains deferred: this prototype follows the existing target's build-time lane assignment. Restart/reflash to change the lane. System ID and radio channel must match ST-1; defaults match the controller prototype. The system ID prevents accidental mixing, not impersonation.

## Prototype wiring

| Matrix input | Reference C3 GPIO | Connection |
| --- | --- | --- |
| DIN | 6 | Through 3.3 V → 5 V logic buffer |
| CLK | 4 | Through buffer |
| CS / LOAD | 7 | Through buffer |
| VCC | — | Regulated 5 V supply |
| GND | — | Common supply, buffer and C3 ground |

Use a 5 V-powered 74AHCT125 or equivalent TTL-input buffer; enable its used channels and terminate unused inputs as its datasheet requires. Do not power the matrix from the board's 3.3 V regulator. Follow the board's power guidance and avoid backfeeding USB from another supply. These pins are an explicit prototype allocation and may be overridden in the build flags; do not copy them blindly to another board.

The MAX7219's guaranteed input-high threshold is 3.5 V, so direct ESP32 GPIO drive at 3.3 V is not guaranteed. Power capacity, decoupling, brightness, battery life and outdoor readability still need measurement. `JNS_MATRIX_INTENSITY` is 0–15 (default 4). `FC16_HW` is selected in the adapter: check actual panel orientation/type before use.

References: [C3 board guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c3/esp32-c3-devkitm-1/user_guide.html), [MAX7219 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/max7219-max7221.pdf), [MD_MAX72XX](https://github.com/MajicDesigns/MD_MAX72XX).

## What it displays

| Output | Meaning |
| --- | --- |
| `IDLE` | Controller heard, no attempt adopted |
| `SYNC` | Attempt pending while timebase acquires samples |
| `----` | Awaiting reveal, or controller offline; never a numerical result |
| `1.750` / `12.345` | This lane's result, rounded to milliseconds |
| `DNF` | This lane's bit is clear in the Reveal finished mask |
| `ERR` | Invalid/cancelled attempt, missed data or unusable reveal schedule |
| `OFF` | This lane is disabled |
| `FAIL` | Latched identity conflict, receive backlog/overflow, or hardware setup failure; restart after investigating |

No running clock, ranking or rep-number alternation. Five-pixel numeric glyphs and a one-column decimal fit `30.000` in 31 columns. The whole frame updates when content changes. Rendering uses software SPI and loop scheduling; simultaneous physical LED onset is not yet measured.

The radio-free bench build cycles `TEST`, `1.750`, `12.345`, `30.000`, `DNF`, `ERR` and `----` every three seconds. It is for orientation, power and outdoor visibility tests; its numbers are demonstrations, not measured times. It never sends or receives application results.

## Result and fault handling

- Bind to the first valid same-system controller Status MAC. Another same-system controller Status locks the display to `FAIL` until restart. Rebooted controller sessions clear output and rebuild clock sync. Retired sessions cannot be readopted; after eight retired sessions a display restart is required to bound memory safely.
- Track heartbeats using the shared block-minimum/drift fit (about eight seconds to establish sync). Capture local time first in the receive callback; queue records and keep all state/timebase mutation in the main loop.
- A newer controller Status or ARM clears old output and establishes attempt identity. A CANCEL also retires an attempt even if its ARM was missed. Duplicate ARM and old attempts cannot reopen terminal output. Controller Fault and configuration mismatches invalidate the matching attempt.
- Only accept Results from this lane's finish MAC/boot identity, announced by a matching Armed Status within three seconds. Conflicting finish identities or reboots invalidate the attempt. The first valid elapsed value is immutable; accepted values are 1–30,000,000 microseconds.
- Hold the Result until Reveal. Convert the scheduled ST-1 time through the timebase; require a fresh fit and a future deadline no more than two seconds away. Duplicate schedules must agree. At the deadline show time, DNF, or ERR if the mask says finished but the Result was missed. A Result may arrive after Reveal but must arrive before the deadline; no late Result changes the revealed output.
- Three seconds without a fresh controller heartbeat clears the output and timebase. Repeated Results cannot keep a stale number alive. The expired attempt remains retired even when heartbeat reception resumes; a newer attempt can recover.
- Pending attempts with no Reveal expire after 32 seconds (local adoption time). Lost sync during a scheduled reveal invalidates it. Queue overflow or a valid same-system packet queued for more than 100 ms locks `FAIL`, since a lost control packet could otherwise leave misleading output.

Broadcast delivery is not guaranteed. At least one valid Reveal must arrive before its deadline; a missed cancellation cannot guarantee immediate clearing at every unit. Firmware does not announce display presence or acknowledge results. Display resolution does not establish timing accuracy.

## Verification

```sh
sh firmware/lane-display/tests/run.sh
pio run -d firmware/lane-display -e c3_lane1 -e c3_lane2 -e c3_lane3 -e c3_lane4 -e c3_bench
```

Native tests cover lane/system/session/attempt filtering, finish identity, duplicate messages, controller reboot, cancellation, expiry, missing Results versus DNF, reveal scheduling, faults and number formatting. A wire-encoded simulation feeds four displays with different local clock offsets and different surviving Reveal copies; it checks common reveal timing and separate missing-data/DNF states. This does not model or validate real radio/sensor latency.

Before track use: verify the bench font/orientation (especially decimal and `30.000`), level shifting, power stability and visibility. Then test with an actual controller/finish node, all four lanes, missed packets, reset during the lead-in and controller power loss. No hardware has been flashed or physically verified as part of this implementation.
