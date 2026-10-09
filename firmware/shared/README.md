# Shared code

- `include/lanetime/protocol.hpp`: wire contract encoder/decoder (below).
- `include/lanetime/timebase.hpp`: ST-1 timebase tracking (specification §5.1). Native tests: `sh firmware/shared/tests/run.sh`. Callers must serialise `sample()` (radio callback) against conversions (main loop).

# Prototype wire contract v2

`include/lanetime/protocol.hpp` is the portable encoder/decoder used by ST-1. The lane-display prototype also uses this contract; sensor/spare firmware is still to be built. This contract may change during bench testing and has no JNS_Timing code dependency.

ESP-NOW broadcast uses an exact 40-byte payload. Integers are unsigned little-endian, explicitly encoded; no packed structs or native-endian assumptions.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | ASCII `JNSL` |
| 4 | 1 | Version = 2 (v1 is rejected) |
| 5 | 1 | Kind: Status=1, Arm=2, Cancel=3, Go=4, Start=5, Result=6, Reveal=7 |
| 6 | 1 | Sender role: Controller=1, Start=2, Finish=3 |
| 7 | 1 | Sender lane 1–4; controller always lane 1 |
| 8 | 4 | System ID |
| 12 | 8 | Controller session: random nonzero per controller boot |
| 20 | 4 | Attempt: incremented on each accepted ARM |
| 24 | 1 | Mode: Flying=0, Standing=1 |
| 25 | 1 | Enabled-lane mask, bits 0–3 |
| 26 | 1 | Flags: Ready=1, Armed=2, Running=4, Complete=8, Fault=16, Cued=32 |
| 27 | 1 | Reveal: finished-lane mask, bits 0–3; otherwise reserved, zero |
| 28 | 8 | See *Value field* below |
| 36 | 4 | Sender boot token, random nonzero per boot |

## Value field

| Packet | Value |
| --- | --- |
| Controller Status | ST-1 clock in microseconds, read just before submission: the common timebase sample |
| Peer Status with Cued | The GO time the peer holds, in ST-1 time |
| Peer Status otherwise | Zero |
| Go | Scheduled GO time in ST-1 time; beeps fall at GO−3 s, GO−2 s and GO−1 s |
| Start | Qualified beam event time, converted to ST-1 time by the sender |
| Result | Elapsed microseconds, computed from start and finish in ST-1 time |
| Reveal | Scheduled reveal time in ST-1 time |
| Arm, Cancel | Zero |

Identity is ESP-NOW source MAC plus announced role/lane and boot token. The system ID is an accidental-mixing filter, not authentication. Reject invalid encodings, other systems, unknown versions, unexpected roles and stale sessions/attempts.

## Required peer behaviour

Track ST-1's timebase from its Status heartbeats. Timestamp each heartbeat in the receive callback, keep the minimum of (local − ST-1) over a sliding window, and correct for drift. Report not Ready until the estimate is established. See specification §5.1.

Send Status every 500 ms, including while running and after completion. Before adopting control it may use session/attempt zero. Ready means eligible for a new arm: a usable timebase estimate, no fault and, in Flying, a qualified clear beam. Start sensors in Standing report Ready without a clear beam, since they only sound cues. On ARM, echo its session/attempt/mode/mask and assert Armed once ready. Keep Armed set through running/completion, adding Running/Complete as applicable; Fault invalidates readiness. Change boot token after every reboot.

ARM must be idempotent: duplicate copies cannot clear an already latched start/finish. A newer ARM creates a new attempt and clears stale results after a qualified clear beam. Remember cancelled/completed identities so delayed copies cannot reopen them. Bind control to the chosen controller MAC. A controller reboot changes session; retire the old session rather than accepting arbitrarily old sessions again. Detect additional controllers instead of switching mid-run.

Riders wait for green at ST-1. ARM is broadcast while acknowledgements are collected; an observed START before all required acknowledgements invalidates the group. Accept GO only in Standing after ARM; accept START only from the matching Flying start sensor (Controller lane 1 for ST-1). Latch the start time once per attempt; duplicates never change it. A finish earlier than the start time is invalid.

On GO, every required start and finish unit stores the GO time and echoes it in Status with Cued set. Start units convert the beep and GO times to their local clocks and sound them from a hardware timer. ST-1 abandons the attempt if any required unit has not echoed the correct GO time by the first beep. Finish units in Flying set Running once START is latched; start units resend START every 100 ms until the matching finish unit shows Running or Complete.

Result comes from the matching finish sensor, with its own role/lane/boot token, the adopted controller session/attempt/mode/mask, Complete flag and elapsed microseconds computed in ST-1 time. Repeating an immutable Result is allowed; the controller accepts the first. Receive-only lane displays filter by lane/session/attempt and clear stale output; they do not announce themselves.

ST-1 sends Reveal once it holds every enabled lane's Result, or when the coach presses reveal now. It freezes the result set and schedules the reveal 1 s ahead, then submits Reveal immediately and every 100 ms until that instant. Busy/failed submissions are retried; the timestamp and mask never change. Each display holds its own lane's Result and shows it at the reveal time, converted through its timebase, not on receipt. A lane whose bit is clear in the finished-lane mask shows DNF. Displays show only their own lane's time. A set bit with no matching Result means missing data, not DNF: show an explicit unavailable indication. A display must receive at least one Reveal before its deadline for a synchronised reveal; receive-only broadcast cannot guarantee delivery.

Reveal uses byte 27 for a finished mask that must be a subset of the enabled mask (zero means all DNF). It must come from Controller lane 1 and have nonzero session, attempt, reveal time and boot token. Other packets still require byte 27 to be zero. The existing 40-byte v2 layout is retained; older decoders reject Reveal, so display firmware must use the updated shared decoder.

ST-1 keeps configuration locked and eInk refresh suspended during its reveal lead-in. CANCEL and faults abort the scheduled reveal. The 30-second deadline applies to collecting results; a reveal scheduled by that deadline may finish its one-second lead-in afterwards. Required sensor readiness/identity checks remain active until the reveal.

CANCEL is terminal for the matching session/attempt regardless of subsequent configuration changes. Controller Fault status must also invalidate matching attempts/results. Expire control status locally after a provisional three seconds so lost cancellation cannot leave a sensor armed indefinitely. ST-1 sends Status every 500 ms in every state; required sensor status likewise expires after three seconds at ST-1. The group deadline is 30 seconds from ARM. Controller Preparing is represented by ARM packets, without Armed in controller Status.

START and GO carry timestamps, so retries are safe: a late or repeated copy carries the same time. Radio delay and loss therefore affect only when a result appears, not its value. Sync error, drift and sensing latency remain to be measured.
