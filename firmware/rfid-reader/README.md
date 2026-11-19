# JNS SPLIT reader bench — Core2 + Unit RFID2

This is a standalone test target. It does not transmit ESP-NOW, alter timing firmware, write to tags or claim a rider is checked in. Its screen says **Tag read - not checked in**. No success beep is emitted because the controller has not acknowledged an assignment.

## Hardware / VS Code

- M5Stack Core2, powered by USB.
- Unit RFID2 (U031-B) on red **Port A**, with its supplied Grove cable.
- Core2 external I2C: SDA GPIO32, SCL GPIO33, reader address 0x28. Internal peripherals use different pins; do not substitute generic ESP32 GPIO21/22 wiring.
- NTAG213/215/216 or compatible MIFARE Type A tags. This probe reads full UIDs, including leading zeros; it does not reject NTAG just because it is not MIFARE Classic.

Install the PlatformIO extension in VS Code. Open **this firmware/rfid-reader folder** as the PlatformIO project (or add it to a multi-root workspace). Select `core2_bench` and Build, then Upload. Monitor at 115200 baud.

CLI equivalent, from the repository root:

```sh
pio run -d firmware/rfid-reader
pio run -d firmware/rfid-reader -t upload
pio device monitor -b 115200
```

Default lane is 1. Change `JNS_READER_LANE` in platformio.ini for later lane tests. This target is specifically Core2; do not flash to CoreS3/Stamp without a board-specific port and build target.

## Test sequence

1. Expect 'Waiting for tag'. If absent, check Port A and cable; missing reader is retried automatically.
2. Present a tag: screen shows UID and Serial Monitor emits one JSON line.
3. Hold it for 10 seconds: no repeated presentations should be emitted.
4. Remove for at least 500ms and tap again: new request ID, same UID.
5. Try unknown tags, different form factors and enclosure placement.
6. Unplug the reader: a fault should appear. Reconnect and verify recovery.
7. Restart Core2: request boot prefix changes; reader ID remains stable.

The absence detector polls at 100ms and requires 500ms of confirmed timeouts. I2C failures/collisions do not count as absence. Weak RF at the edge of range can still look absent; validate the threshold physically. Present one tag at a time.

## Web app

The separate `jns-split-web` app can consume these scans under Equipment. Use desktop Chrome/Edge at localhost or HTTPS. Close Serial Monitor before connecting the browser. USB is read-only: simulator acceptance never sends a controller acknowledgement to the Core2. Select one enabled lane for the first physical test, or complete other lanes with simulated scans.

Alternatively paste a JSON scan line. Enrol the UID against a test rider, remove the tag, then tap again. Refreshing the browser clears simulator data. Never use real child records in this milestone.

See [contract](../../docs/reader-bench-contract.md) and [current development scope](../../docs/development-status.md).

## Checks

`sh firmware/rfid-reader/tests/run.sh` runs a native C++ presentation test with undefined-behaviour sanitisation. It requires a C++17 compiler; PlatformIO compilation checks the actual embedded dependencies. No board was connected during authoring, so flashing, RF reads and power consumption still need physical verification.

## Dependencies and references

- [Core2 pin map](https://docs.m5stack.com/en/core/core2)
- [Unit RFID2](https://docs.m5stack.com/en/unit/Unit_RFID2)
- [M5 RFID tutorial](https://docs.m5stack.com/en/arduino/projects/unit/unit_rfid)
- M5Unified 0.2.8 and M5GFX 0.2.11 (their upstream licences apply).
- MFRC522_I2C pinned to upstream commit `8152dddc93cf743397ac225e34bf268698326664` from https://github.com/kkloesener/MFRC522_I2C; upstream files declare public-domain release. It is fetched as a dependency, not copied from Sportiduino.

Long-term reader power target: a season of defined club use; annual AA battery replacement is a stretch goal requiring measurement. This screen-equipped Core2 bench target is not that low-power design.
