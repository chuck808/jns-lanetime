# JNS SPLIT development plan · 19 November 2026

## Repository boundary

- jns-lanetime: controller, timing sensors, displays, readers, wiring and hardware tests. Keep the existing repo name/URLs for continuity.
- jns-split-web: coach/club/rider UI, application models, future Supabase migrations and web tests. Independent Vercel deployment.
- Version contracts at both ends. Current v1 is a JSON-lines **bench scan** contract, not the timing radio protocol. Do not change the current 40-byte radio packets to accommodate it.

## Milestone 1 — supplied now

The web simulator and standalone Core2 reader probe are deliberately independent of live timing. A physical UID can drive the same simulator path as a synthetic UID. No physical green acceptance or success beep is emitted, because no controller has approved registration. No production protocol or source-code reuse from Sportiduino is included.

## Milestone 2 — coached RFID integration

Define a separately versioned registration radio protocol. Bind reader identities to lanes and installation; distribute a test roster to ST-1. ST-1 must validate, deduplicate and acknowledge assignments. Replies bind request, reader and current controller boot/session. Rejected/expired responses never display acceptance. Freeze ownership at arm, consume assignment at terminal outcome. Use bounded retries and reject stale requests. Keep packet callbacks short and timing independent of networking.

Measure shared-channel Wi-Fi/ESP-NOW coexistence using an access point fixed to channel 6. Test interference, reconnects and lost acknowledgements before cloud uploading during live timing. A two-ESP32 gateway remains an option.

## Milestone 3 — Supabase club foundation

One current home club per rider; multiple separately approved coach links. Club admins enrol riders and issue/revoke/replace tags. A rider does not need a login to train. Visiting coaches receive session access; ongoing history access requires its own grant. Later invite guardians/riders into existing profiles, avoiding duplicate identities.

Implement Supabase Auth, invitations, migrations and RLS together. Start with clubs, user memberships, riders, tag assignments, coaching grants, sessions, attempts and audit records. No service-role key in browser or reader firmware. Device ingestion needs scoped credentials, payload validation and idempotency. Test denied cross-club access and revoked grants. Safeguarding paperwork remains with the club; define minimal profile/consent references and retention before real records.

## Milestone 4 — online prototype recording

MagTag prototype uses internet uploads with a bounded RAM queue, stable result IDs, acknowledgement after cloud persistence, and visible pending/saved/offline states. Queue full must be explicit; stop accepting new recorded efforts rather than silently dropping records. Power loss can erase pending RAM records. This is a deliberate deferral of durable offline recording in earlier documents, not equivalent protection.

Keep timing hardware authoritative; web refresh/disconnection cannot reassign an armed rider. Save outcome and frozen distance/start type. Provisional results do not become personal bests before final outcome. Corrections retain original data and an audit trail. Support completed, cancelled, DNF and invalid/equipment-error outcomes separately.

## Later hardware

Persistent local journal on SD-equipped hardware, replay after outages and power loss, capacity/error handling and verified backup/export. Low-power standalone readers inspired by Sportiduino's duty cycling; target a normal club season, annual replacement a stretch goal. Define hours/week and taps/session, then measure consumption. AA versus AAA, radio wake-up, polling responsiveness and enclosure/tag reliability require measurement. Timing sensors have separate power/responsiveness constraints.

Free-ride independent lanes and advanced analytics remain later work. Do not infer peak velocity, acceleration or fatigue from one distance/time pair. UI uses 'rider' for the BMX pilot; underlying identity can support other sports.
