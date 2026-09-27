# Firmware overview

One repository, five planned firmware targets. Lane numbers are configuration values, not separate codebases. No executable firmware or build framework has been selected yet.

| Target | Devices | Responsibilities |
| --- | --- | --- |
| [controller](controller/) | ST-1 | Coach controls, readiness/conflict checks, arm/reset, countdown/GO, lane 1 start sensing |
| [start-sensor](start-sensor/) | ST-2–ST-4 | Beam detection, alignment, lane identification, START events |
| [finish-sensor](finish-sensor/) | FN-1–FN-4 | Accept START/GO, local timing, finish detection, result publication |
| [spare](spare/) | Configurable spare | Lane/role controls, saved assignment, shared start/finish behaviour |
| [lane-display](lane-display/) | LN-1–LN-4 | Separate elapsed-time displays, attempt identity, non-result and stale states |
| [shared](shared/) | Reusable modules | Radio, optics, timing behaviours, configuration, and UI utilities |

Passive end-caps require no firmware. FN-1's omitted reflector does not require a separate firmware target. ST-1 is dedicated; the spare cannot replace it. Additional spare assignments remain as described in the specification.

Shared modules should prevent duplicate implementations of start/finish logic, particularly in ST-1 and the spare. The exact source tree can follow the selected framework.
