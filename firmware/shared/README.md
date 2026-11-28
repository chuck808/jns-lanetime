# Shared code

- `include/lanetime/beam.hpp`: shared qualified beam-edge logic used by all start and finish posts.
- `include/lanetime/protocol.hpp`: wire contract encoder/decoder (below).
- `include/lanetime/timebase.hpp`: ST-1 timebase tracking (specification §5.1). Native tests: `sh firmware/shared/tests/run.sh`. Callers must serialise `sample()` (radio callback) against conversions (main loop).

# Prototype wire contract v5

`include/lanetime/protocol.hpp` is the portable encoder/decoder used by ST-1. Controller, external starts, all four finishes and lane displays use this contract; spare firmware remains unimplemented. This contract may change during bench testing and has no JNS_Timing code dependency.

ESP-NOW broadcast uses an exact 41-byte payload. Integers are unsigned little-endian, explicitly encoded; no packed structs or native-endian assumptions.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | ASCII `JNSL` |
| 4 | 1 | Version = 5 (earlier versions are rejected) |
| 5 | 1 | Kind: Status=1, Arm=2, Cancel=3, Go=4, Start=5, Result=6, Reveal=7, Command=8 |
| 6 | 1 | Sender role: Controller=1, Start=2, Finish=3, Remote=4 |
| 7 | 1 | Sender lane 1–4; controller always lane 1; a remote sends its paired slot 1–4 (1 before pairing) |
| 8 | 4 | System ID |
| 12 | 8 | Controller session: random nonzero per controller boot |
| 20 | 4 | Attempt: incremented on each accepted ARM |
| 24 | 1 | Mode: Flying=0, Standing=1 |
| 25 | 1 | Enabled-lane mask, bits 0–3 |
| 26 | 1 | Flags: Ready=1, Armed=2, Running=4, Complete=8, Fault=16, Cued=32, Retired=64 |
| 27 | 1 | Reveal: finished-lane mask, bits 0–3. Controller Status: bits 0–2 = paired remote holding control (0 none, 1–4), bit 7 = ST-1 in fault. Otherwise zero |
| 28 | 8 | See *Value field* below |
| 36 | 4 | Sender boot token, random nonzero per boot |
| 40 | 1 | Distance in metres set on ST-1, in controller packets; 0 = not set. Must be 0 from start and finish posts |

v4 added byte 40 so lane displays can show average speed. v5 added the Remote role, the Command kind and the control-holder byte. Reflash every unit together: a unit on an older version cannot decode newer packets.

ST-1 sets the Ready flag in its Status only when an ARM would be accepted right now (sensors ready, beam clear, room in the effort queue). Start and finish posts ignore it.

## Remote commands (v5)

A remote sends only Command packets, unicast to its paired ST-1, which answers each one unicast. The remote copies ST-1's latest session, attempt, mode and lane mask from its Status into every Command. The command itself is packed into the value field:

| Bits | Field |
| --- | --- |
| 0–7 | Code: Hello=1, Arm=2, Go=3, Cancel=4, Reveal=5, TakeControl=6, Pair=7 |
| 8–15 | Reply: Request=0 (remote to ST-1), Accepted=1, Refused=2 |
| 16–23 | Reason when refused: NotPaired=1, NotInControl=2, TooLate=3, WrongAttempt=4, WrongSession=5, NotReady=6, Busy=7, Full=8, Bad=9 |
| 24–39 | Argument: milliseconds since the button was pressed (presses); battery percent, 255 unknown (Hello); assigned slot (accepted Pair) |
| 40–63 | Sequence number, per remote boot |

Rules ST-1 applies (in `firmware/controller/include/remotes.hpp`, with tests):

- Only paired remotes are heard. Others get NotPaired. Pair requests are only noticed while ST-1's pairing window is open, and only a person confirming on ST-1 pairs one.
- **CANCEL is accepted from any paired remote, at any time, however old.**
- ARM, GO and REVEAL are accepted only from the remote holding control, for the current session and attempt, if pressed no more than 250 ms ago and only when ST-1 could act on them now.
- TakeControl succeeds when nobody holds control, the asker already does, or the holder has been silent for 30 s. Never during an attempt. CANCEL on ST-1 itself releases control; ST-1's own buttons always work.
- The same sequence number is answered again but acted on once. An older one is refused.
- Hello (every 2 s while awake) reports battery and presence; it gets no reply.

There is no encryption: pairing plus the address allow-list keeps out accidental interference, not a determined attacker. ESP-NOW per-peer encryption is the planned follow-up.

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
| Command | Packed command; see *Remote commands* |

Identity is ESP-NOW source MAC plus announced role/lane and boot token. The system ID is an accidental-mixing filter, not authentication. Reject invalid encodings, other systems, unknown versions, unexpected roles and stale sessions/attempts.

## Required peer behaviour

Track ST-1's timebase from its Status heartbeats. Timestamp each heartbeat in the receive callback, keep the minimum of (local − ST-1) over a sliding window, and correct for drift. Report not Ready until the estimate is established. See specification §5.1.

Send Status every 500 ms, including while running and after completion. Before adopting control it may use session/attempt zero. Ready means eligible for a new arm: a usable timebase estimate, no fault and, in Flying, a qualified clear beam. Start sensors in Standing report Ready without a clear beam, since they only sound cues. On ARM, echo its session/attempt/mode/mask and assert Armed once ready. Keep Armed set through running/completion, adding Running/Complete as applicable; Fault invalidates readiness. Change boot token after every reboot.

ARM must be idempotent: duplicate copies cannot clear an already latched start/finish. A newer ARM creates a new attempt and clears stale results after a qualified clear beam. Remember cancelled/completed identities so delayed copies cannot reopen them. Bind control to the chosen controller MAC. A controller reboot changes session; retire the old session rather than accepting arbitrarily old sessions again. Detect additional controllers instead of switching mid-run.

Riders wait for green at ST-1. ARM is broadcast while acknowledgements are collected; an observed START before all required acknowledgements invalidates the group. Accept GO only in Standing after ARM; accept START only from the matching Flying start sensor (Controller lane 1 for ST-1). Latch the start time once per attempt; duplicates never change it. A finish earlier than the start time is invalid.

On GO, every required start and finish unit stores the GO time and echoes it in Status with Cued set. Start units convert the beep and GO times to their local clocks and sound them from a hardware timer. ST-1 abandons the attempt if any required unit has not echoed the correct GO time by the first beep. Finish units in Flying set Running once START is latched; start units resend START every 100 ms until the matching finish unit shows Running or Complete.

Result comes from the matching finish sensor, with its own role/lane/boot token, the adopted controller session/attempt/mode/mask, Complete flag and elapsed microseconds computed in ST-1 time. Repeating an immutable Result is allowed; the controller accepts the first. Receive-only lane displays filter by lane/session/attempt and clear stale output; they do not announce themselves.

ST-1 sends Reveal once it holds every enabled lane's Result, or when the coach presses reveal now. It freezes the result set and schedules the reveal 1 s ahead, then submits Reveal immediately and every 100 ms until that instant. Busy/failed submissions are retried; the timestamp and mask never change. Each display holds its own lane's Result and shows it at the reveal time, converted through its timebase, not on receipt. A lane whose bit is clear in the finished-lane mask shows DNF. Displays show only their own lane's time. A set bit with no matching Result means missing data, not DNF: show an explicit unavailable indication. A display must receive at least one Reveal before its deadline for a synchronised reveal; receive-only broadcast cannot guarantee delivery.

Reveal uses byte 27 for a finished mask that must be a subset of the enabled mask (zero means all DNF). It must come from Controller lane 1 and have nonzero session, attempt, reveal time and boot token. Other packets still require byte 27 to be zero. All units must be reflashed together when the version changes.

ST-1 keeps configuration locked and eInk refresh suspended during its reveal lead-in. CANCEL and faults abort the scheduled reveal. The 30-second deadline applies to collecting results; a reveal scheduled by that deadline may finish its one-second lead-in afterwards. Required sensor readiness/identity checks remain active until the reveal.

CANCEL is terminal for the matching session/attempt regardless of subsequent configuration changes. Controller Fault status must also invalidate matching attempts/results. Expire control status locally after a provisional three seconds so lost cancellation cannot leave a sensor armed indefinitely. ST-1 sends Status every 500 ms in every state; required sensor status likewise expires after three seconds at ST-1. The group deadline is 30 seconds from ARM. Controller Preparing is represented by ARM packets, without Armed in controller Status.

START and GO carry timestamps, so retries are safe: a late or repeated copy carries the same time. START/Result delivery timestamps do not enter the elapsed calculation. Heartbeat delay biases the timebase, while message loss can invalidate an attempt. Sync error, drift and sensing latency remain to be measured.

## Baseline recovery and clock calibration (v3)

Controller Status carries **Retired (64)** whenever ST-1 is idle after an attempt. This flag is valid only on controller Status and cannot coexist with Armed, Running, Complete or Cued. It retires the advertised attempt even if every CANCEL copy was lost; sensors stop and displays clear. It persists until a newer ARM. Preparing does not carry Retired. Fault status remains terminal. Retirement recovery still requires reception of a heartbeat; complete control loss uses the existing local expiry.

`timebase.hpp` requires actual retained, non-rejected fit points bracketing an event. A block-closing heartbeat alone is insufficient. Event conversion may take several seconds; rejected blocks can extend this until the attempt deadline.

`JNS_TIMEBASE_DELAY_US` is an optional per-device measured minimum heartbeat-delay correction, expressed in ST-1 microseconds. It defaults to zero (uncalibrated), applies in both conversion directions, and survives session resets. Do not copy a simulation delay into firmware. See [calibration and validation](../../docs/baseline-validation.md).

`include/lanetime/cue_player.hpp` is the common bounded-lateness/lease cue engine used by ST-1 and external start posts.
