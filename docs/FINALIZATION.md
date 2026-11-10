# Dashboard API and integration contract

Product remains **1.0**. Firmware changes require uploading the rebuilt Main image through the normal preserved-layout update process. No board is flashed by preparation or software checks. Upload matching Main and Sensing builds for the dashboard update. Sensing's numerical Balanced detector and calibration storage remain unchanged. The incident journal now writes V2 and reads the preceding V1 format without discarding its records.

## Main battery estimate

The current battery follow-up requires Main only. Main computes its percentage from validated bus voltage plus the signed shunt drop, independently of the Nano percentage/profile. The existing raw packet layouts remain compatible. The filter advances once per accepted fresh sample; stale or invalid measurements make the live estimate and normal LED unavailable, while the rider retains a labelled last reading. Both dashboard APIs, incident snapshots and the battery LED use the same Main estimate. See [battery behavior](BATTERY.md).

## Correct piezo policy reporting

`GET /api/v1/sensing/imu` retains its existing schema and returns `imu.detector.piezoImpactDetectionEnabled` as `true` or `false` only when a current compatible piezo peer report is available. It returns JSON `null` when that setting is unknown/stale/offline, including after a peer reboot until fresh status arrives. `piezoCorroborationRequired=false` states the implemented policy. Enabled classification, input health, a qualifying IMU decision and end-to-end alert readiness are distinct. Old saved ride records remain readable and retain their originally exported values.

## Bounded read-only diagnostics

Both authenticated administrator endpoints retain their purposes and record fields:

- `GET /api/v1/cameras/monitor`: camera status, timing/retention limits and a page of up to **eight** `logs` entries, newest first. `?view=summary` returns detailed camera status without history and marks `page.omitted=true`. `?view=events` returns the normal bounded history page with an empty `cameras` array. The camera dashboard displays summary responses before collecting one event page per cycle; a history failure does not invalidate a successful live status response. The unscoped endpoint remains available for complete diagnostic exports.
- `GET /api/v1/gsm/trace`: up to **eight** recent `entries`, newest first, plus last-failure metadata. No full `TraceSnapshot` allocation/copy is made for each request.
- `GET /api/v1/gsm/trace?section=failure`: up to **eight** last-failure entries in `lastFailure.entries`. Use the returned failure `revision` when continuing that snapshot. The normal page retains last-failure metadata and `entryCount`; its failure entries are intentionally empty until separately requested.

Every history response includes `page`: `epoch` identifies this Main runtime; `total`, `retained` and `oldest` describe the selected bounded ring; `after` is the effective lower bound; `more` and `nextBefore` identify additional older rows; `reset` reports a reboot, counter reset or changed failure revision; `lost` reports an incremental gap/evicted requested range. Records have `serial` identifiers within their ring/epoch. Total normal camera/modem retention remains 48 each; the last failed modem transaction retains at most 16 entries.

For older pages, send `before=<nextBefore>&epoch=<epoch>`; `before` is exclusive. For incremental updates, send `after=<last completed total>&epoch=<epoch>`. A client must finish its current older-page window before advancing its incremental watermark, deduplicate by serial and retain reset/loss flags. Cursors are strict unsigned 32-bit decimal values; malformed values receive HTTP400. A changed failure revision resets the failure window and must not be joined silently to an older snapshot. Ring/counter rollover behaves as a reset. Never interpret collected RAM history as an unlimited durable journal.

The supplied camera view accumulates the current retained window. Camera and modem diagnostic downloads assemble all retained pages, preserve completeness/loss information and reject a mixed reboot/failure revision. Old unpaged fixtures/saved exports remain readable. Deploy regenerated browser assets and matching Main together; external diagnostic consumers must adopt paging to retrieve more than the first eight rows.

## Allocation diagnostics and boundaries

Large read-only status/IMU/camera/trace trees use an instrumented allocator with a 24 KiB document ceiling. Allocation accounting includes owned JSON allocations; allocator prefixes, TCP, Wi-Fi, other tasks and fragmentation are separate. Response encoding still requires an output String. JSON strings copied from live journals are explicitly owned before releasing the hardware-service mutex. Network writes never hold that mutex.

`GET /api/v1/status` adds `http.responses`: response count, document/output/serialization-length failure counters, last/max encoded body sizes, last measured document allocation peak/failure request size, and free/largest heap before encoding. `lastFailureStage` is 0=none, 1=document overflow/allocation, 2=output reservation, 3=serialization length. These are diagnostic observations, not a promise that arbitrary payloads/low-heap operation succeed. A failed response remains an explicit HTTP503 rather than silently truncated JSON. Hardware-service tasks and incident logic retain their existing authority.

`http.responses.endpoints` retains the last completed measurement separately for `status`, `imu`, `cameras`, `trace` and `rider`, including document peak, encoded size, allocation failure size/stage and heap observations. Building a status response does not erase another endpoint's measurements. Global last-response values and per-endpoint last-completed values describe different instants; correlate them with the matched test workload.

Each endpoint's `responses` count distinguishes an unmeasured endpoint (zero) from a completed measurement whose value happens to be zero.

## Ride Test and privacy

Motion is requested first, followed by at most one due status/camera/trace request per cycle. Healthy nominal intervals remain approximately 2/5/10 seconds for those optional tasks; response latency affects actual sampling. Camera requests omit unused history. Trace pages are fetched incrementally across cycles. Per-endpoint failures exponentially back off to at most 30 seconds while other observations continue.

Moments retains the failed endpoint, HTTP status and whether the error was a transport failure. A server HTTP503 is not recorded as proof of Wi-Fi disconnection. Recovery is tracked per endpoint. The existing browser/origin/session/visibility/cancellation/storage limits remain. Recipient numbers are masked recursively in persisted normal and last-failure trace strings; bodies remain omitted by the modem. Diagnostic downloads intended for administrators can contain private status and must be handled locally.

## Evidence and physical acceptance

Current retained software outcomes and target build/input hashes are in [Verification](VERIFICATION.md). Use the [physical acceptance protocol](HARDWARE_ACCEPTANCE.md) for new device trials. Superseded analysis and release snapshots have been removed from the active workspace.

Software tests use simulated devices. The latest supplied integrated ride exposes real operational symptoms, but exact on-device allocation cause and success after this fix require a new matched-build 20-minute run. SIM readability/registration/handset receipt, total-load shunt routing/reference-meter comparison, individual piezo tests and actual SD footage/window playback remain separate acceptance tasks. No crash sensitivity or overall accuracy is inferred from normal rides alone.

## Piezo support profile

A bank impact registers at a default rise of 100 ADC points. A valid peak of at least 800 points provides optional support for a qualifying IMU decision; neither bank is mandatory and piezo alone cannot create an incident. Editable hit thresholds accept 50–800 with device-side headroom checks. Support remains fixed at 800. Main and Sensing must be updated together because piezo status schema 2 is 180 bytes. Valid settings records from versions 1 and 2 migrate once to version 3 at 100/100 with an increased revision and verified readback; later version 3 overrides persist. Corrupt records are retained and flagged. Historical exports remain unchanged. See [impact detection](PIEZO_IMPACT.md).

## Incident records and removed recorder

Administrator `GET /api/v1/incidents` and rider `GET /api/v1/rider/status?view=activity` use the same durable incident journal. Rider responses expose `activity: {loaded, writable, incidents}` and omit diagnostic test incidents and technical latency/message fields. The administrator response retains technical fields for existing diagnostic consumers. Each record adds `occurredAtUtcMs` (nullable), `timeSource`, `timeBasis`, `detailsRecorded`, `movement`, `location`, `mobile`, `impact`, `cameraPresence`, `battery`, and recipient names. See [incident behavior](INCIDENT_RECORDS.md).

The Sensing live API adds `link.twoWayConfirmed`. The removed RAM calibration recorder has no HTTP route, sample buffer, sample paging or start/stop/read command implementation. Reserved wire identifiers/zero fields remain to preserve the other command numbers and frame layout. Gyro and Upright/Forward/Neutral calibration, piezo settings trials, Ride Test and single live snapshots remain available.

## Pending-text control

`POST /api/v1/messages/cancel` requires the existing authenticated rider-or-administrator session, same-origin validation and CSRF token. Select one manual job with `{id}`, one incident recipient with `{boot, sequence, recipient}` (zero-based recipient index), or all eligible pending texts with `{all:true}`. Extra/malformed selectors receive HTTP400. A stale/completed selection or a text already handed to the mobile network receives HTTP409. The response reports `cancelled`, `notCancelled`, `alreadySending` and a readable `message`; a complete storage failure receives HTTP503, while a partial cancellation reports the actual result. Administrator `jobs/action` retains clear-history and delegates `cancel`/`cancel_all` to this same implementation.

Administrator `incidents`, administrator `jobs` and rider Activity responses include `messageQueue`. Full queue items carry per-contact fields and `cancellable`. Incident/Activity responses use compact `incidents` references `[boot, sequence, recipient, cancellable]`, reusing the frozen names/numbers already in incident records. Legacy diagnostic references can carry their name/number after those four values. Compact manual entries in `items` use `[id, phone, name, state, cancellable]`. The administrator jobs response retains full objects. Both forms use the same eligibility checks. No queue mutation occurs during a read.

Main serializes cancellation with modem processing. Before SMS body/Ctrl-Z handoff it can abandon a selected transaction using ESC and resynchronize command mode. During the result phase it refuses cancellation and retains the actual result. A cancelled journal recipient cannot be restored by an older ordinary job snapshot. Existing arming, detection, UART protocols, camera delivery and storage schemas remain compatible.
