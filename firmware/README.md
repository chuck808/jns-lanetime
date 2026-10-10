# Firmware overview

One repository, five planned firmware targets. Lane numbers are configuration values, not separate codebases. ST-1 now has an Arduino/PlatformIO MagTag prototype; lane displays now have a C3/MAX7219 prototype; FN-1 has a C3 optical/GPIO prototype; external start sensors, other finish lanes and spare remain outstanding. Shared code contains the wire encoder/decoder (contract v2). All units work in ST-1's timebase (specification §5).

| Target | Devices | Responsibilities |
| --- | --- | --- |
| [controller](controller/) | ST-1 | Coach controls, readiness/conflict checks, arm/reset, countdown/GO, lane 1 start sensing |
| [start-sensor](start-sensor/) | ST-2–ST-4 | Beam detection, alignment, lane identification, timestamped START, scheduled Standing cues |
| [finish-sensor](finish-sensor/) | FN-1–FN-4 | FN-1 C3 prototype: buffered finish edges, START/GO timestamps, repeated Result; lanes 2–4 deferred |
| [spare](spare/) | Configurable spare | Lane/role controls, saved assignment, shared start/finish behaviour |
| [lane-display](lane-display/) | LN-1–LN-4 | C3/MAX7219 prototype: scheduled reveal, lane/attempt filtering, DNF/missing-data and stale states |
| [shared](shared/) | Reusable modules | Wire contract, timebase tracking, radio, optics, timing behaviours and UI utilities |

Passive end-caps require no firmware. FN-1's omitted reflector does not require a separate firmware target. ST-1 is dedicated; the spare cannot replace it. Additional spare assignments remain as described in the specification.

Shared modules should prevent duplicate implementations of start/finish logic, particularly in ST-1 and the spare. The exact source tree can follow the selected framework.
