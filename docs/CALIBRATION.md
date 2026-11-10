# BMI323 calibration and live readings

Use the paired v1.0 Main/Sensing firmware. A ready calibrated candidate enters the enabled [automatic detection](CRASH_DETECTION.md) pipeline. The existing numerical rule and calibration requirements are retained. Piezo impact registration defaults to 100 ADC rise points, with optional support at 800 points or more; see [impact settings](PIEZO_IMPACT.md).

For the rider-facing three-step setup and the matching administrator view, see [your riding view and position setup](RIDER_POSITION.md). The pages distinguish a request acknowledgement from a newly saved calibration.

## Acquisition and features

The reference SPI driver uses SCK18/MISO19/MOSI23/CS21, mode0, 100 kHz, a coherent 13-word burst and nominal200 Hz. Configuration is0x70B9/0x70C9, ±16 g and ±2000 deg/s:2048 counts/g,16.384 counts/(deg/s). Temperature is raw/512+23 C only when valid; temperature readiness does not gate motion. The standalone100 ms diagnostic is not a200 Hz pipeline test.

Only fresh, validated motion frames advance calibration, attitude or detector. Duplicate/not-ready polls do not repeat evidence. Reference near-range guards (15.5 g or1950 deg/s on any axis) and hardware saturation flags both mark unusable/clipping-quality motion. Any rejected frame, clipping, discontinuity over7500 us, stale stream or sensor loss breaks detector continuity. Host read time and30-bit sensor time are both exported; missing motion is not interpolated. Sensor time wraps; valid deltas are computed modularly. Host timestamps describe completion of acquisition, not the exact conversion instant.

The gravity-direction estimate integrates all three gyro axes, normalizes before/after correction, and uses the reference0.8 s correction constant with acceleration trust and a0.1 correction cap. It subtracts current gyro bias and transforms through rider axes. Activity follows the reference squared acceleration residual filter. It is not absolute yaw, linear acceleration, velocity or position. Dynamic acceleration may distort apparent tilt.

## Fresh calibration

Saved gyro bias, rider axes and neutral offsets restore automatically after an ordinary Sensing reset/reboot or sensor-loss recovery. The existing NVS records retain their schema, CRC, sensor identity/profile and value checks; mismatched bias/body records do not restore detection readiness. Current sensor health, fresh attitude and the other readiness gates are still required. Startup/recovery does not redefine the rider neutral position or perform a stationary capture automatically. First installation, moving/replacing the sensor or changing the mount: Upright -> Forward -> Neutral. Each operation also captures gyro bias. Forward lean must be15–60 degrees; about20–30 degrees is convenient. Do not move during a capture. Recalibrate gyro bias if stationary readings drift. Explicit calibration reset or erased flash is not an ordinary reboot and does not preserve cleared records.

Capture target240 fresh samples; minimum220 within2 s. Quality checks: mean gravity magnitude strictly0.75–1.25 g; magnitude SD and vector RMS below0.04 g; gyro vector RMS below2 deg/s; mean rate below5 deg/s initially or3 deg/s relative to a valid current-session bias. Clipping, unknown timing or sensor loss fails capture. These limits are not crash thresholds.

Bias storage: `sv_bmi_gyro/bias`,24 bytes; rider calibration: `sv_bmi_mount/body`,82 bytes. Both use dedicated model/profile/schema checks, CRC and readback. Legacy MPU/lab calibration is untouched and never imported. Rider basis must be orthonormal/right handed. Calibration revision is included in metadata. Gyro/body writes are separate: a failed body write invalidates current bias readiness; this is not an atomic two-key transaction. No accelerometer six-position calibration or temperature compensation is provided.

## Live display and captures

Live snapshots are 10 Hz on UART. Rider pages request compact position data about every 100 ms after a reply. The administrator Sensing page shares pose and raw-signal data in one response about every 350 ms after a reply. Both use display-rate smoothing and may miss peaks. Use [Ride Test](RIDE_TEST.md) to record browser observations at up to 4 Hz. A downloaded snapshot is a single live observation, not a full-rate sensor recording.

The Sensing board page starts with a plain-language Main–Sensing communication summary. A fresh heartbeat alone reports an incomplete connection; a current session-bound live response confirms two-way exchange. Motion-sensor health is reported separately. A stalled dashboard response reports that status is unavailable rather than assuming the boards have disconnected.

Balanced candidates require all three conditions within4500 ms: rotation>=120 deg/s for>=100 ms, event-relative posture change>=30 degrees, estimated tilt>=60 degrees for>=800 ms. A quiet reference is required. Individual high-g, fast-rotation or low-g flags are diagnostics. A candidate latches until recovery (upright/quiet2 s) or explicit reset; reset preserves the boot counter and latest candidate summary. A1.5 s event cooldown prevents immediate repeats. The API/snapshot retains only the latest candidate summary, not a durable event journal. Candidate identity is sensor boot plus candidate counter, separate from incident identity.

Four original ride workbooks are included under `Ride Testing Trials` at the workspace root. They contain normal-motion observations and incomplete capture periods; they do not establish crash sensitivity or comparative piezo benefit. The 100/800 thresholds follow the owner’s subsequent controlled testing, not a rewritten interpretation of those older exports.
