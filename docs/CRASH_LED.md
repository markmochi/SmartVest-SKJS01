# Crash indicator light — v1.0

Main's existing RGB light gives these automatic signals without an open dashboard:

| Event | Light | Duration |
| --- | --- | --- |
| Main powers on or restarts | Blinking white, starting on; 250 ms on / 250 ms off, six flashes | 3 seconds before sensor/modem startup |
| A new real crash incident reaches Main | Blinking orange, starting on; 250 ms on / 250 ms off | 5 seconds |
| The first emergency SMS for that incident is successfully submitted to the mobile network | Steady orange | A fresh 5 seconds |

The white sequence indicates that Main has powered on. Sensor, modem and HTTP initialization follow the three seconds. Main advertises its hotspot only after saved state is initialized, the dashboard listener is ready and its servicing task has started. This prevents devices from joining during a period when the local website cannot respond. It also runs after a reset and is not a readiness confirmation. All three RGB channels turn off at the end, then the existing indicator policy takes over. The indicator-enable setting controls this sequence as well. The separate optional six-second color demonstration remains disabled for normal operation.

If submission succeeds during blinking, steady orange replaces it immediately on the next normal application tick. A later success can still produce its five-second signal after the blinking has ended. After either signal expires, the current battery indicator or still-running administrator test resumes. Emergency indications take priority over battery colors, startup diagnostics and manual LED tests. Tests keep their original 15-second timeout while hidden; no timeout is extended.

Steady orange means **at least one emergency SMS was sent successfully**, with the modem's `+CMGS` and final `OK` confirmation, or a matched delivery report already showing delivery. It does not mean all contacts received/read it, the rider is safe, or footage protection completed. Delivery reports, other recipients, storage and camera results remain separate in the dashboard. Failed, unknown, queued and sending states do not produce success orange. A later failed delivery does not rewind a signal that already indicated successful submission.

Only real Crash and AutomaticCrash incident frames activate the signal. Manual diagnostic/camera tests, contact introductions, selected-contact SMS tests, camera acknowledgements and merely leaning the rider visualization do not. Both real frame types use the existing incident identity; the type-5 historical journal flag does not suppress the light.

Duplicate incident frames and later recipient/delivery confirmations do not restart a timer. A newer real incident takes ownership immediately; an older incident's late SMS result cannot hide its blinking. With no contacts or no successful SMS, only the five-second crash signal occurs. The crash signal begins after Main retains the new incident in RAM, even if its flash commit fails; this is recognition feedback, not proof that storage or communications succeeded. Existing durable retry/ACK rules remain unchanged.

These short indications are RAM-only. Main reboot does not replay old crash/success colors, nor show success for incidents restored from before that reboot. They do not modify calibration, sensor detection, camera commands, battery readings, incident schema or SMS queue/retry policy. The existing indicator-enable configuration also controls these colors.

Orange mixes full red with 25% green (8-bit green duty 64) and no blue. The ESP32 hardware brightness (SigmaDelta) peripheral at 312,500 samples/second drives green; the visible blink uses nonblocking elapsed-time checks. Existing pins and configured polarity are retained. Output writes happen only on a change. A brightness-channel allocation failure is logged on the existing application console; digital battery indication remains available, but the orange hue degrades and needs attention. Exact hue/brightness depends on the actual LED/resistors and needs physical checking.

## Software and hardware verification

The host suite exercises the actual event coordinator, production signal policy and GPIO/brightness driver through hardware adapters, including early/late success, multiple contacts, retries, new incidents, no contacts, storage failure, reboot, timer wrap, priority and restoration. Main must pass the current flash budget and preserved partition checks. No new dashboard assets or API were required.

Upload Main while preserving its partition table and saved storage, with matching current Sensing and camera firmware. No upload or real SMS is performed by software checks. Physical acceptance should use a securely supported bench fixture and controlled contacts; do not fall from a chair or deliberately crash a motorcycle. Confirm the LED and SMS/camera outcomes independently. Existing labelled diagnostic incidents deliberately do not activate the real-crash signal.

The brightness channel uses the supported [Espressif Arduino SigmaDelta API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/sigmadelta.html). This avoids pulling the larger PWM timer driver into Main. No software brightness interrupt or extra task is used.

Main retains the normal compiler optimization settings and adds only `-fno-exceptions` through its Arduino sketch response file. This removes unused C++ exception unwinding; no per-function optimization directives or unsafe math settings are introduced. Keep the response file when copying or building Main; see [setup](SETUP.md).
