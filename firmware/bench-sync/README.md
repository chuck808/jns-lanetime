# Bench sync test

Measures how closely two units agree on ST-1 time, using real radios and the shared `timebase.hpp`, before any optics exist (specification §5.1, §9).

## Hardware

Three M5Stamp-S3s on carriers, plus jumper wires.

- **Heartbeat board** (`heartbeat` env): stands in for ST-1. It broadcasts v2 Status heartbeats carrying its clock, and every 1.37 s pulses `SYNC_PIN` low for 10 ms, logging the true ST-1 time of each falling edge.
- **Two unit boards** (`unit` env, flashed to both): track the heartbeats, timestamp the falling edge on their own `SYNC_PIN` in an interrupt, then convert it to ST-1 time once it can be interpolated (about 2 s later).

Wire the heartbeat board's `SYNC_PIN` to `SYNC_PIN` on both units, and join all three grounds. The default is GPIO 5. If your carrier doesn't expose it, change `-DSYNC_PIN` in `platformio.ini` to any plain GPIO it does expose, avoiding strapping pins 0/3/45/46, USB pins 19/20 and the Stamp's LED pin 21. All three boards must use the same value.

The MagTag is not needed. Do not run the MagTag ST-1 at the same time: two controllers on the same system ID will confuse the units.

## Running

```sh
pio run -d firmware/bench-sync -e heartbeat -t upload --upload-port <port1>
pio run -d firmware/bench-sync -e unit -t upload --upload-port <port2>
pio run -d firmware/bench-sync -e unit -t upload --upload-port <port3>
pip install pyserial
python firmware/bench-sync/tools/bench_sync.py ST1=<port1> A=<port2> B=<port3>
```

Pulses start 10 s after the heartbeat board boots. Units need about 8 s of heartbeats before they are Ready; earlier pulses are marked `*` and excluded from the statistics.

## Reading the results

| Column | Meaning |
| --- | --- |
| A-B | Sync error between the two units. **This is the figure that matters**, because a result's start and finish come from different units |
| A-ST1, B-ST1 | Each unit against true ST-1 time. Includes the minimum radio delay, a bias that cancels in A-B and in elapsed times; its spread still matters |

Every 20 paired pulses the script prints the mean, standard deviation and worst case. Units also print `TB,ready,kept,spread_us,drift_ppm,samples,dropped` every 2 s, which shows how your real radio environment compares with the simulation.

Things worth trying once the baseline is recorded: move one unit 30 m away, put a phone hotspot or busy Wi-Fi nearby, and power-cycle the heartbeat board mid-run (units should drop to not-Ready and recover).

The simulation's figure for A-B is a worst case of about 22 µs. This test replaces it with a measured one, and its results set the readiness thresholds left TBD in §5.1.
