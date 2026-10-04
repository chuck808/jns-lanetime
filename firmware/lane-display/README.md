# Lane result display

LN-1, LN-2, LN-3, and LN-4 share this target with the lane fixed by build environment. These are separate from the small lane-identification screens on sensors.

Receive-only. Track ST-1's timebase, hold the matching lane's Result, and show it at the Reveal instant so all lanes appear together; show DNF if the Reveal mask says the lane did not finish. Show only this lane's time, never a ranking. No running clock: during an attempt, show lane state only. Clear stale output after a timeout. See specification §8.1.

Existing 8×8 RGB matrices may stand in for testing; the final digit hardware is open (specification §8).

Implementation has not started. See [build specification](../../docs/build-specification.md).
