# Firmware overview

One repository, five planned firmware targets. Lane numbers are configuration values, not separate codebases. ST-1 now has an Arduino/PlatformIO MagTag prototype; lane displays now have a C3/MAX7219 prototype; FN-1–FN-4 have C3 optical/GPIO prototypes; external start sensors now have a C3 prototype; spare firmware remains outstanding. Shared code contains the wire encoder/decoder (contract v2). All units work in ST-1's timebase (specification §5).

| Target | Devices | Responsibilities |
| --- | --- | --- |
| [controller](controller/) | ST-1 | Coach controls, readiness/conflict checks, arm/reset, countdown/GO, lane 1 start sensing |
| [start-sensor](start-sensor/) | ST-2–ST-4 | Beam detection, alignment, lane identification, timestamped START, scheduled Standing cues |
| [finish-sensor](finish-sensor/) | FN-1–FN-4 | C3 prototypes for lanes 1–4: buffered finish edges, matching start identity, START/GO timestamps, repeated Result |
| [spare](spare/) | Configurable spare | Lane/role controls, saved assignment, shared start/finish behaviour |
| [lane-display](lane-display/) | LN-1–LN-4 | C3/MAX7219 prototype: scheduled reveal, lane/attempt filtering, DNF/missing-data and stale states |
| [bench-sync](bench-sync/) | Test rig, not a product | Measures timebase agreement on real radios with three M5Stamp-S3s |
| [shared](shared/) | Reusable modules | Wire contract, timebase tracking, radio, optics, timing behaviours and UI utilities |

Passive end-caps require no firmware. FN-1's omitted reflector does not require a separate firmware target. ST-1 is dedicated; the spare cannot replace it. Additional spare assignments remain as described in the specification.

Shared modules should prevent duplicate implementations of start/finish logic, particularly in ST-1 and the spare. The exact source tree can follow the selected framework.
