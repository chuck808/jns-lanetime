# JNS LaneTime — Build Specification

**Draft for implementation · Revision 4 · 4 October 2026**

**29 September prototype note:** The first [ST-1 firmware](../firmware/controller/README.md) selects the Adafruit MagTag (ESP32-S2), its front buttons/eInk/LEDs/speaker, D10 IR drive and A1/GPIO18 TSSP77038 reception at 3.3 V. ST-1 has TX and RX for Flying mode and no passive reflector. The controller README records provisional group arm, the scheduled Standing countdown, 30-second group deadline, beam qualification and wire contract choices. These are bench defaults, not measured performance requirements. Revision 3 replaces arrival-time starts with a common ST-1 timebase (§5) and moves the Standing cue onto every start post (§5.3); ST-1 firmware and the wire contract follow it. Revision 4 records the agreed finish-sensor and reveal methods (§5.1, §5.4, §8.1). ST-1 now implements scheduled Reveal and hold-to-reveal; other device firmware and timing validation remain outstanding; system-wide open choices below still apply.

## 1. Status and scope

This document translates the agreed concept into hardware and firmware responsibilities. It is a standalone build, with no dependency on JNS_Timing code, JNS Pro, AppGatePro, or AppSprints.

**Specified** means an agreed product decision or the supplied optical specification. **Proposed** means an implementation recommendation that is not yet an agreed decision. **TBD** means a value or choice needed before the affected part can be built or finalised. Do not treat TBD values as implicit defaults.

The system supports one to four BMX lanes, Flying and Standing modes, and raw elapsed time per lane. It has no subscription, rider profiles, IMU, G-force, or separate reaction-time measurement. Cruiser bikes are excluded. Relative placing is outside the baseline pending a product decision.

**Optical specification supplied for this build: 940 nm infrared, modulated at 38 kHz.** This does not specify an LED part, receiver part, drive current, modulation envelope, or sensor response time.

## 2. Physical system

### 2.1 Layout and unit designs — Specified

Each timing line consists of N powered sensing posts plus one passive end-cap, producing N beams across N lanes. The first post has active optics only. Intermediate posts carry active optics aimed across their assigned lane and a passive reflector on the opposite narrow face for the preceding lane. The end-cap terminates the final beam. There is no separately powered optical unit across a lane.

| Unit | Physical design and interface | Optical faces |
| --- | --- | --- |
| ST-1 | Dedicated main controller; landscape enclosure, setup/status display and physical coach controls | Active IR; no reflector |
| ST-2–ST-4 | Compact sensor enclosure; persistent lane/START identification and one power button | Active IR and opposite-side passive reflector |
| FN-1 | Same simple sensor form factor; persistent lane 1/FINISH identification and one power button | Active IR; no reflector |
| FN-2–FN-4 | Same shared enclosure as ST-2–ST-4; persistent lane/FINISH identification and one power button | Active IR and opposite-side passive reflector |
| Passive end-caps | Slim upright enclosure with printed identification; no electronics or controls | Passive reflector only |
| Spare sensor | Same form factor as the simple sensors, with lane and START/FINISH configuration controls | Active IR and opposite-side passive reflector |
| Lane result displays | Separate powered units; physical design remains TBD | None |

ST-1 is a dedicated controller design. Its replacement would be another ST-1, not a reconfigured ordinary sensor or spare. Other replacement arrangements remain deferred. The shared design decision applies to ST-2–ST-4 and FN-2–FN-4; it no longer means that every powered post must have identical hardware or be controller-capable. Sharing suitable circuits and firmware remains possible, but a common PCB or single firmware image is not a requirement.

ST-5 and FN-5 identify the passive end-caps in the four-lane layout. They are not lane 5 sensors. Smaller installations still require one passive end-cap per line; labelling for those packages remains TBD.

### 2.2 Equipment quantities

| Complete Flying system | ST-1 controllers | FN-1 sensors | Intermediate sensors, both lines | Passive end-caps | Result displays |
| --- | --- | --- | --- | --- | --- |
| 1 lane | 1 | 1 | 0 | 2 | 1 |
| 2 lanes | 1 | 1 | 2 | 2 | 2 |
| 3 lanes | 1 | 1 | 4 | 2 | 3 |
| 4 lanes | 1 | 1 | 6 | 2 | 4 |

Spares are additional to these operating quantities.

### 2.3 Enclosure design direction

The agreed mock-ups establish a family of rounded rectangular enclosures with charcoal front faces, grey surrounds, integrated optics, and photographic tripod quick-release mounting. Intermediate sensors have optics and reflectors on opposite narrow faces. ST-1 and FN-1 have a plain opposite face. Passive end-caps use a slimmer upright housing while retaining the reflector area.

Mock-up screen artwork, battery icons, exact button geometry, materials, fasteners, and dimensions are illustrative, not selected components or verified construction details. Component fit, outdoor readability, optical performance, and weather protection still need proving.

**Proposed mechanical requirement:** use a common optical-height reference relative to the tripod mounting plate across active units and passive end-caps. Preserve reflector coverage around that reference. Make alignment indication visible during aiming; a bevel-mounted or wraparound indicator remains an option, not a committed feature.

Beam height is fixed in use and intended for 20-inch BMX wheels. **TBD:** actual beam-centre height and installation tolerance above the riding surface.

Standing mode does not use start-line beam sensing, but the start posts ST-1 to ST-N stand beside the riders to sound the countdown (§5.3). **Proposed Standing package:** ST-1, ST-2–ST-N, FN-1–FN-N, the finish-line end-cap and the result displays. The start-line end-cap is not needed.

## 3. Optical subsystem

### 3.1 Specified operation

Emit 940 nm IR with 38 kHz modulation towards a corner-cube reflector and detect the returning signal. An interruption of the reflected path produces the beam-break event.

The builder has already measured reliable operation beyond 4 m in direct sunlight, with no cross-talk observed on the tested JNS_Timing hardware. Those measurements are valid evidence for that assembly. They do not require reusing its software, and they do not automatically establish the performance of an unspecified replacement receiver or enclosure.

### 3.2 Electrical responsibilities

| Block | Required responsibility | Selection still needed |
| --- | --- | --- |
| IR emitter | Produce the specified wavelength and modulated output | LED, optics, drive current, duty cycle |
| Emitter driver | Switch LED current under hardware-generated modulation | Driver topology, supply, protection, component values |
| IR receiver | Detect reflected modulated IR and expose beam state | Receiver part/circuit, output polarity and voltage |
| Modulation source | Generate 38 kHz without blocking timing or radio handling | MCU peripheral, carrier tolerance, modulation envelope |
| Beam conditioning | Convert receiver behaviour into qualified present/broken states | Response time, filtering, interruption and clear thresholds |
| Alignment indicator | Display red/no return, amber/weak margin, green/adequate margin | Available margin measurement and thresholds |

**Receiver compatibility is a part-selection requirement:** determine whether the selected receiver supports continuous carrier or requires bursts and gaps. “38 kHz receiver” alone is insufficient to specify its drive waveform. Obtain these details from the chosen part's datasheet before defining the emitter waveform.

The three-colour indicator needs evidence of signal margin. If the receiver exposes only a binary output, define a separate margin-measurement technique or revise the indicator requirement before hardware freeze. Do not infer weak versus strong return from one static digital bit.

Use the shielded aperture concept, but verify that the completed aperture permits the intended beam geometry. Weather protection is a property of the complete enclosure, not the slot shape alone.

### 3.3 Event definition — Proposed

Use the first qualified front-wheel interruption as the timing event. Match start and finish height and geometry. Subsequent wheel, frame, body, and rear-wheel interruptions cannot start or finish the same attempt again.

Retroreflective strips on clothing, helmets or bikes may return enough IR to hold the beam present while a rider passes. Test with typical club kit before relying on the event definition.

Require a qualified clear beam before rearming. Exact filter times must follow measurements of the selected receiver; do not select them solely for convenient firmware delays.

## 4. Controller, configuration, and interfaces

### 4.1 Configuration

ST-1 has the controller role by design. Remove the controller-enable switch requirement from ordinary sensors and the spare. Exactly one ST-1 controls a deployed set.

**Specified:** ordinary sensors have their lane and START/FINISH role fixed at build time, compiled into the firmware image for that unit. There is no per-post configuration step in routine use. A unit that changes role is reflashed. The spare is the only field-configurable sensor.

Lane result displays retain the previously specified lane switch, read at boot; changes require a restart.

Each powered sensor also needs a distinct device identity for electronic conflict checks. A role such as “finish, lane 2” is not a unique device identity.

### 4.2 Physical controls

**ST-1 — Specified:** physical buttons provide setup and operation. **Proposed layout shown in the mock-up:** POWER, MODE, SELECT, RESET, and START. ST-1 also provides **reveal now** (§8.1), which schedules an early reveal of finished lanes and marks unfinished lanes DNF. **Specified for the MagTag prototype:** hold ARM/GO for about 1 s while an attempt is running; a short press while running does nothing, so a repeated GO press cannot end an attempt early. Each lane LED turns from blue (started) to green once ST-1 holds that lane's Result, so the coach can see which lanes will show DNF. When every enabled lane is green, ST-1 reveals automatically. After the reveal the eInk lists every enabled lane's time or DNF in lane order, as the coach's record, with no ranking or fastest highlight. RESET (CANCEL) remains the control that discards an attempt without a reveal. Exact button behaviour, lane-selection navigation, and arming interaction remain TBD. ST-1 performs controller duties and lane 1 start sensing in Flying mode.

**Ordinary sensors — Specified:** one power button, lane/role identification, alignment indication, and a piezo. Start posts use the piezo for the scheduled Standing cue, so use the same piezo part and drive on every start post to keep onset behaviour matched. No coach setup-button row or elapsed-time readout is required on these units.

**Spare — Specified:** controls select the lane being replaced and its START/FINISH role; the display shows that assignment. It does not become ST-1. **Proposed interaction:** minus/plus select the lane and SET confirms; holding SET enters configuration or role selection. Exact interaction remains TBD. Configuration locking while armed and retaining the confirmed assignment across power cycles are proposed requirements. Do not silently change identity during a run.

The spare's supported assignments beyond the agreed ST-2–ST-4/FN-2–FN-4 group, including any FN-1 replacement arrangement, remain deferred.

**Deferred:** handheld remote and phone/browser control. Physical ST-1 buttons are the build baseline. No mobile interface or access-point functionality is required for this revision.

### 4.3 Board interface allocation

Allocate the following functions when choosing the ESP32 variant and boards. This is a functional interface list, not a pinout. Passive end-caps require none of these electrical interfaces.

| Interface | Direction | Applicable units / firmware use |
| --- | --- | --- |
| IR modulation/driver control | Output | All powered sensing posts; hardware-generated 38 kHz waveform with selected envelope |
| Receiver beam signal | Input | All powered sensing posts; qualified beam event and readiness |
| Receiver margin signal, if available | Input | All powered sensing posts; alignment indication |
| Identity configuration | TBD | Ordinary sensors: commissioning method TBD; spare: setup buttons |
| Local lane/role indication | Output | Sensors and spare; placement and fault identification |
| Red/amber/green indication | Output | Active sensors; alignment feedback |
| Piezo control | Output | Powered sensing posts; audible behaviour as defined in §5.3 |
| Coach controls and setup/status display | Input/output | ST-1 only; mode, arm, GO/countdown, cancel/reset |
| Power control | Input | Powered units; electrical implementation TBD |
| Power/battery indication | TBD | Battery icons in mock-ups are illustrative |

Board selection must account for peripheral conflicts and usable pins before pin numbers are assigned. Ordinary sensors and the spare do not need ST-1's controller interface.

**TBD:** MCU variant, power source, regulators, charging arrangement, display technology, exact control implementation, buzzer drive, connectors, and component values. This document does not yet constitute a schematic or purchasable BOM.

## 5. Firmware timing behaviour

### 5.1 Common timebase — Proposed architecture, adopted in ST-1 firmware

ST-1's free-running microsecond clock is the system timebase. Every result is the difference of two timestamps expressed in that timebase. No unit starts or stops a clock because a packet arrived.

**Method — agreed in outline; thresholds TBD:**

1. **Sampling.** ST-1 broadcasts Status every 500 ms in every state, carrying its clock read as late as possible before submission. Every other unit reads `esp_timer_get_time()` as the first statement of its ESP-NOW receive callback. Each valid ST-1 Status gives one sample: local time *L*, ST-1 time *R*, offset *o = L − R*.
2. **Block minima.** Group samples into blocks of four (about 2 s). Keep only the sample with the smallest *o* in each block, since the least-delayed packet is the truest. Store the last eight block minima (about 16 s) as (*L*, *o*) pairs.
3. **Drift fit.** Fit *o = a + b·L* by least squares through the stored minima. Reject points beyond a residual threshold and refit once, which removes blocks where every packet was delayed. *b* is the relative crystal drift, typically tens of ppm. A local time converts to ST-1 time as *R = L − (a + b·L)*.
4. **Readiness.** Report Ready only with at least four blocks (about 8 s after first hearing ST-1), the newest sample under 1.5 s old, and the fit's residual spread under a threshold (provisionally 100 µs, to be replaced by a measured figure).
5. **Reset.** On an ST-1 session change (ST-1 rebooted), discard all samples and rebuild. After more than 3 s without a heartbeat, also discard and rebuild, since drift across an unobserved gap is unverified.
6. **Interpolated conversion.** Hold each event's local timestamp and convert it only once a block that closed after the event is in the fit, so the fit has samples either side. This adds up to about 2.5 s before a result is available, which the display behaviour (§8.1) accepts. A converted time is final and is never revised.

The minimum one-way delay appears in every unit's offset estimate, so it largely cancels when a finish timestamp from one unit is subtracted from a start timestamp from another.

The method lives once in `firmware/shared/include/lanetime/timebase.hpp` as pure logic with no Arduino dependency, unit-tested natively with simulated jitter, drift and loss. Start sensors, finish sensors and lane displays all use it. In simulation, two units with different drifts on a deliberately harsh link (half of heartbeats delayed by up to 5 ms) agree on a common event to within about 22 µs; this is a simulation result, not a hardware measurement.

Remaining error sources are sensing latency, interrupt capture latency, offset-estimate error and residual drift. They are **expected to be tens of microseconds, not measured for this build** (100 µs is about 1.5 mm at sprint speed). **Proof before optics (§9):** drive one GPIO edge into two non-ST-1 units; each timestamps it, converts it to ST-1 time and logs it. The difference is the sync error, measured directly. **TBD:** final residual threshold and readiness figures, from that test.

### 5.2 Flying

1. Arm the lane.
2. ST-x captures the qualified start event and sends START carrying the event time in ST-1 time. ST-1 is the timebase and sends at once; ST-2–ST-4 send once the time is interpolable (§5.1, step 6).
3. FN-x latches that start time for the attempt and reports Running in its Status.
4. FN-x determines its finish (§5.4), converts it to ST-1 time, computes elapsed time, and publishes the Result for the reveal (§8.1).

Because START carries a timestamp, delivery delay does not shorten the result. A START that arrives late, or after a retry, still carries the correct start time. The start unit therefore resends START until the matching finish unit reports Running, and a send failure is not a timing fault. A finish beam event captured before START is received is held with its timestamp, which is routine now that ST-2–ST-4 send START late. It becomes a result only if START for the same attempt arrives within the attempt deadline and precedes it in ST-1 time.

Every START must identify its attempt. FN-x accepts the first START for an attempt; duplicates carry the same timestamp and change nothing. Messages for completed, cancelled or superseded attempts must not reopen them.

Keep display updates, sound generation and logging out of time-critical capture.

### 5.3 Standing — scheduled cues

On the coach's GO press, ST-1 fixes a GO time in its own clock: one second of lead-in, then three beeps one second apart, then GO. It broadcasts GO carrying that time, and repeats it until every required unit acknowledges.

- **Start posts (ST-1 to ST-N)** convert the beep and GO times to their local clocks. They sound them from a hardware timer, independent of the main loop. Each rider hears the post beside them, which removes the speed-of-sound offset of a single central cue (roughly 3 ms per metre).
- **Finish posts** take GO as their start time in ST-1 time.
- **Acknowledgement:** each required unit echoes the GO time in its Status with the Cued flag. If any required unit has not echoed it by the first beep, ST-1 abandons the attempt. A missed packet costs a rerun, not a silent lane or an unfair start.

The cue sequence and lead-in are provisional bench values. No automatic early-start detection is provided; valid standing-start comparisons depend on the coach rejecting obvious jumps. **TBD:** cue tones, durations and volume, and measured onset alignment between posts.

### 5.4 Finish sensor behaviour — Specified

**Which break counts.** After ARM, once the beam has a qualified clear, record every qualified break as a local timestamp in a small ring buffer (about eight entries). The finish is the **first recorded break later than the start time**, compared in ST-1 time. This one rule covers the normal case, a late-arriving START (finish already recorded), and stray breaks before the start such as a coach walking through. Later breaks from the same rider's frame, body or rear wheel never replace it.

**State machine.**

| State | Entry and behaviour |
| --- | --- |
| Idle | No adopted attempt. Waiting for ARM from the bound ST-1 |
| Armed | Echo ARM once the beam is clear and the timebase is Ready. Record breaks |
| Cued | Standing only: GO time held and echoed with Cued |
| Running | Start time latched from START (Flying) or GO (Standing); Running flag set |
| Complete | Finish determined and converted; Result published and repeated every 500 ms until the next ARM or a cancel |
| Invalid | Cancel, controller expiry, attempt deadline, timebase lost, or fault. No result |

A unit never resumes an attempt after a reboot. Duplicate START or GO for the latched attempt changes nothing. FN-1 to FN-4 run one firmware target with the lane fixed by build environment; FN-1 differs only physically (no reflector).

## 6. Radio and configuration checks

Use ESP-NOW. There is no requirement for a recurring user pairing ritual.

Assigned lane and role provide logical identity; firmware still needs an addressing method. **Proposed:** commission a system identity once and filter messages by system, intended role, lane, and attempt. Broadcast filtering or stored peer addresses can implement this; the choice is TBD. Neither requires importing JNS_Timing code.

Only the following information flows are needed to describe the implementation at this stage:

- Controller to all units: Status heartbeats carrying the ST-1 timebase.
- Controller to participating units: arm/reset, and the scheduled GO time.
- Start sensor to matching finish sensor: START carrying the event time in ST-1 time, resent until acknowledged.
- Finish sensor to ST-1 and matching display: Result, repeated until the next ARM or cancel.
- Controller to displays: Reveal, carrying a scheduled reveal time in ST-1 time and the finished-lane mask.
- Sensors to controller: role announcements, readiness/fault information, Running, and Cued acknowledgement of the GO time.

Role announcements are **Proposed new behaviour**. Use unique device identity plus configured role to detect two sensors claiming the same lane-and-line or multiple ST-1 controllers in the same set. Refuse arming when a conflict is detected and show a fault. Define the announcement/check interval and late-joining-unit behaviour during implementation; a single missed announcement cannot prove that a conflict is absent.

Retain visual checks for placement, optical aiming, and display lane assignment. Receive-only displays cannot electronically announce their configuration.

Check coexistence with other JNS equipment and another copy of this system. A set identity prevents accidental message acceptance; it does not remove radio contention.

## 7. Attempt lifecycle — Proposed baseline

One outstanding rider per lane. Lanes share one group attempt in both modes, because results are revealed together (§8.1). Overlapping riders in the same lane are not supported by this baseline; adopting that behaviour requires an event-matching design.

| State | Behaviour |
| --- | --- |
| Unarmed | Beam events cannot create a result |
| Armed | Clear/readiness checks have passed; wait for the mode's start event |
| Running | Start time is latched; accept the first qualified finish only |
| Complete | Result is latched; later beam events and duplicate starts are ignored |
| Invalid/DNF | No valid numerical result; rearm is required |

A new arm establishes a new attempt. Reset/cancel invalidates the affected attempt. Reboot during an attempt must not silently resume timing. A finish earlier than the attempt's start time cannot produce a valid result.

**Proposed:** shared group arm in both modes. This supersedes Revision 2's per-lane Flying rearm, which conflicts with a single simultaneous reveal; reinstating it would need a per-lane reveal design. Global reset cancels every active lane. The 30-second attempt deadline remains the backstop, with reveal now (§8.1) as the coach's normal way to end an attempt early. **TBD:** attempt numbering, enabled-lane controls, and rearm user interface.

## 8. Result displays

These are separate units from the lane-identification screens on the sensors. Their enclosure mock-ups remain outstanding; sensor screens do not replace them.

Each lane display shows **its own lane's elapsed time only**. No display shows a ranking, a fastest-only time or another lane's result; comparison is left to riders and coach. Optional alternation between the completed time and rep number in the same field remains proposed. Use an explicit non-result indication for DNF, unused lanes, or invalid results.

There is no running clock. Sprints last about two seconds, so a live display adds nothing; during an attempt each display shows lane state only.

### 8.1 Simultaneous reveal — Specified

1. Each FN publishes its Result, repeated every 500 ms. Each LN holds its own lane's Result without showing it.
2. When ST-1 holds every enabled lane's Result, it chooses a reveal time about 1 s ahead in its own clock and broadcasts **Reveal** carrying that time and a finished-lane mask, repeated until the reveal.
3. Each LN tracks the timebase (§5.1) and shows its result **at the reveal instant**, not on receipt. A display that receives at least one copy before the deadline can reveal at the same moment as the others; missing every copy cannot guarantee a reveal.
4. **Reveal now:** the coach can trigger the reveal before every lane has finished. Lanes without a Result are marked DNF in the mask and show DNF.

**Display states:**

- On arm: remove the previous time and indicate the pending attempt.
- While running: show lane state only; never show a previous result as current.
- At the reveal instant: show the matching attempt's time, or DNF; then alternate with its rep number if adopted.
- Reject older results and Reveals once a new attempt is established.
- If control updates cease, clear stale output after a defined timeout.

Displays remain receive-only and originate no application messages. The controller therefore cannot confirm they received a reset or result. One broadcast cannot guarantee that all displays clear immediately; repetition and a local timeout are proposed to bound stale output.

**Prototype options:** the Neo7 Mini figure-8 digit modules shown in the gallery, or cheaper off-the-shelf flexible WS2812B matrix panels; the choice is open. For early testing, the builder's existing 8×8 RGB matrices may stand in for the result displays.

**TBD:** display size, sunlight visibility, numerical resolution, rounding, symbols, alternation period, and stale timeout. Display resolution must not be confused with timing accuracy.

## 9. Build decisions and practical checks

Resolve decisions when needed for the affected build step rather than inventing component values or completing a production process prematurely.

| Build step | Inputs required | Check before moving on |
| --- | --- | --- |
| Optical bench circuit | Emitter, receiver, reflector, driver, modulation envelope | Stable return, interruption response, ambient-light performance, margin indication |
| Controller and sensor boards | MCU, pin allocation, power, identity configuration, indicators, role-specific controls | Interfaces work together; configuration and spare assignment read correctly |
| Timebase sync | Clock tracking alone, before optics | One GPIO edge into two units; compare converted times (§5.1) |
| One timing lane | Attempt handling, timer capture, clock sync, radio routing, basic output | Measure sync offset error and drift; measure timing error against a common reference; inject duplicates, delayed and lost START |
| Multi-lane operation | Conflict detection, lane controls, independent attempt behaviour | Concurrent starts, correct lane routing, duplicate roles, optical interaction |
| Standing operation | Start-post cue scheduling | Onset alignment between posts' cues; cue-to-timer alignment; missed GO acknowledgement aborts before the first beep |

Before claiming useful timing accuracy, choose an acceptable error and repeatability target for coaching and compare measured results against it. Before producing a schematic/BOM, resolve the component and power choices listed above.

## 10. Reference boundaries

JNS_Timing contributes prior optical test evidence, not a code dependency. The existing Swift G4 aperture and SKLZ identification references are design references. Do not incorporate the previously identified noncommercial Pinewood Derby source into this commercial build under this specification.

This draft intentionally leaves pin numbers and component values open where hardware choices have not been supplied. It defines what those choices must support so the next revision can become a concrete schematic, BOM, and firmware implementation without silently inventing requirements.

## Revision 2 note

Updated the unit taxonomy, quantities, configuration, and interface requirements to match the agreed mock-ups: dedicated ST-1, simple intermediate sensors, reflector-free FN-1, slim passive end-caps, and a button-configurable spare. Deferred phone/remote interfaces and replacement details remain deferred. Illustrative controls and screen details are distinguished from agreed functions. Optical specifications, prior test evidence, and the unresolved timing-delivery issues are unchanged.

## Revision 3 note

Replaced arrival-time starts with a common ST-1 timebase. Heartbeats carry ST-1's clock; START and GO carry timestamps; delivery delay and retries no longer affect results. This resolves the Revision 2 late-delivery constraint, subject to measurement. Standing cues are now scheduled and sounded by every start post, so ST-2–ST-N are required in Standing. Fixed build-time identity replaces the open commissioning question for ordinary sensors. Added a retroreflective-kit test and the display prototyping options. The wire contract moves to version 2.

## Revision 4 note

Recorded the agreed finish-sensor methods: the exact timebase method with interpolated conversion (§5.1), late START from ST-2–ST-4, the finish break rule and FN state machine (§5.4), no running display clock, each display showing only its own lane's time, simultaneous scheduled reveal with DNF marking, and a coach reveal-now control (§8.1). Group attempts now apply in both modes. The Reveal packet was specified in this revision; the subsequent ST-1 implementation now encodes/validates it and schedules both automatic and early reveals. Lane-display firmware remains outstanding.

## ST-1 reveal implementation note — 9 October 2026

The controller freezes its collected Results and finished mask, schedules Reveal 1 s ahead, and repeats it every 100 ms. A fresh ARM/GO press held for 1 s while Running schedules an early reveal; holding the original ARM/GO does not. Late Results cannot change that reveal. Configuration and eInk refresh stay locked during the lead-in; cancellation and faults invalidate it. A reveal accepted by the 30 s attempt deadline may complete its lead-in afterwards. Required sensor readiness checks continue through the lead-in. A display missing the Result for a set finished-mask bit must show unavailable rather than DNF. Physical radio/display verification remains outstanding.

## Lane-display prototype note — 9 October 2026

The lane-display target now contains receive-only state logic and ESP32-C3-DevKitM-1/MAX7219 FC16 builds for lanes 1–4, plus a radio-free display demo. This is a provisional hardware option pending the club's outdoor readability test, not a final display selection. The prototype uses build-time lane identity; the specified physical lane switch remains unimplemented. It distinguishes missing data (ERR) from DNF, retires cancelled/expired attempts and clears output on controller loss. See the target README for provisional pin allocation, power/logic interfacing, failure behaviour and verification. Sensor/spare firmware and physical validation remain outstanding.

## FN-1 prototype note — 9 October 2026

The finish target now implements lane 1 on ESP32-C3-DevKitM-1: qualified timestamped edges, an eight-event buffer, late START support, scheduled Standing GO acknowledgement, interpolated finish selection and immutable repeated Results. A live-radio GPIO bench build disables optical drive. The controller/finish/display interaction is tested in a wire-encoded native simulation; physical timing remains unverified. This is FN-1 only, not yet the planned common FN-1–FN-4 target. Pin allocation, optics assumptions and recovery behaviour are documented in the finish-target README.
