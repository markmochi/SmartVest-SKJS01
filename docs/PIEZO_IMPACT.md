# Piezo impact detection and trials

The two protected banks use front GPIO34 and rear GPIO35 on Sensing. Each bank combines four elements; software cannot identify which individual element responded. Use the [pinouts](../hardware/pinouts.md) and the actual conditioning circuit.

## Impact registration and optional support

The default hit threshold is **100 ADC rise points above the learned resting level**, including exactly 100. A valid captured peak of **800 points or more** on either bank supplies optional supporting evidence. A peak from 100 through 799 registers an impact without providing this stronger support. These are ADC differences, not force or injury measurements. The thresholds follow the owner's controlled testing on 31 October 2026; crash sensitivity has not been established by that testing.

The BMI323 runs continuously. Its complete motion rule remains authoritative: rotation at least 120 degrees/s for 100 ms, posture change at least 30 degrees and tilt at least 60 degrees for 800 ms within 4.5 seconds, following a quiet reference. Calibration, fresh valid readings and system readiness remain required. An IMU crash may qualify without a piezo hit. Piezo alone cannot create an incident or reduce the IMU thresholds.

A strong hit may match a motion decision from 4.5 seconds before to 1.5 seconds after that decision. A later small hit does not overwrite retained strong evidence within this interval. Every new candidate starts with an empty match. Clipping, drift, settings faults, changed thresholds and acquisition discontinuities invalidate affected support. This temporal association does not establish the cause of an event.

## Administrator: Sensing → Piezo impacts

1. Wait for **Listening for impacts**, keeping both banks untouched while resting levels are learned.
2. Record 15 seconds of ordinary controlled movement.
3. Record 15 seconds of controlled taps. Exercise every element separately and record at least four clear taps per bank.
4. Read the movement/tap comparison. **Use starting values** fills a draft; **Save thresholds** explicitly applies and verifies it on Sensing.
5. Repeat the trials after changes. **Save report** downloads the measurements, thresholds and comparison.

The hit threshold can be set explicitly from 50 through 800, subject to ADC headroom. Suggestions never fall below the 100-point default and cannot exceed 800. The separate support criterion stays at 800. Advice is withheld for interrupted or clipped trials, insufficient taps, changed baselines or overlapping ranges. Suggestions are provisional starting values, not accuracy estimates. Nothing is saved automatically.

Each input must have room above rest for its hit threshold without reaching the saturated 4095 reading. At the 100-point default, rest must be at most 3994. To reach valid 800-point support, rest must be at most 3294. A bank can therefore register small impacts while lacking range for stronger support; check the input circuit rather than treating saturation as an impact. Subsample pulses can still be missed. Test with supported equipment and controlled taps.

The piezo trial buttons do not create incidents. Continuous IMU monitoring remains active during trials, so a qualifying motion event may still produce an alert. A normal lean plus a piezo hit is insufficient.

## Settings and migration

Thresholds use an isolated CRC checked 16-byte `sv_piezo/thresholds` record. Valid versions 1 and 2 upgrade once to version 3 with front/rear thresholds of 100/100, a monotonically increased revision and verified readback. Explicit version 3 settings survive reboot. Corrupt records, failed writes and exhausted revisions are retained and raise a settings fault. An explicit verified save can recover settings where allowed. Other NVS namespaces, calibration, contacts and incident journals are untouched. Earlier firmware does not understand version 3; an application image is not an NVS backup.

Electrical baselines are relearned after restart or recovery. Rider/gyro calibration has separate persistent records. Trial summaries exist only in RAM; download a report before restarting or replacing a trial pair. Closing the browser does not extend a 15-second trial.

## Sampling, transport and installation

Paired ADC inputs are sampled nominally every 1 ms. Baseline tracking has an approximately 1 second time constant and requires an initial stable 2 second window. Each bank captures a 25 ms peak window, requires a return toward rest for 50 ms, and has a 300 ms cooldown. Held input does not repeat as separate hits. Quality, gaps, clipping and freshness are exposed; measure actual cadence on the conditioned hardware.

Piezo status uses diagnostic type 8, payload schema 2 and 180 bytes, carrying the latest impact and separate supporting peak identity/time. Commands 11–15 retain their existing operation numbers. Incident V1 (32 bytes), battery V3 (40 bytes), IMU samples (48 bytes) and camera protocols keep their layouts. Install matching updated Main and Sensing. Camera and Nano implementations are unchanged by this threshold correction.

Administrator writes require authentication, origin/host/CSRF checks, exact fields, valid ranges, current readings and a matching settings revision. Trial starts additionally require bench tests enabled; Stop remains available when that gate is disabled. The panel shares the 350 ms Sensing refresh stream and accounts for request delay. Local preview fixtures simulate devices and do not send real SMS or protect real camera footage.

Software checks establish implementation behavior only. Individual element response, pulse capture, handset receipt, actual camera coverage and labelled event performance require physical acceptance.
