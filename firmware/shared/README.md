# Prototype wire contract v1

`include/lanetime/protocol.hpp` is the portable encoder/decoder used by ST-1. Other device firmware is still to be built. This contract may change during bench testing and has no JNS_Timing code dependency.

ESP-NOW broadcast uses an exact 40-byte payload. Integers are unsigned little-endian, explicitly encoded; no packed structs or native-endian assumptions.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | ASCII `JNSL` |
| 4 | 1 | Version = 1 |
| 5 | 1 | Kind: Status=1, Arm=2, Cancel=3, Go=4, Start=5, Result=6 |
| 6 | 1 | Sender role: Controller=1, Start=2, Finish=3 |
| 7 | 1 | Sender lane 1–4; controller always lane 1 |
| 8 | 4 | System ID |
| 12 | 8 | Controller session: random nonzero per controller boot |
| 20 | 4 | Attempt: incremented on each accepted ARM |
| 24 | 1 | Mode: Flying=0, Standing=1 |
| 25 | 1 | Enabled-lane mask, bits 0–3 |
| 26 | 1 | Flags: Ready=1, Armed=2, Running=4, Complete=8, Fault=16 |
| 27 | 1 | Reserved, zero |
| 28 | 8 | Result elapsed microseconds; Start/Go local diagnostic timestamp; otherwise zero |
| 36 | 4 | Sender boot token, random nonzero per boot |

Identity is ESP-NOW source MAC plus announced role/lane and boot token. The system ID is an accidental-mixing filter, not authentication. Reject invalid encodings, other systems, unknown versions, unexpected roles and stale sessions/attempts.

## Required peer behaviour

Send Status every 500 ms, including while running and after completion. Before adopting control it may use session/attempt zero. Ready means eligible for a new arm, including a qualified clear beam and no fault. On ARM, echo its session/attempt/mode/mask and assert Armed once ready. Keep Armed set through running/completion, adding Running/Complete as applicable; Fault invalidates readiness. Change boot token after every reboot.

ARM must be idempotent: duplicate copies cannot clear an already latched start/finish. A newer ARM creates a new attempt and clears stale results after a qualified clear beam. Remember cancelled/completed identities so delayed copies cannot reopen them. Bind control to the chosen controller MAC. A controller reboot changes session; retire the old session rather than accepting arbitrarily old sessions again. Detect additional controllers instead of switching mid-run.

Riders wait for green at ST-1. ARM is broadcast while acknowledgements are collected; an observed START before all required acknowledgements invalidates the group. Accept GO only in Standing after ARM; accept START only from the matching Flying start sensor (Controller lane 1 for ST-1). Establish the finish clock once per attempt. Duplicates never change its origin. Finish before start is invalid.

Result comes from the matching finish sensor, with its own role/lane/boot token, the adopted controller session/attempt/mode/mask, Complete flag and elapsed microseconds measured on its own clock. Repeating an immutable Result is allowed; the controller accepts the first. Receive-only lane displays filter by lane/session/attempt and clear stale output; they do not announce themselves.

CANCEL is terminal for the matching session/attempt regardless of subsequent configuration changes. Controller Fault status must also invalidate matching attempts/results. Expire control status locally after a provisional three seconds so lost cancellation cannot leave a sensor armed indefinitely. ST-1 normally sends Status every 500 ms (about once per second in countdown); required sensor status likewise expires after three seconds at ST-1. The group deadline is 30 seconds from ARM. Controller Preparing is represented by ARM packets, without Armed in controller Status.

ST-1 does not retry START/GO. This avoids one source of delayed first acceptance but cannot detect or eliminate radio delay/loss or guarantee every receiver starts. Diagnostic timestamps are not synchronised clocks. The architecture's accuracy remains to be measured.
