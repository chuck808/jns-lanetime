# RFID, session modes and results recording

**Working design v0.1 · 26 October 2026 · Discussion baseline, not implemented firmware**

This document defines the next extension to JNS LaneTime: club-issued rider tags, one check-in reader per lane, three operating modes and a reliable record of each effort. It is intended to guide prototype decisions and implementation without committing to a complete production system.

**Status key:** **Agreed** records the behaviour settled in discussion. **Proposed** is a recommended implementation for review. **Open** requires a decision or measurement before the affected build step.

## 1. Existing system and scope

The [build specification](build-specification.md) and [firmware documentation](../firmware/README.md) describe the existing prototype. It uses ST-1's common timebase, timestamped beam events, group attempts in both start modes and a scheduled result reveal. Controller, external start, finish and lane-display prototypes exist; physical timing accuracy remains unvalidated.

**Agreed:** add RFID identification before every run in all three modes. This supersedes the earlier suggestion to retain a rider assignment across several coached efforts. No number of planned sprints needs to be entered.

RFID readers, rider records, durable session logging and independent free-ride attempts are new work. This document does not change the current firmware or claim those features already exist. Its free-ride lifecycle is a planned extension to the group-only baseline, not a description of current behaviour.

## 2. The three operating modes

| Mode | Registration and start | Result and next attempt |
| --- | --- | --- |
| **Free-ride** | Rider scans at an available lane. Once that lane is ready and armed, its start-beam break starts timing; its finish-beam break ends timing. No countdown. | Save and show that lane's result independently. Automatically return to check-in for a new scan, without waiting for other lanes. |
| **Coached flying start** | Riders scan at their chosen lanes. Coach arms the participating group and gives the go-ahead. Each lane starts on its own start-beam break. | Preserve the shared reveal and coach-controlled rearm. A fresh scan is required for every effort. |
| **Coached static start** | Riders scan at their chosen lanes. Coach arms the group and initiates the shared countdown. Scheduled GO starts all participating lanes. | Preserve the shared reveal and coach-controlled rearm. A fresh scan is required for every effort. |

**Agreed:** each lane times at most one rider at once. Free-ride means self-service timing within the club's normal operating arrangements; a clear beam does not establish that the riding or run-out area is clear.

**Proposed:** select the operating mode for the whole installation, rather than mix modes across lanes. Change mode or distance only when all attempts are inactive. Freeze the participating lane set at group arm. Missing registration must not silently remove a lane from the coach's selected group.

**Open:** free-ride result hold duration, readiness indication, registration expiry and the exact conditions for automatic rearm. Account for the existing delayed timestamp conversion and finish-break burst handling; do not rearm merely because the first finish edge has occurred.

## 3. Rider check-in

**Agreed hardware concept:** one standalone battery-powered ESP32/RFID reader per lane, a few metres before the start, with a clear lane label, deliberate tap area, status LED and buzzer. See the [mock-up](../images/README.md#rfid-lane-check-in-scanner). The reader is separate from the timing optics.

A rider presents their club-issued tag. The reader sends its identity, configured lane and tag identifier to ST-1. ST-1 accepts or rejects the assignment; the reader confirms that decision.

**Proposed feedback pattern:**

| Feedback | Meaning |
| --- | --- |
| Amber | Registration pending |
| Green and one short beep | ST-1 has accepted the rider for this lane |
| Red and two short beeps | Registration rejected or could not be confirmed |

Green means **checked in**, not permission to start. Coached riders wait for the coach. Free-ride riders need a separate lane-ready indication; its location and behaviour remain open. The reader has no screen in the agreed mock-up; a rider-name display is not a current hardware requirement.

**Proposed registration rules:**

- Enrol a tag against a stable rider ID before use. Unknown or revoked tags do not automatically create riders.
- One rider may occupy only one lane at a time. A fresh scan may move an unarmed registration to another available lane; ST-1 retires the old assignment and updates both readers. An armed or running rider cannot move.
- A different tag cannot silently replace another rider on an occupied lane. The coach can clear an unarmed assignment, or it can expire under a defined rule.
- Holding a tag in the field is one presentation. Removal and re-presentation are required for a new check-in, even if the previous attempt has just finished.
- Each presentation gets a unique request identity. Radio retries reuse it. ST-1 repeats its original response rather than creating another assignment.
- Readers accept acknowledgements only for their current request and current controller session. Timeout or controller loss clears any positive indication. A delayed request cannot register someone after its validity window or after a newer attempt has begun.
- Arming freezes rider, lane and attempt identity. Scans received during the attempt cannot alter its ownership.
- Completion, cancellation or invalidation consumes the registration. Every subsequent effort requires a new scan. An explicit clear action handles a rider who scans and leaves.

**Open:** tag/reader standard, tag form factor, compatible hardware, scan-removal interval, retry/expiry timings and where the coach sees rejection details. Test the chosen tag, antenna and enclosure together. A tag identifier is a practical club lookup key, not proof of the person's identity or secure authentication.

## 4. Attempt lifecycle and result meaning

**Proposed free-ride lifecycle:** awaiting scan → registered → preparing/armed → running → result or terminal outcome → awaiting fresh scan. No electronic rider queue in the first version. Each lane owns its own attempt and deadlines; activity in another lane cannot clear, supersede or reveal it.

**Proposed coached lifecycle:** collect registrations → coach arms group → freeze one rider per participating lane → beam starts or scheduled GO → collect results → shared reveal → fresh registrations. Existing cancel and early-reveal controls remain available.

Saving a result and revealing it are separate operations. Record a received measurement while it is hidden, but retain its validity state: a subsequent group fault or cancellation must not leave a provisional time presented as a successful effort. Preserve the measurement for diagnosis and append the terminal outcome.

| Recorded outcome | Meaning |
| --- | --- |
| Completed | Valid timing result accepted for the attempt |
| DNF | Attempt ended without a valid finish under the session's completion rule; include the reason |
| Cancelled | Explicit cancellation; include whether the rider had started |
| Error / invalid | Timing, identity, communication, synchronisation or equipment failure prevents a trustworthy result |

**Proposed:** preserve unsuccessful attempts rather than erase them. Do not record a missing radio Result as proof that a rider physically failed to finish. The current shared reveal mask displays DNF for unfinished lanes; richer stored reasons must distinguish coach-ended/no-result from a confirmed equipment failure. A display missing a known valid result is a delivery problem, not a change to that rider's result.

**Open:** no-start versus no-finish deadlines in free-ride, display text for each outcome and how long a previous result stays visible before the next rider checks in. A previous time must never appear to belong to the next attempt.

## 5. Software responsibilities

| Component | Proposed responsibility |
| --- | --- |
| RFID reader | Read tags, identify its lane, send/retry registration requests, show acknowledged status; optional bounded diagnostic log |
| ST-1 | Authoritative registration and rider-to-attempt association; session settings; coached group or independent lane coordination; durable event journal |
| Start and finish units | Capture and exchange timing events using the common timebase; carry attempt identity; no rider names required |
| Lane display | Show only its lane's current attempt; shared reveal in coached modes, independent result handling in free-ride |
| Browser management interface | Rider/tag enrolment, session setup, corrections, results table, exports and later progress views |

**Proposed:** a shared reader firmware target with a fixed lane configuration, rather than separate implementations per lane. Ordinary reader use should not require lane selection at each scan.

The current wire contract uses a global controller session/attempt, enabled-lane mask and group reveal. Independent lane operation needs an explicit protocol/state-machine design across ST-1, sensors and displays. It is not a reader-only change. Registration messages also need versioning and duplicate handling; do not squeeze rider data into existing fields without reviewing their meaning.

**Open:** whether to add a separate registration protocol or revise the shared contract, and how per-lane attempt identities and cancellation scope are represented. Keep the timing radio path responsive while performing storage or UI work.

## 6. Records and offline operation

**Proposed minimum records:**

| Record | Fields |
| --- | --- |
| Rider | Stable rider ID, display name, active tag assignment(s), retired/replaced tag history |
| Session | Stable session ID, date/location if known, default measured distance, optional coach notes |
| Registration | Unique request/registration ID, reader identity, tag reference, rider ID, lane, decision and reason |
| Attempt | Globally unique attempt ID, session ID, optional coached group-run ID, rider ID, lane, mode, distance snapshot, elapsed microseconds if available, terminal outcome and reason |

Store mode and distance with each attempt, not only in mutable session defaults. Distinguish a club session from the controller's boot/session token. Attempt IDs must not collide after reboot or across independent lanes. Date/time may be supplied later by the management interface; do not invent a calendar timestamp when offline. Calendar time is not the precision timing clock.

Use integer microseconds for stored elapsed times and round for display. This is storage resolution, not an accuracy claim. Compare personal bests only within compatible distance and start-mode categories; free-ride beam-to-beam efforts should remain identifiable separately from coached flying efforts.

**Proposed persistence strategy:** ST-1 keeps a durable journal; the management interface receives and stores records using stable IDs and acknowledges them only after saving. Interrupted/repeated transfers must not duplicate results. Retain unsynchronised records until confirmed safe to remove. Define an explicit retention policy for already-synchronised records.

In recorded sessions, persist the accepted registration and rider-to-attempt association before confirming/arming. Persist measurements and terminal outcomes as they arrive, using a recovery-safe format. On reboot, recover saved history, retire unfinished attempts as interrupted/invalid and require fresh scans; never resume timing an old attempt. If recording storage is full or unavailable, stop accepting new recorded attempts and show the fault rather than silently dropping history. This policy and storage latency need prototype validation.

A reader's optional diagnostic log is not authority to backdate an unacknowledged registration. Results and rider history are not written to the RFID tags in the first version. Replacing a tag retains the same rider ID and history.

A phone disconnection should not stop an already configured session: ST-1 needs the relevant roster/tag mapping and settings locally. Corrections made later should preserve the original measurement and an audit entry rather than silently overwrite ownership or elapsed time.

**Open:** storage medium/capacity and endurance, roster size, session length, power-loss recovery method, browser connection/transport, durable browser storage and backup/export policy. Internet access is not required for track-side recording. Private rider/parent access is later work; no names, tag mappings or session records belong in the public source repository.

## 7. Implementation sequence and useful checks

These are proposed milestones, not a release-qualification matrix.

1. **Reader bench test:** one reader and representative club tags. Check tap reliability through the case, held-tag suppression, visible/audible feedback and power consumption.
2. **Coached registration:** add accepted rider/lane assignments to the existing group workflow. Exercise unknown tags, lane swaps, occupied lanes, lost acknowledgements and scans after arm.
3. **Recording and export:** add the rider register, session journal and simple results table/CSV export. Reboot and disconnect/reconnect during a test session; recover one correctly attributed record per attempt, without invented times or duplicates.
4. **Independent free-ride:** extend attempt identity, readiness, deadlines, cancellation and display handling across the timing system. Run different lanes at different cadences; one lane's reset must not disturb another. Preserve coached-mode behaviour.
5. **Progress views:** add rider history and comparable personal bests once capture and attribution are reliable. Rider/parent accounts, automatic leaderboards and richer sharing remain optional later work.

The key end-to-end exercise is four tagged riders swapping lanes between efforts, with repeated scans, packet loss and a disconnected management device. Every accepted attempt must retain the correct rider, lane, mode and distance. Timing accuracy still requires independent physical measurement.

## 8. Reference lessons and next decisions

[Sportiduino](https://github.com/sportiduino/sportiduino) provides useful examples of successful-operation feedback, duplicate-presentation handling, local backup records and reader/tag compatibility testing. Its checkpoint/tag-written timing model is not the LaneTime timing model. Do not copy its rule against consecutive punches at the same station literally: our riders must be able to repeat a lane after a fresh scan. Its code is GPLv3; assess specific reuse before importing code. No Sportiduino code is incorporated by this document.

Before the reader prototype, choose and test a compatible reader/tag pair and settle the physical tap placement. Before session logging, decide storage and roster capacity and how the management interface connects. Before free-ride, specify per-lane attempts and display/reset behaviour. Those choices can be resolved at their build steps; a complete app, cloud service or production PCB is not needed to begin.
