# External start sensors ST-2–ST-4

Buildable ESP32-C3 prototype for lanes 2–4. ST-1 remains the MagTag controller and lane-one start. This target implements Flying beam starts and scheduled local Standing cues using the existing v2 wire contract. FN-2–FN-4 remain outstanding: these builds alone do not make a working four-lane system. Physical operation and timing accuracy are unverified.

## Build

Use PlatformIO 6.2.0 and the pinned Espressif 6.10.0 platform:

```sh
pio run -d firmware/start-sensor -e c3_st2
pio run -d firmware/start-sensor -e c3_st2 -t upload
pio device monitor -d firmware/start-sensor -b 115200
```

`c3_st2`, `c3_st3`, and `c3_st4` fix the lane at build time. `c3_st2_sync` disables IR drive for GPIO edge tests; **radio and countdown sound remain live**. Use a separate system ID/channel for an isolated test installation. System ID and channel must match the controller and other posts. There is no phone interface or pairing UI.

## Provisional reference-board connections

Reference board: ESP32-C3-DevKitM-1. Check another board's pinout before using these assignments.

| Function | GPIO | Interface |
| --- | --- | --- |
| IR driver | 4 | 38 kHz, 50% duty; suitable external emitter driver required |
| Beam receiver | 5 | 3.3 V compatible, active LOW for return; pull-up means disconnected = broken |
| Beam-clear LED | 3 | External LED with series resistor; binary clear indication only |
| Sound output | 6 | PWM input to a suitable external sound driver/amplifier, not a directly driven speaker |

Use a common ground and suitable regulated supplies. Do not connect a 5 V receiver output to the C3. The IR and sound PWM channels use separate LEDC timers. Sound is provisionally 1 kHz for 120 ms on each countdown beep, then 2 kHz for 400 ms at GO, matching ST-1. Speaker/driver choice, volume, optics and final PCB pins still need bench decisions. The lane/role screen and battery monitoring are not implemented; label prototype posts physically.

## Behaviour

- Bind to the first same-system ST-1 Status MAC and synchronise its clock (roughly eight seconds). Controller sessions and boot identities are checked; restart never resumes an existing attempt. Another controller or another start post claiming this lane latches a fault until reboot.
- ARM requires an enabled lane, fresh timebase, and (Flying only) a qualified clear beam. Consume rejected/cancelled attempts so delayed duplicate ARM cannot reopen them.
- Flying records the **first qualified beam edge** (20 ms clear, 200 microseconds broken, timestamp at the first edge). Wait for a timebase block after that event before conversion. Publish its immutable ST-1 timestamp in START, retrying every 100 ms until the matching Armed finish node reports Running/Complete. Later wheel/body breaks do not replace it. Finish identity changes during the attempt invalidate it.
- Standing ignores beam starts. Accept GO only before the first beep with at least 20 ms scheduling margin; convert all three beeps and GO to local time once. Duplicate GO cannot reschedule them. Install the timer schedule before advertising Cued with the original ST-1 GO timestamp.
- Cues are serviced by an `esp_timer` task every 1 ms independently of the main loop. Sound state is mutex-protected. A cue transition delayed over 20 ms or a main-loop lease expired over 100 ms silences output and latches a fault; late tones are never replayed. These are provisional fault thresholds, not measured timing accuracy. Cancel/fault/reveal stops the schedule on the next timer service.
- Status repeats every 500 ms and on state changes. Reveal stops START retries but preserves Armed acknowledgement during the controller's reveal lead-in, including DNF.
- Lost sync during an attempt, 30-second attempt expiry, controller fault, or configuration mismatch invalidates the attempt. Controller absence over three seconds resets the timebase. Receive/edge overflow or over-100-ms queued input latches a fault until restart. Recoverable invalid attempts need cancellation/idle recovery and a fresh ARM.

The controller still decides whether the whole group is ready and cancels if GO acknowledgements are missing. There is no additional global commit handshake: a post that misses cancellation may sound its scheduled cue until local expiry. Radio repeats reduce loss; they cannot guarantee delivery. Similarly, START may reach the finish but be missed by the controller; existing controller logic conservatively rejects that lane's result if it never saw START. No automatic early-start detection is provided in Standing mode.

## Verification

```sh
sh firmware/start-sensor/tests/run.sh
pio run -d firmware/start-sensor -e c3_st2 -e c3_st3 -e c3_st4 -e c3_st2_sync
```

Native tests exercise all three lane identities, interpolation, immutable retries and matching finish acknowledgements, short-pulse rejection, countdown timestamps and late-cue silence, cancellation, session/identity conflicts, reboot, timeout, overflow, disabled lanes and controller integration in both modes. Integration uses the actual controller and all three start engines with explicit simulated finish statuses; it does not substitute for implementing/testing FN-2–FN-4.

Before club use: measure shared-edge clock conversion against ST-1, beam timing and ambient-light behaviour, sound onset alignment (including ST-1), and cancellation/dropout behaviour on real radios. Bench GPIO input can generate genuine START packets. Do not claim millisecond accuracy from timestamp units or display precision.
