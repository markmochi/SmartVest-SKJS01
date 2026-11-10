# Automatic crash detection

The v1.0 release uses one control: `ARM_CRASH_DETECTION`, enabled by default on Main and Sensing. The old unused false flag and the separate automatic-test switch have been consolidated. Runtime arming still depends on current sensor, calibration, input-health, storage/queue and paired-Main state. An enabled setting does not bypass those conditions.

The motion rule and its numerical thresholds are retained. Piezo registers an impact at a default rise of 100 ADC points on either bank; a valid captured peak of at least 800 points adds optional support. A full IMU event remains sufficient without impact. Valid legacy threshold records migrate once to 100/100. See [administrator trials and threshold limits](PIEZO_IMPACT.md). The thresholds follow owner reported controlled testing; software verification does not establish crash accuracy.

## Motion rule

After establishing a quiet reference, the Balanced rule requires all of these within a 4.5-second event window:

| Evidence | Requirement |
| --- | --- |
| Rotation magnitude | At least 120 degrees/s continuously for 100 ms |
| Event-relative posture change | At least 30 degrees |
| Estimated tilt from neutral | At least 60 degrees continuously for 800 ms |

High acceleration, low acceleration and faster-rotation flags remain diagnostic; they do not independently trigger an event. The posture estimate uses the calibrated rider axes and gyro/acceleration fusion. It is not absolute yaw or a reconstructed crash location.

Only fresh, validated BMI323 samples advance evidence. Rejection, clipping, stale/lost sensing or intervals above 7.5 ms break continuity. The candidate latches until two seconds of quiet upright recovery or an explicit diagnostic reset. Event cooldown remains 1.5 seconds.

## Readiness

Sensing requires a healthy fresh IMU, valid gyro and mounting/neutral calibration, healthy fresh protected piezo ADC inputs, a writable Sensing outbox with free capacity, and a compatible Main lease. Main reports automatic readiness only while its durable incident journal can accept another automatic event. The compatibility capability remains available during backpressure so retained events can still be acknowledged. Main renews that lease every second; it expires after 2.5 seconds. Valid saved calibration restores automatically after reboot or sensor-loss recovery without repeating captures. Fresh attitude and quiet-reference evidence are rebuilt from new samples; old motion evidence is not carried across a sensing discontinuity. Corrupt, incompatible or mismatched saved records keep detection unready until repaired/recalibrated.

Piezo ADC health verifies acquisition of the two conditioned group signals, not impact detection or the presence of each passive piezo element. `PIEZO_IMPACT_DETECTION_ENABLED` is true. Baseline/threshold readiness controls impact classification separately from the protected ADC acquisition-health gate. No piezo hit is required for the full IMU crash rule. Existing input-health gates have not been silently removed.

## Alert transport

A qualifying event enters the acquisition queue, durable Sensing outbox, Main incident journal, SMS queue and two independent camera-protection paths. Normal SMS begins “SmartVest CRASH ALERT,” identifies the rider and asks the recipient to call their personal phone, with 911 as the emergency escalation if there is no answer. A usable position is provided as plain latitude and longitude coordinates without a URL; missing location is stated plainly. No browser or rider-confirmation countdown is required.

Packet type 5, the capability token value, CRC/layout, durable journal schema and internal compatibility flag retain their previous bytes. The source now calls type 5 `AutomaticCrash`. The retained journal flag preserves pending-event replay and queue accounting; it is not a second test mode or SMS-wording switch. Three active type-5 events remain the quota, with the fourth incident slot reserved for ordinary crash frames.

Sensing retries retained events and removes them only after a durable Main ACK and persisted removal. Main commits before ACK and deduplicates retained identities. Worker handoff queues are RAM-only until persisted. Queue blockage/drop/skipped counters remain available.

An uncertain submitted SMS becomes unknown rather than being automatically duplicated. Camera receipt differs from successful protection of its complete recording window. Check actual recipient receipt and playable protected footage when validating end-to-end operation.

To disable automatic motion alerts deliberately, set `SV_ARM_CRASH_DETECTION=0` for both Main and Sensing and rebuild. Existing type-5 outbox entries are retained/quarantined rather than silently erased. The separate explicit developer SMS and diagnostic actions remain authenticated manual actions.

## Response timing

The 500 ms target applies to internal handling after the complete IMU rule qualifies: acceptance, acknowledgement and dispatch preparation. It is a measurement target, not a guaranteed bound during storage faults, retries or backpressure. The Balanced rule itself requires at least 800 ms of qualifying tilt. Camera command polling is nominally one second; complete protected footage requires the post-event window. GPS reception runs continuously, and modem submission, carrier reports and handset receipt have separate external delays. Measure these stages independently under the integrated workload.
