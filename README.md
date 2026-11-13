# JNS LaneTime

Standalone ESP-NOW timing system for 1–4 BMX lanes, supporting standing and flying starts, coach-operated controls, and per-lane time displays.

## What it is for

Club sprint training over short distances (around 30 m or less). Up to four riders go side by side, each lane timed independently, so a coach can compare efforts rep by rep without a stopwatch.

- **Flying:** riders roll in at speed. Each lane's clock starts when its rider breaks the start beam and stops at the finish beam.
- **Standing:** riders start from a standstill on a shared countdown. Every start post sounds the same cue at the same instant, and every lane's clock starts at GO.

It was built for a local club as an affordable, quick-to-deploy alternative to commercial multi-lane timing systems, with no subscription. The current timing prototype has no rider profiles or phone pairing; ordinary sensors use fixed lane and role identities. RFID check-in, rider-linked session records and independent free-ride operation are planned extensions.

## How it is laid out

Each timing line is a row of posts, one between each pair of lanes. A powered post emits a beam across the lane on one side and carries the reflector for the lane on its other side. A passive end-cap closes the line. N lanes need N powered posts plus one end-cap per line.

```
          lane 1     lane 2     lane 3     lane 4
FINISH  [FN-1] ───▶ ▮[FN-2] ───▶ ▮[FN-3] ───▶ ▮[FN-4] ───▶ ▮[FN-5]
             ↑          ↑          ↑          ↑
             │  riders travel up the page     │
             │          │          │          │
START   [ST-1] ───▶ ▮[ST-2] ───▶ ▮[ST-3] ───▶ ▮[ST-4] ───▶ ▮[ST-5]

───▶  940 nm IR beam, retroreflected back to its source
▮     reflector face        ST-5 / FN-5  passive end-caps (no electronics)
```

The result displays (LN-1 to LN-4) stand beyond the finish line, facing the riders and coach.

| Name | Unit |
| --- | --- |
| ST-1 | Main controller and lane 1 start post: coach buttons, setup screen, countdown |
| ST-2–ST-4 | Start posts for lanes 2–4; sound the Standing cue |
| FN-1–FN-4 | Finish posts; each times its own lane |
| ST-5 / FN-5 | Passive reflector end-caps, not a fifth lane |
| LN-1–LN-4 | Lane result displays |

## Timing model

ST-1's clock is the common timebase. Its 500 ms heartbeat carries its own timestamp, and every other unit continuously estimates its offset from it. Beam events and the Standing GO are exchanged as timestamps in that timebase, so START/Result delivery time is not used as the measured event time. Heartbeat delay still biases clock estimation, and packet loss can invalidate an attempt. Sync error, drift and sensing latency still need measuring; no timing accuracy is claimed yet.

**Firmware upgrade:** the baseline now uses wire version 3. Reflash controller, sensors, displays and bench boards together; mixed v2/v3 installations cannot arm. See [baseline hardening and calibration](docs/baseline-validation.md).

## Project status

Prototype for local club use. The first [ST-1 MagTag firmware](firmware/controller/) is implemented, with live-radio and isolated bench builds, scheduled result reveal and a hold-to-reveal control. An [ESP32-C3/MAX7219 lane-display prototype](firmware/lane-display/) now supports scheduled reveal and a radio-free readability demo. An [FN-1 finish-sensor prototype](firmware/finish-sensor/) now completes the single-lane software path. [ST-2–ST-4 start-sensor prototypes](firmware/start-sensor/) now provide Flying starts and local Standing cues. FN-2–FN-4 are now included in the common finish target; spare firmware remains to be built. The device images are concept mock-ups, not manufacturing drawings or evidence of tested hardware. Physical MagTag operation has not been validated.

The first build can use development boards with integrated screens and buttons, plus 3D-printed enclosures. The builder has a Bambu Lab P1S and Fusion 360.

## Further reading

- The [build specification](docs/build-specification.md) records agreed requirements, proposals and open choices.
- The [RFID, session modes and results working design](docs/rfid-sessions-and-results.md) records the planned extension and outstanding decisions.
- The [firmware overview](firmware/README.md) maps devices to targets.
- The [mock-up gallery](images/README.md) records the current physical design direction.

| Folder | Contents |
| --- | --- |
| `docs/` | Build specification and design decisions |
| `hardware/` | Future parts lists, wiring and enclosure designs |
| `images/` | Current device concept mock-ups |
| `firmware/` | Five planned firmware targets and shared modules |

ST-1 uses Arduino C++ and PlatformIO on the MagTag ESP32-S2. Native tests, the three controller builds, ten C3/Stamp-S3 display builds, six finish-sensor builds and four external-start builds run in GitHub Actions. No dependency on JNS_Timing code. Cruiser bikes are excluded; phone control and a handheld remote are deferred.
