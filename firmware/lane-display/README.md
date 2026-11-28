# Lane result display prototype

Receive-only ESP-NOW result display for LN-1 through LN-4, with an ESP32-C3 or M5Stamp-S3 and a four-module MAX7219 32×8 FC16 matrix. This is a buildable prototype, not validated hardware. It depends on the ST-1 scheduled Reveal implementation; FN-1–FN-4 and ST-2–ST-4 prototypes are available.

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
| `c3_bench` | Isolated C3 static-pattern cycle, Wi-Fi off |
| `stamp_s3_lane1`–`stamp_s3_lane4` | M5Stamp-S3 + GroveBreakOut, live ESP-NOW, lane fixed at build time |
| `stamp_s3_bench` | M5Stamp-S3 static-pattern cycle, Wi-Fi off |

The `c3_*` reference board is **Espressif ESP32-C3-DevKitM-1**; the `stamp_s3_*` builds explicitly select **M5Stack Stamp-S3** (`m5stack-stamps3`), not an arbitrary C3 SuperMini or S3 board. Check the board, pinout and USB/upload arrangement before flashing. The final lane switch remains deferred: this prototype follows the existing target's build-time lane assignment. Restart/reflash to change the lane. System ID and radio channel must match ST-1; defaults match the controller prototype. The system ID prevents accidental mixing, not impersonation.

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

## M5Stamp-S3 + GroveBreakOut

These environments use the builder's working PlatformIO board/platform settings, USB CDC flags and software-SPI wiring:

| Matrix input | Breakout connection |
| --- | --- |
| VCC | P2 pin 7, VIN_5V |
| GND | P2 pin 6, GND |
| DIN | P2 pin 5, G9 |
| CS / LOAD | P2 pin 4, G7 |
| CLK | P2 pin 3, G5 |

The finished-unit level shifting and power considerations above still apply. Stamp-S3 brightness defaults to **10**, matching the original working test sketch; edit `JNS_MATRIX_INTENSITY` in `[stamp_s3]` for 0–15. C3 brightness and pins retain their existing defaults.

In VS Code, open `firmware/lane-display` as the PlatformIO project. Under **Project Tasks**, choose **stamp_s3_bench → General → Upload** first. Alternatively, from the repository root:

```sh
pio run -d firmware/lane-display -e stamp_s3_bench -t upload
pio device monitor -d firmware/lane-display -b 115200
```

This runs the existing radio-free demo using the project's actual font: TEST, 01.750, 12.345, 30.000, DNF, ERR and ----, three seconds each at fixed brightness, then a revealed time with its scrolling speed. It differs from the separate brightness-sweep sketch. No serial connection is required for battery operation.

For live lane 1, select **stamp_s3_lane1** and upload, or run:

```sh
pio run -d firmware/lane-display -e stamp_s3_lane1 -t upload
```

Use the corresponding environment for lanes 2–4. The project's default remains `c3_lane1`, so explicitly select the Stamp-S3 environment before uploading. Live firmware shows `----` until controller contact; it will not generate sample results. ST-1 and the finish sensor must share the system ID/channel. The MagTag can remain absent while using `stamp_s3_bench`.

The same state machine and renderer serve both board families. MD_Parola is unnecessary here: the firmware draws through pinned MD_MAX72XX directly, including its compact numerical font. The builder has confirmed the separate Parola readability sketch on USB-C and battery behind smoked acrylic; that does not yet validate this firmware's font, radio, synchronisation or physical reveal timing. Check those with the bench and live builds respectively.

Breakout reference: [M5Stack Stamp-S3 GroveBreakOut](https://docs.m5stack.com/en/accessory/Stamp-S3_GroveBreakOut).

## What it displays

| Output | Meaning |
| --- | --- |
| `IDLE` | Controller heard, no attempt adopted |
| `SYNC` | Attempt pending while timebase acquires samples |
| `----` | Awaiting reveal, or controller offline; never a numerical result |
| `01.750` / `12.345` | This lane's result, rounded to milliseconds, always with two digits of seconds |
| `54.0 KM/H` (scrolling) | Average speed for that time over ST-1's distance setting; scrolls across between 4-second holds of the time |
| `DNF` | This lane's bit is clear in the Reveal finished mask |
| `ERR` | Invalid/cancelled attempt, missed data or unusable reveal schedule |
| `OFF` | This lane is disabled |
| `FAIL` | Latched identity conflict, receive backlog/overflow, or hardware setup failure; restart after investigating |

No running clock, ranking or rep-number alternation. Five-pixel numeric glyphs and a one-column decimal fit `30.000` in 31 columns. The whole frame updates when content changes.

After a reveal the time is held still for 4 seconds, then the speed scrolls in from the right and off the left at 25 columns a second, and the cycle repeats until the next attempt. The speed is worked out on the display from the time it shows and the distance ST-1 sends in its Reveal (wire v4), so the two always agree. If ST-1 has no distance, only the time is shown. On a Standing start the speed is an average from a standstill, not a top speed.

The LED colour is a property of the MAX7219 module, not the firmware. FC16 modules are sold in red, green, blue, yellow and white; JNS SPLIT's lane colours are lane 1 orange-red (`#ff5a36`), lane 2 blue (`#3b82f6`), lane 3 yellow (`#ffc83d`) and lane 4 teal (`#22c3a6`). Fit the closest module colour to each lane. Rendering uses software SPI and loop scheduling; simultaneous physical LED onset is not yet measured.

The radio-free bench build cycles `TEST`, `01.750`, `12.345`, `30.000`, `DNF`, `ERR` and `----` every three seconds, then shows a revealed `02.004` with its 30 m speed scrolling, for 12 seconds. It is for orientation, power and outdoor visibility tests; its numbers are demonstrations, not measured times. It never sends or receives application results.

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
pio run -d firmware/lane-display -e stamp_s3_lane1 -e stamp_s3_lane2 -e stamp_s3_lane3 -e stamp_s3_lane4 -e stamp_s3_bench
```

Native tests cover lane/system/session/attempt filtering, finish identity, duplicate messages, controller reboot, cancellation, expiry, missing Results versus DNF, reveal scheduling, faults and number formatting. A wire-encoded simulation feeds four displays with different local clock offsets and different surviving Reveal copies; it checks common reveal timing and separate missing-data/DNF states. This does not model or validate real radio/sensor latency.

Before track use: verify the bench font/orientation (especially decimal and `30.000`), level shifting, power stability and visibility. Then test with an actual controller/finish node, all four lanes, missed packets, reset during the lead-in and controller power loss. No hardware has been flashed or physically verified as part of this implementation.

## Baseline v3 update

Reflash the entire installation for protocol v3. Controller Retired heartbeats recover lost CANCEL messages and permanently retire that attempt. Clock conversion now waits for retained fit samples on both sides of an event; conversion latency can exceed the old block-close estimate. Per-device measured delay compensation is available through `JNS_TIMEBASE_DELAY_US`; zero remains uncalibrated. See [baseline validation](../../docs/baseline-validation.md).


## ESP32-C6 / WS2812B build

`c6_lane1`–`c6_lane4` and `c6_bench` target the **DFRobot FireBeetle 2 ESP32-C6** (4 MB flash) driving an **8×32 WS2812B** panel. ESP32-C6 needs Arduino core 3.x, so these environments pin the pioarduino 51.03.07 platform; the other targets stay on espressif32 6.10.0.

| Setting | Default | Notes |
| --- | --- | --- |
| Panel data | GPIO 6 (`JNS_MATRIX_DIN`) | Provisional. 300–500 Ω series resistor; 3.3 V data may need a 74AHCT125 |
| Brightness | 16/255 (`JNS_MATRIX_BRIGHTNESS`, capped at 64) | Power the panel from its own 5 V supply with a common ground, never from the board |
| Layout | Serpentine columns (`JNS_MATRIX_SERPENTINE`, `JNS_MATRIX_FLIP_X`, `JNS_MATRIX_FLIP_Y`) | Adjust if text is mirrored, inverted or scrambled |
| Status LED | GPIO 15 (`JNS_STATUS_LED`) | Lit while synchronised to ST-1; useful without a panel |

Text is amber and uses a built-in 5×7 font, including the scrolling `KM/H` speed. On Windows, esptool's final reset may report a serial error after every "Hash of data verified" line; the image is written, but press RST if the board does not restart. If Smart App Control blocks the RISC-V toolchain locally, the **ESP32-C6 images** workflow publishes a merged image per environment; flash it at `0x0` with esptool.

First hardware check (15–18 November 2026): LN-1 bound to a live MagTag ST-1 over ESP-NOW, synchronised (status LED) and showed `IDLE` on the panel. Digits, layout and reveal timing are not yet verified.
