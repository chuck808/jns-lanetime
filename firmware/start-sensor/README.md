# Start sensor

ST-2 through ST-4, with lane and role fixed at build time. Drive and read the optical subsystem, show alignment and lane identity, and track ST-1's timebase from its heartbeats.

- **Flying:** send START carrying the qualified beam time in ST-1 time, once it can be interpolated (§5.1). Resend until the matching finish sensor reports Running.
- **Standing:** acknowledge GO by echoing its time with Cued. Sound the three beeps and GO from a hardware timer at the scheduled instants, converted to the local clock.

Implementation has not started. See [build specification](../../docs/build-specification.md) §5 and the [wire contract](../shared/README.md).
