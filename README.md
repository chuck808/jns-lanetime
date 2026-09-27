# JNS LaneTime

Standalone ESP-NOW timing system for 1–4 BMX lanes, supporting standing and flying starts, coach-operated controls, and per-lane time displays.

## Project status

Design and prototype planning for local club use. No firmware has been implemented yet. The device images are concept mock-ups, not manufacturing drawings or evidence of tested hardware.

The first build can use development boards with integrated screens/buttons and 3D-printed enclosures. The builder has a Bambu Lab P1S and Fusion 360. Exact boards, displays, power arrangements, and optical parts remain to be selected; the rendered appearance is not a procurement requirement.

## Design

- One to four lanes, each with its own start and finish events in Flying mode.
- Shared countdown and GO in Standing mode, operated from ST-1's physical controls.
- Retroreflective 940 nm IR sensing with 38 kHz modulation.
- Dedicated lane result displays LN-1 through LN-4.
- No subscription, rider profiles, or dependency on JNS_Timing code.
- Cruiser bikes excluded; phone control and handheld remote deferred.

Read the [build specification](docs/build-specification.md) for agreed requirements, proposals, and open choices. The [firmware overview](firmware/README.md) maps devices to targets. The [mock-up gallery](images/README.md) records the current physical design direction.

## Repository layout

| Folder | Contents |
| --- | --- |
| `docs/` | Build specification and design decisions |
| `hardware/` | Future parts lists, wiring, and enclosure designs |
| `images/` | Current device concept mock-ups |
| `firmware/` | Five planned firmware targets and shared modules |

## Timing questions still open

Starting a finish timer when a radio START arrives introduces delivery-delay error. Delay and variation must be measured. Attempt identifiers prevent duplicate restarts but do not solve delayed first receipt. No timing accuracy is claimed yet.

## Development baseline

MCU models, framework, pin assignments, and build configuration are intentionally unselected. The firmware folders currently contain responsibility notes only. Do not assume a working build or an agreed parts list from this scaffold.
