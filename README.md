# SmartVest SJKS-01

SmartVest is a thesis prototype for motorcycle riders. It uses vest-mounted motion sensing to identify a possible crash, requests protection of front and rear camera clips, and queues an SMS to saved contacts with GPS coordinates when a fix is available.

The BMI323 evaluates rotation, posture change and sustained tilt together. Piezoelectric sensors provide supporting impact readings. An INA226 and Arduino Nano report battery measurements. The rider and developer dashboards run locally on the Main ESP32.

## Get started

1. Run `python tools/configure.py` to create local configuration headers and unique credentials. Keep the generated credentials file private.
2. Follow the [hardware pinouts](hardware/pinouts.md) and [setup guide](docs/SETUP.md). Compile Main, Sensing, Nano and both camera roles using the [development guide](docs/DEVELOPMENT.md).
3. Join the vest hotspot and open `http://192.168.4.1/`. Complete [position calibration](docs/CALIBRATION.md) and the [hardware acceptance checks](docs/HARDWARE_ACCEPTANCE.md) before a field trial.

## Repository layout

| Directory | Contents |
| --- | --- |
| `Main_and_Cameras` | Main and camera firmware, dashboard sources and built assets |
| `Sensing` | Motion acquisition, detection, impact sensing and battery forwarding |
| `Nano` | INA226 measurement and telemetry |
| `IMU Lab Test` | Separate motion-testing sketch |
| `common` | Shared protocols and the camera pairing template |
| `hardware`, `docs` | Wiring, setup, operation and testing guides |
| `testing`, `tools` | Local tests, build scripts and release checks |

The hotspot starts after the startup indication, saved-state initialization and dashboard listener setup. Cameras record to their own microSD cards. Downloading a clip temporarily pauses recording on the selected camera. See [operation](docs/OPERATION.md), [camera recording](docs/CAMERA_HARDENING.md) and [SMS behavior](docs/SMS_MESSAGES.md).

The detection rule and alerts depend on calibration, motion, GPS reception, compatible 2G service and the saved contact settings. This is a research prototype. Build and software checks do not establish field crash sensitivity or SMS delivery.

[MIT License](LICENSE)
