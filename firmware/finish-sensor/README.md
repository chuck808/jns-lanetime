# FN-1–FN-4 finish-sensor prototypes

Firmware for the DFRobot Beetle ESP32-C6 (DFR1117), and the earlier ESP32-C3-DevKitM-1 reference board, with a 38 kHz IR driver, active-low receiver and a status NeoPixel on the C6. Captures qualified finish edges, computes an elapsed timestamp difference in ST-1 time and repeats Result for the controller and lane display. Lane 1 accepts START from ST-1; lanes 2–4 discover and bind their corresponding external start post. All lanes receive Standing GO from ST-1. This completes the software timing path for four lanes; physical timing remains unvalidated.

One common target supports all four lanes, selected by build environment. FN-1 differs physically (no opposite-face reflector), not in its optical timing algorithm.

## Build

Arduino C++ / PlatformIO 6.2.0, Espressif platform pinned to 6.10.0.

```sh
pio run -d firmware/finish-sensor -e c6_fn1
pio run -d firmware/finish-sensor -e c6_fn1 -t upload
pio device monitor -d firmware/finish-sensor -b 115200
```

| Environment | Board | Behaviour |
| --- | --- | --- |
| `c6_fn1`–`c6_fn4` | Beetle ESP32-C6 | Live ESP-NOW, 38 kHz optical drive and status NeoPixel, lane fixed at build time |
| `c6_fn1_sync` | Beetle ESP32-C6 | Live ESP-NOW, optical drive disabled; accepts bench GPIO edges |
| `c3_fn1`–`c3_fn4` | ESP32-C3-DevKitM-1 | Live ESP-NOW and 38 kHz optical drive, lane fixed at build time |
| `c3_fn1_sync`, `c3_fn2_sync` | ESP32-C3-DevKitM-1 | Live ESP-NOW, optical drive disabled; accepts bench GPIO edges |

The ESP32-C6 needs Arduino-ESP32 3.x. PlatformIO's own `espressif32` platform stops at 2.0.17, so the C6 environments use [pioarduino](https://github.com/pioarduino/platform-espressif32), the community-maintained platform, pinned to release 55.03.313 (Arduino-ESP32 3.3.13). The first C6 build downloads that platform and its toolchain. The C3 environments are unchanged, and the source builds on both cores. ESP-NOW between the C6 and the ESP32-S2/C3 units is unaffected: the packets are well under the original 250-byte limit.

The C6 boards are flashed over their USB-C port. If the port does not appear, hold BOOT (GPIO 9), press and release RST, release BOOT and retry.

**The sync bench is not radio-isolated.** Use a separate test system ID/channel if another club set is nearby. It can generate genuine protocol Results from electrically injected edges. Unlike the lane display's readability demo, it does not generate sample times by itself.

Set the same `JNS_SYSTEM_ID` and `JNS_RADIO_CHANNEL` as ST-1 and the lane displays. Defaults match their prototypes. Choose the environment for the physically labelled lane; changing the lane requires reflashing. A nonzero random boot token changes on every reboot. No attempt resumes after restart; the controller must cancel/rearm.

## Beetle ESP32-C6 wiring

| Function | Beetle pin | Notes |
| --- | --- | --- |
| IR emitter signal (In) | GPIO 6 | 38 kHz, 50% duty, hardware LEDC. Signal only |
| IR emitter V+ | 5 V rail from the boost module | **Not** the Beetle's 3V3: the emitter draws about 400 mA when pulsing at 5 V |
| Receiver signal | GPIO 7 | Active LOW while the beam return is detected; pull-up makes disconnection read broken |
| Receiver power | 3V3 | Must be 3.3 V, so its output never exceeds the C6's input limit |
| Status NeoPixel data | GPIO 22 | See *Status light* below |
| Status NeoPixel power | 3V3 | The C6's 3.3 V data is too low for a pixel run at 5 V to read reliably |
| Board power | VIN (5 V) from the boost module | |
| Ground | GND | One common ground for the boost module, emitter, receiver, pixel and board |
| Onboard LED | GPIO 15 | Lit while the beam is qualified clear |

Pins avoided on purpose: GPIO 4, 5, 8, 9 and 15 are the C6's strapping pins (read at power-on), and GPIO 16/17 carry the boot log. GPIO 0 is wired to the board's battery-voltage divider. GPIO 19–21 and 23 remain free for later use. Board pin numbers are from DFRobot's schematic and Espressif's Arduino board definition for the Beetle ESP32-C6.

Fit a 220–470 µF capacitor across the emitter's V+ and GND, at the emitter. Join every ground. On the bench, use either USB-C or the boost module to power the board rather than both at once, unless you have checked how this board's VIN pin is joined to its USB supply.

If the NeoPixel is an RGBW part (SK6812 RGBW, as on some Adafruit button boards), set `-DJNS_PIXEL_RGBW=1` in the `[c6]` section of `platformio.ini`; otherwise the colours will be wrong. `-DJNS_PIXEL_BRIGHTNESS` (default 48 of 255) sets its brightness.

### Status light

The pixel shows alignment first while idle, because that is when someone is at the post adjusting it.

| Pixel | Meaning |
| --- | --- |
| Red, flashing fast | Latched fault: restart FN |
| Red | No beam: aim the emitter at the reflector |
| Amber | Beam dropping out (two or more breaks within a second) or just settling: fine-tune the aim |
| Green, flashing slowly | Beam good; waiting to be in step with ST-1 |
| Green | Ready |
| Blue | Armed, waiting for the rider |
| White | Time captured |

The TSSP receiver only reports beam or no beam, so the light cannot show signal strength. Amber is the only hint of a marginal beam: one near its limit keeps dropping out. Aim for a steady green, then check it stays green when you nudge the emitter slightly either way. The one-second window is provisional, to be tuned on the real optics.

## C3 reference wiring

| Function | Reference C3 GPIO | Notes |
| --- | --- | --- |
| IR modulation output | 4 | 38 kHz, 50% duty, hardware LEDC; driver input only |
| Receiver input | 5 | Active LOW while beam return is detected; pull-up makes disconnection read broken |
| Beam-clear indicator | 3 | External LED with suitable series resistor; HIGH means qualified clear |
| Ground | GND | Common reference for optics, controller and bench signal source |

These are reference-board allocations, not a final PCB pinout. Supply compatible optics from a properly rated regulated 3.3 V rail. **Do not connect a 5 V receiver output to the C3 or drive an IR emitter directly from a GPIO.** Use a suitable emitter driver/MOSFET and independently verify peak current, supply capability and receiver polarity. This follows ST-1's TSSP77038/38 kHz prototype assumptions; continuous-carrier compatibility and optics must be checked on the assembled circuit.

The indicator reports a binary qualified beam-clear condition, not optical strength or margin. Physical power controls, battery monitoring and finish-post piezo indications are not implemented. A persistent printed FN-x/LANE x label is sufficient for this bench prototype.

Reference board: [ESP32-C3-DevKitM-1 guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c3/esp32-c3-devkitm-1/user_guide.html). Other C3 boards require a pin/power review before flashing.

## Timing and state behaviour

- The shared timebase consumes ST-1 Status timestamps captured at the start of the radio callback. About eight seconds of heartbeats establish the initial fit. Beam qualification now lives in shared code, retaining ST-1's provisional 20 ms clear / 200 µs broken thresholds.
- Each finish post advertises Status every 500 ms and on state changes. Ready requires a fresh timebase and clear finish beam in **both** modes. ARM is accepted only for a newer attempt; duplicate ARM cannot clear captured events. A rejected/not-ready ARM is retired and needs a fresh attempt.
- Edge interrupts and radio callbacks feed a bounded queue; the main loop owns the timebase, beam and attempt state. Qualified breaks retain the first edge timestamp. A hub-height beam sees one rider as a burst of breaks (tyre, spokes, hub, frame, legs, rear wheel), so only the first break of each burst is stored; a burst ends after 0.5 s with no breaks (provisional). Store up to eight bursts, including events received before START. Overflow invalidates the attempt instead of choosing an uncertain edge.
- Flying on lane 1 accepts START only from the bound ST-1 MAC/session/boot identity. Lanes 2–4 require their matching start post's fresh same-session Status before arming, including matching mode/mask and Ready. A START must match its MAC, boot, lane and attempt, with a current Armed Status no more than three seconds old. Controller START packets cannot start these lanes. The first timestamp is immutable. A provisional 10 ms future tolerance accommodates one-way clock-sync bias; it is not an accuracy guarantee.
- Standing accepts a future GO no more than five seconds ahead, echoes its timestamp with Cued, and enters Running at that time. Finish posts do not sound the Standing countdown. Pre-GO beam breaks are discarded when selecting the finish.
- Once the fit has closed a block after an event, convert it to ST-1 time. The finish is the **first buffered break strictly after START/GO**. Later rider/frame/rear-wheel breaks cannot replace it. Interpolation adds result latency; packet arrival time is never used as the measured start or finish.
- After completion, keep Armed/Complete in Status and repeat the immutable Result every 500 ms. Busy or failed send submissions retry without changing the elapsed time. Standing Status retains the Cued/GO echo through completion.
- Reveal seals the attempt against further finish capture. An unfinished finish post keeps an Armed acknowledgement without reporting a timing fault, so an early DNF reveal is not aborted by ST-1. If the lane is excluded from the finished mask, suppress its Result retries.
- CANCEL retires its attempt even if ARM was missed. Controller faults, incompatible configuration, loss of sync and a 30 s unfinished-attempt deadline invalidate timing. Three seconds without controller Status clears clock history and retires the attempt. A controller CANCEL/idle status allows recovery to readiness after the beam and clock are usable; the same attempt cannot reopen.
- Bind to the first same-system controller MAC. A second controller or second finish Status claiming this lane, or inconsistent controller boot identity, causes a latched fault requiring restart. Queue overflow or an over-100 ms queue backlog discards the backlog, resynchronises the beam from the receiver level and invalidates the current attempt only; the unit recovers when ST-1 cancels or sends its Retired heartbeat. Retired controller sessions cannot be readopted; the last eight are remembered and the oldest is forgotten, so ST-1 can be rebooted any number of times.

For lanes 2–4, start-post expiry, active-attempt reboot, fault or incompatible configuration invalidates timing and suppresses Result. A fresh second start identity for the lane latches a conflict; after an inactive old peer expires, a replacement can be discovered. Wrong-session/old-attempt announcements cannot refresh an active binding. Standing still requires the matching start post for its local countdown cue.

No ranging, receiver-margin estimation, app, or measured accuracy claim is included. Broadcast is unencrypted: system ID filters accidental mixing only.

## Verification and first bench use

```sh
sh firmware/finish-sensor/tests/run.sh
pio run -d firmware/finish-sensor -e c6_fn1 -e c6_fn2 -e c6_fn3 -e c6_fn4 -e c6_fn1_sync -e c3_fn1 -e c3_fn2 -e c3_fn3 -e c3_fn4 -e c3_fn1_sync -e c3_fn2_sync
```

Native tests cover the status light and unsteady-beam detection, Flying and Standing, finish-before-START delivery, countdown stray breaks, short glitches, immutable duplicates, source/session/attempt filtering, cancellation, no-resume reboot handling, expiry, overflow, DNF reveal, and rearm. A wire-encoded integration simulation drives the real controller, finish and lane-display state machines through a single-lane start, finish, Result and simultaneous reveal. The additional four-lane simulation uses the actual controller, all three external start engines, four finish engines and four displays with different clock offsets. It tests staggered Flying starts, Standing GO, START delayed until after a finish, lost START/Reveal copies, partial DNF, missing display data, cancellation and peer identity/expiry/rearm checks. These prove interaction under the simulated conditions, not physical timing accuracy.

For the GPIO bench, provide a clean 3.3 V-compatible active-low signal on the receiver input (GPIO 7 on the C6, GPIO 5 on the C3) (LOW=clear, HIGH=broken), with common ground. Do not connect two push-pull outputs together. Wait for sync and qualified clear; select one lane on ST-1 and arm. For Flying, trigger ST-1's start input before the finish edge. For Standing, wait for the scheduled GO before the finish edge. Check the reported value and delayed reveal on LN-1.

For lanes 2–4, also power the corresponding ST-x, wait for discovery/sync, and enable the contiguous lane count on ST-1. Test all four lanes with their own start/finish inputs and confirm lane routing.

Measure against a common reference before optics/track use. One-way minimum radio delay does not necessarily cancel for ST-1's direct START versus FN-1's converted finish; board/receiver/capture delays need measurement. Compare repeated edge separations, then actual beam interruptions. Exercise cancellation, dropped messages, controller reboot and power loss. FN-1 has been flashed (C3 build) and its clock sync with ST-1 works on the bench. An earlier C6 build (receiver on GPIO 6, no status pixel) completed a Standing run on the bench with a jumper in place of the receiver. Optical operation, timing accuracy and this pin allocation have not yet been verified on hardware.

## Baseline v3 update

Reflash the entire installation for protocol v3. Controller Retired heartbeats recover lost CANCEL messages and permanently retire that attempt. Clock conversion now waits for retained fit samples on both sides of an event; conversion latency can exceed the old block-close estimate. Per-device measured delay compensation is available through `JNS_TIMEBASE_DELAY_US`; zero remains uncalibrated. See [baseline validation](../../docs/baseline-validation.md).

## Flashing a C6 without a local build

If Windows Smart App Control blocks the local RISC-V toolchain, download the `c6_fn<N>` artifact from the **ESP32-C6 firmware** workflow and flash its merged image at `0x0`. For `c6_fn1_sync`, GPIO 7 held LOW (e.g. a wire to GND) is a clear beam; releasing it to the pull-up is a break.
