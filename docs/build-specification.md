# JNS LaneTime — Build Specification

**Draft for implementation · Revision 2 · 27 September 2026**

**29 September prototype note:** The first [ST-1 firmware](../firmware/controller/README.md) selects the Adafruit MagTag (ESP32-S2), its front buttons/eInk/LEDs/speaker, D10 IR drive and A1/GPIO18 TSSP77038 reception at 3.3 V. ST-1 has TX and RX for Flying mode and no passive reflector. The controller README records provisional group arm, three-second countdown, 30-second group deadline, beam qualification and wire contract choices. These are bench defaults, not measured performance requirements. Per-lane rearm, other device firmware and timing validation remain outstanding; system-wide open choices below still apply.

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

Standing mode does not require start-line sensing. A controller and suitable audible cues must still be positioned near the riders; a minimum Standing-only equipment package is TBD.

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

Require a qualified clear beam before rearming. Exact filter times must follow measurements of the selected receiver; do not select them solely for convenient firmware delays.

## 4. Controller, configuration, and interfaces

### 4.1 Configuration

ST-1 has the controller role by design. Remove the controller-enable switch requirement from ordinary sensors and the spare. Exactly one ST-1 controls a deployed set.

Ordinary sensors retain their assigned lane and start/finish identity in routine use. **TBD:** how those identities are commissioned. Internal switches remain an option; the mock-ups do not establish a new configuration method for ordinary units. If switches are used, read them at boot and require a restart after changes.

Lane result displays retain the previously specified lane switch, read at boot; changes require a restart.

Each powered sensor also needs a distinct device identity for electronic conflict checks. A role such as “finish, lane 2” is not a unique device identity.

### 4.2 Physical controls

**ST-1 — Specified:** physical buttons provide setup and operation. **Proposed layout shown in the mock-up:** POWER, MODE, SELECT, RESET, and START. Exact button behaviour, lane-selection navigation, and arming interaction remain TBD. ST-1 performs controller duties and lane 1 start sensing in Flying mode.

**Ordinary sensors — Specified:** one power button, lane/role identification, alignment indication, and the existing piezo provision. No coach setup-button row or elapsed-time readout is required on these units.

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

### 5.1 Flying — Specified architecture

1. Arm the lane.
2. ST-x detects the qualified start event and sends START for the current attempt.
3. FN-x starts its local monotonic timer when START is accepted.
4. FN-x captures its finish event and computes elapsed time on that same clock.
5. FN-x sends the result to its lane display and latches completion.

Approximately:

`reported interval = physical interval − start delivery delay + other sensing/timing errors`

The delay includes sensing, firmware handling, radio delivery, and receiver processing. Its magnitude and variation are **assumed acceptable, not measured for this build**. They must be measured before making an accuracy claim.

Capture events close to their source; keep display updates, sound generation, and logging out of time-critical handling. **TBD:** timer peripheral/API, capture point, timer resolution, and overflow handling.

### 5.2 Attempt identity and late delivery

Every START must identify its attempt. FN-x starts at most once for that attempt. Duplicate messages must never reset a running clock. Messages for completed, cancelled, or superseded attempts must not reopen them.

This prevents duplicate restarts. It does not solve delayed first receipt: if the first packet is lost and a later copy is accepted, timing still starts late and produces a short result.

**Unresolved implementation constraint:** define how late timing events are detected/rejected, or revise the timing architecture if measured radio behaviour is unsuitable. An attempt identifier alone cannot establish packet age. Do not present retransmission as a timing-accuracy solution.

### 5.3 Standing

The controller issues a predictable countdown and GO. Enabled finish units start their clocks when accepting GO and stop on their own qualified beam events. Apply the same duplicate-event and attempt rules as Flying mode. Delayed first receipt of GO carries the same timing risk.

No automatic early-start detection is provided. Valid standing-start comparisons depend on the coach rejecting obvious jumps.

Use piezo buzzers for the audible cue. **TBD:** which powered units sound, their placement, and how timer starts align with what each rider hears. Passive end-caps cannot sound. Sound from the finish line must not be assumed equivalent to a cue beside the rider.

## 6. Radio and configuration checks

Use ESP-NOW. There is no requirement for a recurring user pairing ritual.

Assigned lane and role provide logical identity; firmware still needs an addressing method. **Proposed:** commission a system identity once and filter messages by system, intended role, lane, and attempt. Broadcast filtering or stored peer addresses can implement this; the choice is TBD. Neither requires importing JNS_Timing code.

Only the following information flows are needed to describe the implementation at this stage:

- Controller to participating units: arm/reset, countdown, and GO.
- Start sensor to matching finish sensor: START.
- Finish sensor to matching display: result/status.
- Sensors to controller: role announcements and readiness/fault information.

Role announcements are **Proposed new behaviour**. Use unique device identity plus configured role to detect two sensors claiming the same lane-and-line or multiple ST-1 controllers in the same set. Refuse arming when a conflict is detected and show a fault. Define the announcement/check interval and late-joining-unit behaviour during implementation; a single missed announcement cannot prove that a conflict is absent.

Retain visual checks for placement, optical aiming, and display lane assignment. Receive-only displays cannot electronically announce their configuration.

Check coexistence with other JNS equipment and another copy of this system. A set identity prevents accidental message acceptance; it does not remove radio contention.

## 7. Attempt lifecycle — Proposed baseline

One outstanding rider per lane. Different lanes may operate independently in Flying mode. Overlapping riders in the same lane are not supported by this baseline; adopting that behaviour requires an event-matching design.

| State | Behaviour |
| --- | --- |
| Unarmed | Beam events cannot create a result |
| Armed | Clear/readiness checks have passed; wait for the mode's start event |
| Running | Start time is latched; accept the first qualified finish only |
| Complete | Result is latched; later beam events and duplicate starts are ignored |
| Invalid/DNF | No valid numerical result; rearm is required |

A new arm establishes a new attempt. Reset/cancel invalidates the affected attempt. Reboot during an attempt must not silently resume timing. Finish before accepted start cannot produce a valid result.

**Proposed:** per-lane rearm in Flying mode; shared group arm in Standing mode. Global reset cancels every active lane. **TBD:** DNF timeout, attempt numbering, enabled-lane controls, and rearm user interface.

## 8. Result displays

These are separate units from the lane-identification screens on the sensors. Their enclosure mock-ups remain outstanding; sensor screens do not replace them.

Display one raw elapsed time per lane, with optional alternation between completed time and rep number in the same field. Use an explicit non-result indication for DNF, unused lanes, or invalid results.

**Proposed display behaviour:**

- On arm: remove the previous elapsed time and indicate the current pending attempt.
- While running: do not show a previous result as current.
- On completion: latch the matching attempt's time and alternate with its rep number.
- Reject older results after a new attempt is established.
- If control updates cease, clear stale output after a defined timeout.

Displays originate no application messages. The controller therefore cannot confirm they received a reset or result. One broadcast cannot guarantee that all displays clear immediately; repetition and a local timeout are proposed to bound stale output.

**TBD:** display size, sunlight visibility, numerical resolution, rounding, symbols, alternation period, and stale timeout. Display resolution must not be confused with timing accuracy.

## 9. Build decisions and practical checks

Resolve decisions when needed for the affected build step rather than inventing component values or completing a production process prematurely.

| Build step | Inputs required | Check before moving on |
| --- | --- | --- |
| Optical bench circuit | Emitter, receiver, reflector, driver, modulation envelope | Stable return, interruption response, ambient-light performance, margin indication |
| Controller and sensor boards | MCU, pin allocation, power, identity configuration, indicators, role-specific controls | Interfaces work together; configuration and spare assignment read correctly |
| One timing lane | Attempt handling, timer capture, radio routing, basic output | Measure error against a common reference; inject duplicates and delayed/lost START |
| Multi-lane operation | Conflict detection, lane controls, independent attempt behaviour | Concurrent starts, correct lane routing, duplicate roles, optical interaction |
| Standing operation | Cue locations, countdown behaviour | Cue-to-timer alignment and missed/delayed GO behaviour |

Before claiming useful timing accuracy, choose an acceptable error and repeatability target for coaching and compare measured results against it. Before producing a schematic/BOM, resolve the component and power choices listed above.

## 10. Reference boundaries

JNS_Timing contributes prior optical test evidence, not a code dependency. The existing Swift G4 aperture and SKLZ identification references are design references. Do not incorporate the previously identified noncommercial Pinewood Derby source into this commercial build under this specification.

This draft intentionally leaves wire encoding, pin numbers, and component values open where hardware choices have not been supplied. It defines what those choices must support so the next revision can become a concrete schematic, BOM, and firmware implementation without silently inventing requirements.

## Revision 2 note

Updated the unit taxonomy, quantities, configuration, and interface requirements to match the agreed mock-ups: dedicated ST-1, simple intermediate sensors, reflector-free FN-1, slim passive end-caps, and a button-configurable spare. Deferred phone/remote interfaces and replacement details remain deferred. Illustrative controls and screen details are distinguished from agreed functions. Optical specifications, prior test evidence, and the unresolved timing-delivery issues are unchanged.
