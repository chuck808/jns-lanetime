# ST-1 MagTag prototype

Arduino C++ firmware for the Adafruit MagTag (ESP32-S2), built with PlatformIO. It implements setup, group arming, the common ST-1 timebase heartbeat, the scheduled Standing countdown, Flying lane 1 start detection, sensor readiness/conflict checks, result collection and scheduled result reveal. A C3/MAX7219 lane-display prototype is available; FN-1–FN-4 and ST-2–ST-4 prototypes are available; spare firmware remains outstanding. It also records each attempt as efforts and uploads them to JNS SPLIT at the end of a session. ST-1 and FN-1 have been flashed and their clock sync works on the bench; optical operation and timing accuracy have not yet been validated.

The normal build refuses to arm without compatible sensor announcements. Use the explicitly isolated bench build to try ST-1 alone.

## Hardware and wiring

| Function | Connection |
| --- | --- |
| IR driver input | D10 three-pin STEMMA signal, GPIO 10 |
| TSSP77038 receiver output | A1 three-pin STEMMA signal, GPIO 18 |
| Optical power | Regulated 3.3 V and common GND |
| MODE, LANES, CANCEL, ARM/GO | Buttons A–D: D15, D14, D12, D11 respectively |
| Live lane state | Four onboard NeoPixels, GPIO 1; GPIO 21 LOW enables power |
| Local countdown sound | Onboard speaker GPIO 17; amplifier enable GPIO 16 |
| Display | Onboard SPI eInk; SSD1680 by default, original ILI0373 build available |

**ST-1 needs both TX and RX for Flying mode.** It has no passive reflector. The earlier transmitter-only description was incomplete; Standing does not use local start sensing.

The MagTag's three-pin STEMMA power outputs default to **5 V**. Disconnect power, change its supply-selection jumper to **3.3 V**, then verify voltage/polarity before connecting optics. Both connectors have a 1 kΩ series resistor and a 3.6 V zener, so the receiver must run at 3.3 V: at 5 V its output would hold A1 at the ESP32-S2's absolute limit. Use an IR driver breakout or MOSFET driver; never drive the IR LED directly from a GPIO. The proposed four-pin optical connector has separate TX/RX digital signals and is not an I2C/STEMMA QT device.

**Do not power the Adafruit High Power IR emitter from the STEMMA connector.** It draws about 400 mA when pulsing at 5 V (200 mA at 3 V); the connector is rated for 200 mA and shares the MagTag's 3.3 V regulator with Wi-Fi and the eInk. Power the emitter's V+ from its own 5 V supply (for example a USB boost module), take only its signal from D10, and join the grounds. A 220–470 µF capacitor across V+ and GND at the emitter smooths the 38 kHz pulses. Check the emitter's temperature after a long session: the carrier runs continuously while the beam is in use.

| Wire | Connection |
| --- | --- |
| Emitter In | D10 connector signal |
| Emitter V+ | Separate 5 V supply, not the D10 connector |
| Emitter GND | Separate supply GND **and** MagTag GND |
| Receiver signal, power, GND | A1 connector, jumper at 3.3 V |

Firmware cannot verify the supply jumper or LED current.

The carrier is nominal 38 kHz at 50% duty using hardware LEDC PWM. The TSSP77038 input is active LOW for detected return; an internal pull-up makes a disconnected input read broken. Provisional qualification is 20 ms clear before arming and 200 microseconds broken before START. The first rising-edge timestamp is retained, even for breaks ending between loop iterations. These thresholds and sensing latency need measurement. No signal-strength estimate is inferred from the binary receiver.

## Build and flash

Install PlatformIO (VS Code extension, or `pip install platformio==6.2.0`). From this directory:

```sh
pio run -e magtag
pio run -e magtag -t upload
pio device monitor -b 115200
```

Use a USB data cable. If upload cannot find the board, hold BOOT0, press/release RESET, release BOOT0 and retry. Specify `--upload-port` if necessary. Flashing replaces the current application/CircuitPython installation.

| Environment | Use |
| --- | --- |
| `magtag` | 2025/newer SSD1680 MagTag, live ESP-NOW |
| `magtag_legacy` | Original ILI0373 MagTag, live ESP-NOW |
| `magtag_bench` | SSD1680 MagTag, simulated external readiness, **no ESP-NOW transmission** |

Both display variants use monochrome mode. For a legacy-board bench test, add `-DJNS_LEGACY_DISPLAY=1` to the bench environment's flags. Build dependencies are pinned in `platformio.ini`.

Set `JNS_SYSTEM_ID` to the same unique value across this installation and use a common `JNS_RADIO_CHANNEL` (default 6). There is no AP, phone interface or pairing screen; ST-1 joins a router only during an upload, with ESP-NOW stopped. Broadcast is unencrypted: system ID prevents accidental mixing, not deliberate impersonation.

## Controls

- **MODE (A / D15):** tap to switch Flying/Standing while inactive; hold for 1 s to step the distance through 10, 20, 30 and 50 m.
- **LANES (B / D14):** select contiguous lanes 1 through N, N=1–4.
- **CANCEL (C / D12):** invalidate the attempt and return to idle; takes priority over simultaneous presses. While idle, hold for 2 s to upload the session's efforts to JNS SPLIT.
- **ARM/GO (D / D11):** request a new group attempt. In Standing, press again after arming to schedule the countdown: one second of lead-in, three beeps one second apart, then GO, all at fixed instants in ST-1's clock. Flying waits for each lane's beam break. Press and hold ARM/GO for 1 s while running to schedule an early reveal; lanes without a collected Result show DNF. A short press while running does nothing. The hold must begin with a fresh press while running: holding the original ARM or GO does not trigger reveal.

Mode, lane count and distance are saved. Attempts never resume after reboot. Configuration is locked while active. After completion or a fault, ARM requests a fresh attempt, or CANCEL returns to idle. Group arm applies in both modes; the simultaneous reveal (specification §7, §8.1) supersedes per-lane rearm. Both automatic and early reveals freeze the result set, then repeat a common reveal timestamp every 100 ms during a 1 s lead-in. Late results cannot change the finished mask. Configuration stays locked and eInk remains untouched until that instant; CANCEL still discards the attempt.

**The eInk screen is for setup/results; it retains its previous image while an attempt is active. Use the LEDs for live state.** Inactive refreshes may briefly delay button response. There are no eInk refreshes while preparing, armed, counting down or running.

| LED | Meaning |
| --- | --- |
| Dim white; lane 1 red in Flying when broken | Enabled idle lane; local beam not clear |
| Amber | Preparing / waiting for sensor acknowledgements |
| Green | Armed, or a finished lane after reveal |
| Amber, then flashing with each beep | Standing lead-in, then countdown |
| Blue | Lane started, awaiting finish |
| Green per lane while running | ST-1 holds that lane's Result; all green triggers the reveal |
| Red on enabled lanes | Invalid attempt; screen explains after cancellation repeats |
| Amber during/after reveal | Lane marked DNF |
| Off | Disabled lane |

Colours describe controller state, not optical signal margin. ST-1 sounds lane 1's cue from an `esp_timer` at the scheduled instants; ST-2–ST-N now have a C3 prototype to do the same for their lanes. Onset alignment between posts is unmeasured.

For a standalone test, upload `magtag_bench`, choose Standing, press ARM, wait for green, then press GO. Expect a one-second pause, three short tones, a GO tone and blue LEDs. Simulated peers acknowledge the schedule, and simulated finishes arrive about 2.5 s after GO, so the attempt reveals times and queues completed efforts. CANCEL resets it. For Flying, fit and align the optics, ARM and interrupt lane 1; a simulated lane 1 finish follows about 2.5 s later. External readiness is simulated but the local beam is real. There are no lane 2–4 Flying start events. The screen says BENCH and no ESP-NOW packets are transmitted; the bench build can still upload over Wi-Fi.

## Attempt handling

See the [prototype wire contract](../shared/README.md) before implementing peers.

- Status heartbeats carry ST-1's clock in every state; they are the other units' timebase samples.
- Discovery lasts at least three seconds after boot. Every enabled finish sensor and ST-2 through ST-N must be ready in both modes, since start posts sound the Standing cue. Flying also requires a clear local beam.
- Duplicate role/lane claims, another controller, or an external start sensor claiming lane 1 prevent arming. Retain visual placement checks: an unheard device cannot be detected electronically.
- ARM repeats every 250 ms during preparation. Required sensors must echo this session/attempt/mode/mask with Armed set before green. Preparation times out after three seconds; an observed start during preparation invalidates the group.
- Required sensor status expires after three seconds. Peer reboot/identity change, new sensors during an armed run, sensor faults, queue overflow invalidate the attempt.
- START carries the lane 1 event time and is resent every 100 ms until FN-1 reports Running. GO carries the scheduled GO time and is resent every 250 ms until every required unit echoes it with Cued. If any has not echoed it by the first beep, the attempt is abandoned. Send failures are retried rather than treated as faults; receive-queue overflow still invalidates the attempt.
- Results must match an announced finish MAC/boot identity, current session/attempt, and a lane whose start this controller observed. The first result is latched. If ST-1 misses a START that its finish node receives, that lane's result is conservatively rejected.
- All collected Results automatically trigger reveal. A fresh 1 s hold of ARM/GO while running freezes any partial set (including zero finishes) and marks the other lanes DNF. Reveal carries the same timestamp and finished mask on each repeat; the local eInk lists times/DNF in lane order after the reveal.
- The provisional group deadline is 30 seconds from ARM, including preparation and the four-second countdown. Timeout invalidates the group rather than presenting a partial set as complete. A reveal scheduled by the deadline may finish its 1 s lead-in afterwards. Required sensor checks continue during that lead-in.
- CANCEL repeats for two seconds; subsequent idle Status heartbeats retain the Retired flag until a new attempt, recovering missed cancellation. Receivers must also expire controller status locally; delivery of cancellation is not guaranteed.

Radio callbacks enqueue bounded records; the receiver interrupt captures edges; the main loop owns state. Hardware PWM generates the carrier independently. Cue tones run from a 1 ms `esp_timer` callback, using the shared CuePlayer. A mutex protects all cue/alert state and the timer alone writes sound PWM. A transition over 20 ms late or a main-loop lease over 100 ms old stops cues and latches a fault requiring restart. Late schedules are rejected.

Received results are rounded to three decimals. Display resolution is not timing accuracy. Clock-sync error and drift, sensing latency, interrupt capture delay, cue alignment and actual hardware operation need measurement before claiming useful timing accuracy.

## Distance, efforts and upload

The distance (10, 20, 30 or 50 m) is shown on the screen with each lane's time and average speed in km/h. On a Standing start that speed is an average from a standstill, not a top speed. JNS SPLIT does not take the distance from ST-1: it copies the distance from its own session. Set the session in JNS SPLIT to the same distance.

Every attempt in which riders started becomes one effort per enabled lane, kept in RAM until uploaded:

| Attempt ended by | Uploaded as |
| --- | --- |
| Reveal, lane finished | `completed`, whole milliseconds, rounded as on the screen |
| Reveal, lane without a result (including early reveal) | `dnf` |
| CANCEL after a start, including during the reveal lead-in | `cancelled` |
| Fault or timeout after a start | `invalid` |

Attempts abandoned before any start (preparation failures, cancelled countdowns) are not efforts. Each effort's `resultId` is ST-1's boot ID, the attempt number and the lane, for example `1A2B3C4D-42-L1`. It never changes, so a retried upload cannot create a duplicate. The queue holds 64 efforts. ST-1 will not arm when the next attempt's efforts would not fit: upload first. **The queue is lost if ST-1 restarts or loses power**, so upload before switching off. The SD-card journal in a later build replaces this.

### Uploading

Holding CANCEL for 2 s while idle follows the JNS Pro pattern:

1. ESP-NOW stops, so the sensors lose ST-1's heartbeat.
2. ST-1 joins the configured Wi-Fi network and sends each waiting effort to JNS SPLIT ([results upload v1](https://github.com/chuck808/jns-split/blob/main/docs/results-upload-v1.md)). The serial monitor logs each reply.
3. ST-1 leaves the network and restarts ESP-NOW. Sensors take about 8 s to be Ready again.

The screen then shows how many were saved, how many JNS SPLIT refused, and how many are left. A refused effort (for example `NOT_IN_SESSION` or `TAG_UNAVAILABLE`) is dropped, as the upload contract says. Anything without a reply, or a credential, limit or server error, stops the round and keeps the rest queued for the next hold. Saved and refused efforts are removed from the queue.

### Upload settings

Copy `include/upload_config.example.h` to `include/upload_config.h` and fill it in. That file is git-ignored: it holds a Wi-Fi password and a timing credential, which must be treated like a password. Never commit it or paste it anywhere.

- Use a **Test** credential from Timing controllers in JNS SPLIT while testing. Its results are shown as demo data and never reach riders, families or bests.
- The session ID is the planned session's ID, from its address in JNS SPLIT.
- Until the lane reader exists, every effort is uploaded with one tag UID. Use a fictional test rider's tag, and put that rider on the session.
- To upload to the dev server on your PC instead of the deployed site, use `http://<your PC's address>:5173/api/timing/efforts` and start the server with `npm run dev -- --host`. Plain http sends the credential unencrypted, so use it only on your own network and only with a Test credential.
- For an `https://` address, ST-1 needs the site's root certificate and refuses to upload without it. In a desktop browser, open the site, view its certificate, select the top (root) certificate in the chain and export it as PEM (Base64). Paste it into the commented `kJnsRootCa` block of the config file and uncomment the two lines.

Without `upload_config.h` the firmware still builds and times normally; holding CANCEL shows `NOT_CONFIGURED`.

## Verification and first hardware checks

```sh
sh tests/run.sh
pio run -e magtag -e magtag_legacy -e magtag_bench
```

Native tests cover the effort queue, result IDs, upload request bodies and reply handling, MODE taps and holds, how each kind of attempt ending is recorded, wire validation, readiness, duplicate/stale events, wrong-device results, reboot/conflicts, missing peers, Standing transitions, multi-lane completion, the Standing cue schedule and its acknowledgement, START resend, beam qualification, Reveal wire validation, automatic/partial/all-DNF reveal, late-result rejection, cancellation/faults during reveal, deadline boundaries, and fresh-press hold behaviour. GitHub Actions runs these and all three builds.

On the bench, check screen variant/buttons, carrier waveform/current, receiver polarity/disconnection, clear/break detection, cancel during countdown and supply stability with wireless active. Then add one finish node and compare timing against a common reference before expanding to four lanes. Hardware verification remains outstanding. Using the lane-display prototype and actual sensor nodes, verify simultaneous reveal, early partial/all-DNF reveal, lost Reveal copies, and cancellation during the lead-in with real radios. Controller-side tests/builds do not establish end-to-end display operation.

## References

- [MagTag pinouts](https://learn.adafruit.com/adafruit-magtag/pinouts) and [schematics](https://github.com/adafruit/Adafruit_MagTag_PCBs).
- [Adafruit ThinkInk drivers](https://github.com/adafruit/Adafruit_EPD).
- [TSSP770 datasheet](https://www.vishay.com/docs/82470/tssp770.pdf) and [Vishay application overview](https://www.vishay.com/docs/80067/appoverview.pdf), including continuous-carrier support.
- [ESP-NOW API/callback guidance](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s2/api-reference/network/esp_now.html).
