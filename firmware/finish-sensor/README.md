# Finish sensor

FN-1 through FN-4, with lane and role fixed at build time. FN-1 uses this target despite having no reflector.

Track ST-1's timebase from its heartbeats. Latch the attempt's start time from START (Flying) or GO (Standing), and report Running. Capture the finish beam event locally, convert it to ST-1 time and publish the elapsed time to the lane display and ST-1. A finish captured before START arrives is held, and becomes a result only if START for the same attempt arrives within the deadline and precedes it. Ignore duplicate starts and old attempts.

Implementation has not started. See [build specification](../../docs/build-specification.md) §5 and the [wire contract](../shared/README.md).
