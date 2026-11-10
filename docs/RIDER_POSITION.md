# Your riding view and position setup

The rider’s **Overview** and the administrator’s **Sensing board → IMU / calibration** use the same front-and-side 3D motorcycle rider view and the same position setup. The previous calibration buttons and simple posture drawing have been replaced. Sign in, power the vest and wait for a current movement reading. Opening the page never starts a calibration or sends an alert.

## What the view shows

Sideways and forward/back lean come from the movement sensor on the vest, relative to the saved normal riding position. Positive sideways lean means the rider’s right; positive forward lean means toward the handlebars. In the front view, the rider’s right appears on the viewer’s left. The side view faces forward to the right.

The rider bends at the hips and upper body rather than tipping the whole figure forward. The arms follow the handlebars, and the legs keep contact with the motorcycle. These joints and the motorcycle are an illustration: one vest sensor cannot measure the motorcycle’s actual bank, the rider’s hands, feet or head, steering, road direction or yaw. The measured upper-body position and the illustrative motorcycle lean must not be treated as independent measurements. The displayed angle readings remain the reference for the measured lean.

A strong lean is labelled separately from a crash-like movement reported by the detector. Leaning alone does not confirm a crash. **Crash monitoring ready** describes the motion-monitoring path; check the Overview’s separate monitoring, contacts and text-alert status before riding. The browser visualization neither decides nor changes crash detection.

If WebGL graphics are unavailable, the live angle readings and position setup remain available. An unavailable or old position is held still and clearly marked; it is never extrapolated into a guessed movement.

## Save your normal riding position

Use **Set up position** or **Adjust position** while safely stationary, away from traffic. Wear the vest normally, keep its sensor secured in the intended mounting position and hold each position still for about two seconds. Do not deliberately tip a chair or perform a fall.

1. **Sit upright.** Sit tall, look ahead and keep your shoulders level. Select **Save upright position** and stay still until it is saved.
2. **Lean forward.** Bend gently at your hips by about 20–30°, keeping your shoulders level. Select **Save forward lean** and hold it still.
3. **Your normal riding position.** Return to the relaxed posture you normally use while riding. Select **Save normal position** and remain still until setup is complete.

A request being accepted means the reading started. The page advances only after the vest confirms a newly saved position; an earlier saved result cannot complete a new step. If movement or a connection interruption prevents capture, follow the displayed message and retry that step. Complete all three steps before riding. Monitoring may be unavailable during setup, and saving Upright begins a new rider-axis setup.

If the position gradually drifts while the vest is still, open **Position drifting while you are still?** and select **Refresh stillness reading**. Keep the vest completely still. This optional gyro-bias reading preserves the saved rider axes and normal position; it does not replace the three-step setup.

## Saved positions and recovery

Valid saved gyro bias, rider axes and normal position survive ordinary Sensing reset/reboot and sensor-loss recovery. Startup does not redefine the normal position from whatever posture the rider happens to be holding. Live sensor and readiness checks still apply. Missing, corrupt, incompatible or mismatched records stay unready. Erasing calibration or flash removes the saved records.

Repeat setup after moving or replacing the sensor, changing its mount or observing drift that a stillness reading does not resolve. See [calibration and trial capture](CALIBRATION.md) for the engineering checks and retained-record format.

## Updates and smooth motion

Sensing still measures at its existing nominal 200 Hz and sends the existing live position to Main at about 10 Hz. Rider pages request a compact update on a 100 ms target cadence. The administrator Sensing page obtains pose from its combined IMU response on a 350 ms target cadence, avoiding a second polling stream. Both animate between readings at the display’s available animation rate. Display-rate animation is not a higher sensor sample rate.

Main marks the compact pose invalid and returns null angles when the received position is older than 350 ms, unhealthy or not fully calibrated. The browser also accounts conservatively for request elapsed time. Repeated sample identities do not refresh the reading’s age. Only valid new poses received within a conservative 650 ms source/request-age limit update the target. Presentation remains live for up to 1.6 seconds from that accepted source reading, allowing ordinary request jitter without repeatedly blanking the canvas. It then labels the retained pose as Updating position for at most another 600 ms before pausing and removing angle values. Invalid, offline or unhealthy reports pause immediately. Calibration controls retain the stricter 650 ms gate, and Main still checks the 350 ms source limit. Neither duplicate nor delayed packets extend the accepted-pose timer. A sensor reboot clears the old interpolation state.

Adaptive smoothing settles large changes faster than small changes, using approximately 24–68 ms response constants. A small angular deadband reduces visible idle jitter. It does not change detector inputs or thresholds, invent future readings or filter Ride Test observations. Reduced-motion preferences skip the animated smoothing. Hidden pages stop their pose reader and drawing; signing out disposes them. The existing Overview refresh preserves the canvas and an open setup form.

These measures bound stale displays and avoid unnecessary work. Sensor delivery, Wi-Fi/HTTP, other queued requests, the phone’s browser/GPU and display refresh still add latency. This implementation does not promise zero delay or a fixed frame rate on every device. Verify responsiveness on the intended phone and physical vest.

## Access and maintenance

`GET /api/v1/rider/pose` requires a signed-in rider or developer. It returns only pose freshness/angles, limited calibration progress, command acknowledgement and detector state. It omits raw sensor frames, trial downloads, wiring/register diagnostics, private settings, credentials and keys. The full diagnostic APIs remain developer-only.

`POST /api/v1/rider/calibration` requires the same session plus request-token and origin verification. Its body contains exactly one action: `upright`, `forward`, `neutral` or `gyro`. It cannot reset calibration, retry hardware, start/stop trials, download raw samples or reset a detector candidate. Fresh sensor data, an available command channel and the appropriate earlier setup steps are required; Sensing remains authoritative for capture quality and storage success.

This rider-view change updates **Main firmware only**. It uses the existing paired v1.0 Sensing live-position/calibration protocol, including the saved-calibration follow-up. Older installed firmware may first need that compatible Sensing update. No detector algorithm, thresholds, acquisition timing, UART/wire layouts, NVS records, pins or camera/Nano behavior changed for the rider view. Nothing was flashed as part of source preparation.

Software verification uses simulated hardware, browser fixtures and local compiler/build checks. Those checks cover the UI, restricted APIs, freshness, calibration acknowledgements and integration; they do not establish real-world crash accuracy, phone-specific frame rate, physical sensor recovery or actual SMS/camera outcomes. Consult [retained verification records](VERIFICATION.md) and [verification scope](VERIFICATION.md) for completed runs, exact sizes and hashes.
