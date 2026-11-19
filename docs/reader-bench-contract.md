# Reader bench contract v1

UTF-8 JSON, one object per line, 115200 baud. Current implementation is read-only USB telemetry into a simulator. This is NOT an authenticated radio or cloud ingestion protocol.

```json
{"version":1,"type":"scan","readerId":"core2-001122334455","requestId":"7CFE00012345678989ABCDEF-1","lane":1,"uid":"04000102030405"}
```

- version: exactly 1; type: exactly scan.
- readerId: stable board identifier, 1–80 ASCII letters/digits/underscores/hyphens.
- requestId: random boot token plus incrementing presentation number; same request ID must not represent another presentation.
- lane: integer 1–4; configured in firmware, not chosen by the rider.
- uid: full uppercase hexadecimal UID; 4, 7 or 10 bytes (8, 14 or 20 characters). Preserve leading zero bytes. The UI normalises spaces, colons and hyphens when entering tags.
- Maximum line length 512 characters. Ignore boot diagnostics/non-JSON lines.
- Only one tag at a time. A presentation ends after 500ms of confirmed absence; communication errors do not count as removal. This heuristic needs bench validation against weak reads and enclosure placement.
- The browser's retry key is readerId/requestId. A retry with modified data is rejected. A replay returns the original decision without recreating a consumed assignment.
- Simulator state, request cache and results last only for the current tab. Resetting the page starts a new test session.
- No acknowledgements are sent over USB. The Core2 says 'Tag read - not checked in'. Its output is not a claim that ST-1 accepted a rider.

Future radio protocol needs current-controller-session binding, freshness, trusted reader/lane configuration, acknowledgement matching and bounded retries. Do not use this bench format as a substitute.
