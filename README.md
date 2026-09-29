# JNS LaneTime

Standalone ESP-NOW timing system for 1–4 BMX lanes, supporting standing and flying starts, coach-operated controls, and per-lane time displays.

## Project status

Prototype development for local club use. The first [ST-1 MagTag firmware](firmware/controller/) is implemented, with live-radio and isolated bench builds. Other device firmware remains to be built. The device images are concept mock-ups, not manufacturing drawings or evidence of tested hardware.

The first build can use development boards with integrated screens/buttons and 3D-printed enclosures. The builder has a Bambu Lab P1S and Fusion 360. ST-1 now uses a MagTag with 3.3 V IR drive and TSSP77038 reception; other board and power choices remain open; the rendered appearance is not a procurement requirement.

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

ST-1 uses Arduino C++ and PlatformIO on the MagTag ESP32-S2. Its README documents wiring, build/flash commands, controls and bench mode. Native tests and all three hardware builds are configured in GitHub Actions. Physical MagTag operation and timing accuracy have not been validated.
