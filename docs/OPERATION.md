# Operation and recovery

## Normal startup

Connect to the local vest dashboard, sign in using the saved account, and confirm fresh equipment status. Valid saved gyro bias, rider axes and neutral restore after Sensing reboot or sensor-loss recovery. Use Upright → Forward → Neutral on first setup or after moving/replacing the sensor or changing the mount; recalibrate gyro bias if stationary readings drift. Check intended emergency contacts and mobile service. Automatic detection and incident processing continue without an open browser.

The current [crash-detection policy](CRASH_DETECTION.md) is enabled. [Piezo impact detection and administrator trials](PIEZO_IMPACT.md) are enabled; use the 100-point default impact threshold and separate 800-point support criterion. Save an explicit override only after checking controlled trials. The existing IMU crash rule and protected ADC input-health checks remain active.

## Administrator Sensing tabs

**IMU & rider** contains position setup, motion readiness, graphs and a single live snapshot download. The removed separate IMU trial recorder is unavailable; use [Ride Test](RIDE_TEST.md) for sampled browser observations. **Piezo impacts** contains both banks, saved thresholds and movement/tap reports. Arrow keys, Home and End switch tabs; the URL retains the selected tab. Switching preserves unsaved inputs, reports and ongoing trials without sending calibration/threshold commands or changing monitoring. Hidden plots skip redraws; freshness and device-side action checks still apply.

## SMS and GPS

Up to five Philippine mobile contacts are normalized to `+639…`. Main tracks modem availability, SIM readiness and registration separately. An SMS marked accepted has been submitted to the modem/carrier; delivered requires a matching delivery report. Ambiguous submission is unknown and is not automatically repeated after reboot.

Coordinates use Main's latest valid fix when accepting the incident. A fresh fix is Location, a recent older fix is Last location, and a fix older than five minutes is omitted. Missing GPS does not block SMS. A delayed event may use a later position; the message does not reconstruct the original crash site.

Administrator Pairing / SMS Test sends a selected-contact introduction or normal crash-warning SMS. The selected-contact crash SMS does not create an incident or lock cameras. Separate developer-only diagnostic APIs retain labeled system/camera tests.

Use the Pending text messages panel in rider Activity or administrator Incident records/Message history to cancel one pending text or all pending texts. Expand the list to select a recipient. Submitted texts cannot be recalled. A cancellation keeps its incident record and camera protection work. See [incident records](INCIDENT_RECORDS.md) for queue states and restart behavior.

## Sign-in and hotspot recovery

Password verification uses one temporary worker so camera requests and the dashboard remain responsive during sign-in. A verified factory credential can use an immutable cached proof for the same salt and stored hash; the saved factory-password flag alone never authorizes access. Changed credentials are checked again before a session is issued. The worker releases its password and credential buffers and deletes itself after finishing. Sign-in requests are never automatically replayed.

If a previously working camera link becomes completely silent, Main can recover its hotspot even after all clients disappear. It makes at most three attempts per outage, with increasing intervals. Incoming HTTP traffic rearms recovery. A fresh empty hotspot, or phone-only use before camera service has been observed, does not trigger this recovery. A recovery briefly disconnects clients; it is not a restart of Main or Sensing. Normal camera command and heartbeat intervals are unchanged. Wi-Fi client-list queries run through status reporting rather than the fast recovery loop.

## Cameras

Each camera records VGA JPEG into MJPEG AVI without audio, targeting 8 fps and 60-second segments. Up to thirty ordinary clips form its rolling buffer. Low space reclaims older ordinary clips even below that count; protected and unknown files are retained. Protected clips are excluded from rolling deletion and can eventually fill the card.

An incident protects five minutes before and five minutes after the estimated trigger, plus transport margin. A new camera boot cannot provide missing prehistory. Initial receipt is not proof of a complete protected window; completion commonly waits for the post-event segments. After seven minutes, Main marks an unanswered camera overdue and continues delivery. New requests take priority over requests already received; completion checks rotate fairly. Partial footage is retained when full coverage cannot be established.

Videos are under `/clips`; recovery/clip/event metadata is under `/system`. Keep that metadata while retaining recordings. New protected videos use `_PROTECTED_INCIDENT.avi` for crash alerts or protection tests and `_PROTECTED_INTERRUPTION.avi` for recovered interrupted recordings. Older `_LOCKED.avi` names remain supported. Valid GPS or administrator device time supplies UTC+08:00 names; otherwise the recorder uses explicit time-unknown session/clip names. Pairing alone does not establish time.

Wi-Fi loss does not itself stop local recording, but can delay incident commands. SD faults have automatic remount retries. Camera initialization/capture faults retry automatically with bounded backoff; recovery starts a new recording session. To stop cleanly for card removal, use the camera Serial Monitor `s` command, wait for STOPPED, and power off before removing the card.

Read [camera security and recovery](CAMERA_HARDENING.md) for authenticated communication, verified evidence, interrupted-video repair, legacy files and acceptance trials.

## Freshness and storage

The dashboard separates connection from fresh readings. Camera status is fresh for about seven seconds and offline after fifteen; battery measurements expire after six seconds. Browser presentation invalidates positive readiness when updates are stale while retaining appropriately labelled last readings.

Main stores settings/jobs in `sjks01`, incidents in `sv_incident/journal`; Sensing stores `sv_sensor/outbox`, `sv_bmi_gyro/bias` and `sv_bmi_mount/body`. Corrupt durable records are retained for diagnosis rather than erased automatically. Ordinary firmware updates can retain pending incidents and saved recipients/messages.

Use [battery behavior](BATTERY.md), [calibration](CALIBRATION.md) and the [hardware pinouts](../hardware/pinouts.md) for module-specific details.

## Camera recording library

Open Cameras in either dashboard. Each camera reports its saved video count, recording state, latest saved recording time and available memory card space. Normal and protected clips have separate expandable lists with five entries per page. Times use Philippine time. Each protected entry identifies incident protection or interruption recovery; older records with no reliable reason are labelled accordingly. Recording and recovery/protection times are separate. An unavailable capture time or protection date is shown explicitly; older footage is not assigned an invented date. Lists are read on demand from the camera rather than copied into Main memory.

Manage saved recordings acts on the selected camera only. Delete normal clips keeps protected footage. Delete all saved clips includes incident and interruption recordings and requires a separate confirmation. The recording currently in progress and later recordings are kept, so new clips can appear after deletion. Incident protection and recovery take priority; a new incident stops an ongoing deletion. Partial completion is reported with the number deleted. Unreadable metadata and recovery originals are retained for inspection. This action deletes saved videos, not the whole memory card.

**Delete all interrupted recordings** clears verified recovered videos, their saved recovery copies, recognized orphaned recovery files and inactive recording fragments on the selected camera. Incident recordings, normal saved clips, the recording active when cleanup began, the current recording, and unrecognized or other camera files are kept. Confirm this separately for each camera. Cleanup is permanent; use it while parked and keep the vest powered until it finishes. A partial result reports the videos and recovery files removed; a failed response never automatically repeats deletion. This option requires updated camera firmware and is disabled for an older camera.

A readable card that is full of protected recordings remains available for the library while recording is stopped. After enough space is freed, recording retries automatically. Disconnects retain the last reported counts with a connection message. The page waits for an existing camera request before starting another. Result checks retry within a 125 second overall deadline; destructive requests are never automatically replayed by the browser. Switching between Normal clips and Protected clips loads the selected list after the current read finishes.


## Downloading a saved clip

Open Cameras in either dashboard, expand Normal clips or Protected clips, and choose Download beside a saved clip. Confirm the recording pause. One download is allowed across the vest. Download while parked, keep the vest powered, and stay connected to its Wi-Fi. The browser saves the AVI through its normal Downloads interface.

The selected camera finalizes its current segment and gives the transfer priority. Its recording resumes when the transfer ends or is cancelled. An existing operator stop remains stopped. The other camera and Main's detection and alert services continue. New incident protection interrupts the download; footage during an intentional recording pause is not promised.

Progress reports bytes sent by the camera. “Transfer sent” means to check the browser's Downloads; it does not claim the phone has saved the file. Cancel download is available to the session that started it and to an administrator. A stalled connection, expired permission or camera restart ends the transfer automatically. If power or Wi-Fi is lost, retry from the clip list after reconnecting. The retry starts a new whole-file transfer rather than appending to a partial copy.

Downloading reads the chosen clip without modifying it or changing its protection. Normal automatic retention resumes afterward; normal clips remain subject to replacement. The vest stores no partial download files or persistent download mode. Only completed clips can be downloaded. An older camera without download support must be updated first.
