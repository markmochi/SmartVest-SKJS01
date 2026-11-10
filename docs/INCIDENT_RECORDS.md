# Incident records and board communication

The administrator's Incident records and the rider's Activity show the same recorded crash alerts. Riders see ordinary names such as Location and Emergency text messages. The administrator also sees the installed module names. Bench diagnostics are excluded from rider activity.

Each alert has its own bordered card and highlighted header, with a clear gap before the next alert. Equipment names use neutral labels; results use separate status badges. Emergency contacts start collapsed and can be expanded independently. An expanded contact list stays open during live refresh; a fresh visit starts collapsed.

An alert begins with the saved date and time in Philippine time (UTC+08), followed by movement peaks, saved location, emergency text outcomes, associated impact readings, both camera outcomes and the battery estimate. New records capture evidence when Main accepts the event. Live readings from a later visit never replace those values. SMS and camera outcomes update only when their requests progress or are confirmed.

## Clock and earlier records

GPS time takes priority. A successful dashboard sign-in supplies the phone/computer clock as a fallback. The clock anchor is held for at most 24 hours and must be reacquired after Main reboots. A known recent transport delay is subtracted to estimate detection time. Otherwise the card identifies the time Main recorded the alert. Without a valid clock, the card says **Date and time unavailable**; uptime and the browser's present time are never substituted for historical time.

The valid previous V1 incident journal is read without losing its incident identities, recipients, deduplication tombstones or outcomes, then written in V2. Earlier records lack dates, movement peaks, module snapshots and contact names; unavailable fields remain explicit. A corrupt journal is retained for diagnosis rather than erased. V2 is not readable by older firmware. Preserve NVS before a deliberate downgrade. The journal remains bounded to four incidents, with existing pending-event and eviction rules.

## Meaning of results

- **Not detected:** the system did not have a current module response when it saved the alert. This does not prove a physical part is disconnected. A GPS response without a usable position is shown separately. A saved last-known position is labelled accordingly.
- **Movement:** acceleration and rotation peaks carried with the event, with simple units and an explanation of normal gravity. These are measured motion, not a diagnosis or a severity score.
- **Emergency texts:** each saved contact has its own outcome. Sent to the mobile network does not establish handset delivery. Delivery requires a matching delivery report. Ambiguous submissions remain unknown after a restart and are not replayed automatically. Empty contact lists are stated explicitly.
- **Impact:** only a fresh matching Sensing candidate report can attach front/rear impact results. No matching report means the result was not recorded. No qualifying impact means the report was available but neither bank qualified. ADC readings cannot establish whether every individual piezo element is physically attached.
- **Cameras:** request receipt, completed protection and incomplete footage are separate outcomes. Recording protected means the camera reported completion of its incident window; actual playable SD footage still needs inspection.
- **Battery:** the event's voltage-based charge estimate, when available. It is not a live value or calibrated capacity measurement.

## Sensing board page

The communication summary is visible above both the motion and impact tabs. **Communicating** requires a fresh Sensing heartbeat and a current session-bound live response, proving a return path. **Connection incomplete** means Main receives a heartbeat but has not confirmed two-way data. **Not communicating** means no current heartbeat. A stale browser request shows **Status unavailable**. A working board link can coexist with a motion-sensor fault; both are stated separately.

The redundant calibration trial recorder has been removed from the dashboard, command handler, transfer logic and Sensing RAM. Use Ride Test for observations. Gyro calibration, three-step position setup, piezo threshold trials and live snapshots keep their existing purposes. Detector thresholds, arming, alert transport and camera recording policy are unchanged.

## Pending texts and cancellation

Rider Activity, administrator Incident records and administrator Message history include a compact Pending text messages panel. Expand View pending texts to see each recipient and use Cancel text, or use Cancel all pending texts. The queue list starts collapsed so alerts remain easy to scan. The panel distinguishes waiting for mobile service, waiting for a ready SIM, waiting for coverage, preparing to send and awaiting a send result.

Crash-alert messages deliberately retain their place while mobile service is unavailable. They do not use the normal non-emergency queue expiry. A waiting alert can therefore remain until mobile service returns or a user cancels it. The cancellation controls apply to incident recipients and manually queued texts.

A cancellation is committed before an active text-input transaction is abandoned. Incident cancellations persist in the existing journal; restarting Main cannot recreate their pending jobs. Cancelling one contact leaves the other contacts unchanged. Cancellation keeps the incident record, saved evidence and camera protection requests. An already submitted text cannot be recalled; it continues to its real submission/delivery outcome. Such entries have no cancellation button. A failed or partial storage write is reported with the actual cancelled and unconfirmed counts, and the queue refreshes.
