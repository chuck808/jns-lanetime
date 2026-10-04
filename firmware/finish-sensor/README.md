# Finish sensor

FN-1 through FN-4, one target with the lane fixed by build environment. FN-1 uses this target despite having no reflector.

Track ST-1's timebase (specification §5.1). After ARM and a qualified clear, record every qualified break in a small ring buffer. Latch the start time from START (Flying) or GO (Standing) and report Running. The finish is the first recorded break later than the start time. Convert it by interpolation once the next block minimum exists, compute elapsed time, and publish the Result every 500 ms until the next ARM or a cancel. Never resume after a reboot.

Behaviour and state machine: [build specification](../../docs/build-specification.md) §5.4. Implementation has not started.
