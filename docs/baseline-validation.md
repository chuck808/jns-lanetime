# Baseline hardening and hardware validation

13 November 2026. RFID, rider records, free-ride and spare configuration remain outside this change.

## Upgrade

Wire version 3 keeps the 40-byte layout but adds controller Status Retired=64. Reflash all controller, start, finish, display and bench boards together. Version 2 nodes are deliberately incompatible, avoiding a mixed installation that silently lacks retirement recovery.

Cancellation remains repeated for two seconds and is then recoverable from every idle heartbeat. Preparing is distinct from retired idle. A newer attempt can arm normally; duplicate ARM cannot revive a retired attempt. Fault handling remains terminal.

## Timing correction

One-way heartbeat timing cannot distinguish clock offset from propagation, transmission and callback latency. A quiet, low-spread fit can still be biased. Neither three decimal places nor passing simulation tests establish physical accuracy.

For an uncalibrated unit with minimum delay d, converted event timestamps are approximately true ST-1 time minus d. External Flying elapsed error is approximately d_start minus d_finish. Lane-one Flying and Standing elapsed error is approximately minus d_finish. Scheduled external cues are also delayed by that unit's estimated bias relative to ST-1.

The optional build flag `-DJNS_TIMEBASE_DELAY_US=<measured integer>` adds measured delay to local-to-ST-1 conversion and applies the inverse correction to scheduling. Default zero means no correction. Configure each physical unit separately; start, finish and display hardware may differ. Resetting clock history preserves the compiled correction. This compensates an observed stable bias, not variable link latency or sensing delay.

1. Begin uncalibrated. Use the common-edge bench rig and retain A-ST1, B-ST1, A-B, readiness, spread and dropped-sample observations. The reference is stamped just before the output GPIO transition, so GPIO/ISR latency is part of the measurement.
2. Repeat on the actual board types, firmware workloads, supply arrangements and operating distances. A three-Stamp-S3 result alone is not calibration for MagTag/C3 hardware.
3. Investigate large or unstable bias. Only if repeatable, use the measured positive correction (approximately negative unit-minus-reference mean) for that unit. Do not use the synthetic 800 µs test fixture as a field setting.
4. Repeat the measurement after correction, including direct ST-1 start versus each finish and external start versus its finish. Exercise both start modes with known edge separations. Record accuracy and repeatability against an independently chosen coaching target.
5. Measure optical and sound-onset delays separately. Firmware clock compensation is not a substitute for these measurements.

Event conversion now requires real retained fit points on both sides. It can take longer than one block; rejected blocks can delay it further. If the fit window no longer brackets an event, it is not silently extrapolated into a result; normal attempt expiry remains the backstop.

## Regression and bench checks

Run all five `firmware/*/tests/run.sh` scripts (six native executables). They cover retirement recovery before and after reveal, same-attempt replay, fresh rearm, v2 rejection, invalid retirement encodings, actual fit bracketing, synthetic bias correction, cue lateness/lease expiry, and the existing four-lane flows.

Build every PlatformIO environment through the four GitHub workflows. On hardware, verify CANCEL during countdown, loop stalls and delayed cue callbacks, all CANCEL copies lost while Status survives, controller power loss, early/automatic reveal, and rearm after retirement. ST-1 and external starts share the cue engine, but physical onset alignment still needs measurement.

No firmware can guarantee instant cancellation when all radio control is lost. Surviving Retired/Fault heartbeats stop activity; complete loss is bounded by local freshness/expiry checks. Receive-only displays cannot acknowledge delivery.
