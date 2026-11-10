# Physical acceptance protocol

Physical acceptance is still pending in the retained records. Record actual outcomes before changing the paper from intended behavior to achieved performance. Preserve the original failed ride; save every follow-up as a new dated session.

## Identify the evaluated prototype

Record board roles, compiled-image/input hashes from your local build report and the current source manifest, installed Main/Sensing versions/configuration, saved calibration revisions, piezo thresholds, actual wiring/BOM photographs, SD-card identities and phone/browser/origin. Product `1.0` alone cannot identify the installed source. The Main update must include its matching regenerated assets. Preserve partitions, NVS settings, contacts, calibration, keys and SD metadata; an ordinary update is not an erase-all operation.

## Sign-in and hotspot stability update

Flash the matching Main source/image and reload the dashboard once. Existing camera firmware 1.0.6, pairing keys, accounts and calibration remain applicable. Repeat rider and administrator sign-in with factory and changed passwords while both cameras record and a second client navigates the dashboard. Disconnect during verification, then reconnect and sign in again. Check account reset and password changes against any pending sign-in. An accepted sign-in must use the current credentials and must not interrupt sensor servicing or camera command traffic.

Use the system status JSON fields `http.loginPending`, `http.lastLoginMs` and `http.maxLoginMs`, together with internal free/minimum heap, largest block, reset reason and radio recovery counters. The temporary login task has a 4096 byte stack; creation requires 8192 bytes of allocation headroom above the existing 12 KiB internal-heap reserve. Actual task-stack margin and latency still require device measurement. Test a complete loss of camera/client traffic, recovery without a manual restart, and sustained healthy traffic without spurious hotspot restarts. Automatic recovery is capped at three attempts per silent outage and is not a guarantee against power or physical radio failures.

## First connection after power-on

Repeat cold starts with a phone and PC using both `http://192.168.4.1/` and `http://smartvest.local/`. Join as soon as the SmartVest network appears. Verify that the login page is reachable on this first association, without disconnecting and reconnecting. Repeat with a saved network that joins automatically and the phone's captive sign-in view. Record time to SSID visibility, DHCP address, first HTTP response, AP start/stop counters and whether the device switches networks. The white indication still lasts three seconds; the hotspot is deliberately withheld during setup. A local vest network provides no Internet uplink, so an operating-system Internet warning is separate from dashboard availability. Verify camera reconnect, signed commands and normal recording after each restart.

## Integrated diagnostic acceptance

1. Use matching current firmware and run at least **65 minutes** with both cameras, sensing/battery/GPS/modem services and two dashboard clients. Exercise rider/admin page changes and expiry/relogin. Separately save a full **20-minute Ride Test** export; the logger cap is shorter than the endurance run. Start/stop while stationary.
   For the reported Wi-Fi failure, first leave every module active for at least three minutes, then start Ride Test. Pass the former one-minute failure point and finish the full recording. Repeat with a second client navigating Sensing/IMU, Incident records and other pages. Verify real fresh readings, front/rear heartbeats, SD recording and reconnection after intentionally leaving/rejoining the hotspot. Retain `[RADIO]` service/authentication counters and AP start/stop events; an automatic AP recovery is a recorded interruption, not an uninterrupted-run pass.
2. Before and after, save authenticated administrator status and full camera/modem diagnostic downloads. Inspect `http.responses.endpoints`: each endpoint's measurement count, document peak, encoded size, allocation stage and heap. Do not confuse a pre-encoding heap observation with peak total system memory.
3. Require no allocation/document/reservation/short-serialization failures, resets or watchdog faults during the defined workload. Preserve timeouts, transport failures, HTTP errors and history-loss markers in the export; do not remove failed intervals from the denominator.
4. Require an actual Camera details sheet with received measurements. Check source counters and board uptimes against browser gaps. Logging remains decimated and is not a full-rate IMU recorder.
5. Repeat the diagnostic download with a full retained camera/modem ring. Confirm collected retained histories, page loss/reset indicators and current front/rear status. A full retained ring is not all history since boot.
6. Save a new trial record with the exact hashes/workload and pass/fail outcomes. A successful software simulation does not satisfy this gate.

## Resolve the three physical investigation findings

| Item | Required evidence | Acceptance interpretation |
| --- | --- | --- |
| SIM/registration | Readable installed SIM; stable power; correct EVB/card/holder/interface; current registration; real controlled-recipient result | Working UART is insufficient. Do not bypass readiness or call an absent-SIM run a successful alert test. Record carrier, place and date rather than assuming nationwide coverage. |
| INA226 current path | Actual breakout schematic/continuity; all intended measured load branches through the shunt; reference-instrument comparison under the defined load | Resolve near-zero current before total-load accuracy/endurance claims. Do not invent an offset or use percentage as a substitute for current. Use verified board/pack/converter ratings. |
| Piezo banks/elements | Actual conditioning/protection, repeatable controlled taps at each element, normal-motion and tap-trial records per bank, saved threshold provenance; verify 99/100 and 799/800 boundaries with an appropriate controlled input | A healthy ADC read or enabled flag does not establish element continuity or force calibration. The historical zero readings do not prove either working or failed elements. |

## Sensing evaluation

Keep the approved Balanced baseline unless a separately evaluated change is authorized. Define labelled event episodes and independent timing/video before collecting new trials. Include varied normal riding/handling/lean/braking/vibration negatives and adviser-approved nonhuman controlled positive fixtures. Record mount, orientation, input, calibration, quality flags and observation coverage. Never stage human falls or motorcycle crashes.

Prespecify repetitions, valid/invalid rules, tuning versus held-out episodes and the detection time reference. Report actual event counts and TP/FP/FN, sensitivity/precision and exposure-based false alerts where denominators are defensible. Do not count each correlated sensor row as an independent negative event. State the current rule's slow-fall/upright-impact limitations and include failed/invalid trials.

## Independent notification and footage acceptance

Use consenting controlled recipients and the correct existing test action; selected-contact SMS, diagnostic incident and qualifying automatic incident have different effects. No real message was sent during software finalization.

For each authorized physical trial record: independent event time; IMU decision; Sensing identity/outbox; Main durable acceptance and ACK; GPS source/age; SMS queue/submission reference plus final OK; carrier report if available; actual phone arrival for each recipient; camera intent receipt; verified completion state; actual card files and independent playback/window inspection. Define first-recipient and all-recipient latency separately. An orange indicator or carrier submission cannot stand in for those results.

Allow complete prehistory before evaluating five-minute pre/post footage. Retain both cards' `/clips` and `/system` data. Audit playable AVI content, frame gaps, full window and partial/recovery outcomes. Test supervised interruption/recovery and storage-limit behavior with retained originals and rollback; do not destroy the only evidence copy.

## Other thesis objectives

Measure GPS error against independent reference positions, voltage/current against a stated reference instrument, and runtime/usable energy under a defined simultaneous workload. Record communication loss/recovery and physical indicator observations. Weather/mechanical/wearability claims require their own specified assembly tests. Survey counts, validators, pilot/reliability and respondent results must be actual datasets, distinct from perceived versus technical reliability.

## Trial record fields

| Field | Actual value / evidence link |
| --- | --- |
| Trial ID, date, operator and objective | |
| Board roles, source/image hashes, configuration and calibration | |
| Wiring/BOM, supplies, cards, carrier and phone/browser | |
| Independent ground truth/timing/reference instrument | |
| Workload/input, duration, repetitions and expected outcome | |
| Measured result and missing/invalid/failed intervals | |
| Raw exports, diagnostics, footage and reference readings | |
| Passed / failed / inconclusive / not tested, with reason | |

## Camera library acceptance

After flashing matching Main and front/rear camera firmware, compare each dashboard count and five-item list against its SD card. Check normal rollover at 30 completed clips, locked clips remaining outside that limit, capture time, newly recorded protection time, and a clearly unknown date for older unsynchronized footage. Confirm both roles show recording state and remaining card space.

Use disposable test recordings for deletion checks. Cancel each confirmation first. Then delete normal clips on one camera: its protected clips, active recording and the other camera must remain. Delete all saved clips only after exporting any wanted evidence. Verify the actual remaining files and both reported counts. Interrupt Wi-Fi during result delivery and confirm that the browser checks the original request rather than posting a second deletion. Trigger a controlled incident during pending deletion and verify that protection interrupts deletion.

With a readable card full of disposable protected clips, verify that browsing and explicit deletion remain available while recording is stopped, and that recording resumes when sufficient space is free. Repeat with two dashboards open and record Main free internal heap, largest free block, camera frame gaps and HTTP failures. These device checks are not replaced by host or browser tests.


## Dashboard clip downloads

- With both cameras and two logged-in clients, download a normal and a protected clip from each camera. Compare the saved AVI bytes or SHA-256 with the SD original and play the file. Check Chrome on the rider's phone and a PC, including its native download prompt. Verify direct access to the camera through SmartVest Wi-Fi.
- Confirm that only the selected camera pauses, the other camera continues, Main remains responsive and only one download is admitted across both dashboards. Test a second request for the same and the other camera. Check owner cancellation, administrator cancellation and rejection of another rider session's cancellation.
- Try leaving the camera page and backgrounding the browser while the native transfer continues. Confirm progress when returning, automatic recording resumption after finish/cancel, and preservation of an earlier explicit operator stop. Exercise normal retention at its 30-clip boundary and protected-card capacity without changing protection flags.
- Disconnect Wi-Fi, stop the receiving browser, make the receiver stall, remove power during transfer, reboot Main separately, and retry after reconnecting. A partial PC/phone copy must not be reported as saved or appended to a fresh file. The source clip must remain unmodified by the transfer. Check that boot starts normal recording, with no retained download mode.
- During a download, inject a labelled incident. The transfer must stop, recording must resume, and detection/SMS/protection must follow their existing workflow. Record the actual video coverage; do not treat the recording pause as continuous incident footage.
- Measure Main's free internal heap, largest free block, camera task stack and radio/HTTP errors throughout transfer and recovery. Test a long clip, weak signal, repeated downloads and a full protected card. Host and browser tests do not establish these physical results.

For the camera 1.0.4 clip list recovery update, retain an earlier failed incident while opening both normal and protected lists in both account roles. Confirm five row paging, switching lists during a pending read, navigation away and back, and recovery after a temporary connection loss. With incident protection and alerts finished, verify normal deletion preserves protected clips and confirmed all saved deletion removes only completed recordings. During fresh incident protection, lists may still be read but deletion and download must remain blocked or stop. Confirm an interrupted result read does not repeat deletion.

For camera firmware 1.0.5, verify incident and interruption filename suffixes on both cards and downloads, including existing `_LOCKED` recordings. Interrupt power during a recording after clock synchronization and check that recovered footage preserves its saved recording time. Repeat before any valid clock arrives; both dashboards must report unavailable recording time rather than assign the recovery date. Check that normal deletion retains both protected categories, all saved deletion handles both, and a failed label rename leaves footage protected and readable.

For camera firmware 1.0.6, populate both cameras with normal clips, incident clips, recovered clips, orphaned recovery archives and inactive fragments. Confirm interrupted cleanup is scoped to one camera, clears recognized interruption payloads and keeps normal/incident, unknown and active recordings. Exercise a fresh incident, power interruption, failed unlink and lost browser response during cleanup; check partial outcomes without an automatic replay. During normal operation with both dashboards open and no download or cleanup, measure frame gaps, sensor freshness, camera report timing, Main free heap and HTTP errors. The configured communication intervals and host results are not physical latency measurements.
