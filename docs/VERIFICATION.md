# Verification and acceptance

The build script writes local compiler logs, input hashes and firmware image hashes to the selected output directory. Use those records to identify the firmware used in a device trial.

## Software scope

Use [development commands](DEVELOPMENT.md) and the [test inventory](../testing/README.md) with explicit compiler, library, browser and ESP32 SDK paths. Run output belongs outside source until the final results are selected. Test entry counts describe executed entry points, not coverage percentages.

| Group | Scope |
|---|---|
| Shared assets | Canonical/local protocol copies, pairing header copies, exact embedded gzip payloads and cache identity |
| Sensing and detection | Acquisition, calibration, motion boundaries, readiness, worker/outbox ownership and paired Main round trips |
| Piezo | Inclusive 100 point impacts, separate 800 point optional support, either bank, correlation limits, retained strong evidence, clipping/gaps and verified settings migration |
| Incidents and SMS | Commit before ACK, quotas, readiness, bounded queues, deduplication, modem ownership, ambiguous submission and cancellation persistence |
| Cameras | Authentication, replay protection, scheduling, retention, AVI/checkpoint integrity, interrupted repair and low space faults |
| Battery | Nano to Sensing to Main relay, filtering, freshness, retention and indicator policy |
| HTTP and security | Production parser/transport, pinned SDK String behavior, routing, allocation recovery and scoped 32 bit response fixtures |
| Dashboard | Roles, routes, appearance, responsive layouts, loading/session recovery, cancellation, exports and resource lifecycle |

Host adapters and browser fixtures simulate devices. Accelerated browser checkpoints do not measure physical endurance. Modeled memory and timing do not measure vest heap or radio throughput. Fixture image decoding does not prove playback or coverage on actual camera cards. Optional phases and any retries retain their stated scope in the results.

## Target builds

Pinned tools are Arduino CLI 1.5.1, Arduino ESP32 3.3.12, AVR 1.8.8, ArduinoJson 7.4.3, TinyGPSPlus 1.0.3 and INA226_WE 1.3.0. Main and Sensing target classic ESP32 without PSRAM. Cameras target ESP32 CAM with PSRAM; front and rear differ by role configuration. Nano uses the recorded ATmega328P bootloader selection; confirm the physical board before uploading.

Main's compiled partition CSV must match the supplied file. The build requires at least 8 KiB of headroom below the 1,310,720-byte IDE limit and at least 24 KiB of headroom in each application partition. These limits cover the firmware image size. Use the hardware acceptance procedure to measure runtime heap and stack margins. Preserve configuration, keys, partitions, calibration, pending records and camera metadata during ordinary updates. Follow the [setup procedure](SETUP.md).

## Physical acceptance

Use the [hardware protocol](HARDWARE_ACCEPTANCE.md): at least 65 minutes with two clients and both cameras, plus a full 20 minute Ride Test export beyond the previously reported failure point. Record serial heap/HTTP counters and every failure or recovery. A successful host response is not proof that the previously reported physical HTTP 503 issue is resolved.

Separately establish SIM/registration and actual handset receipt, INA226 current path/reference accuracy, each piezo element's response, playable SD footage and protection coverage, labelled nonhuman event performance, GPS reference error and endurance. Preserve failed, missing and unknown outcomes. Normal rides do not establish crash sensitivity or overall accuracy. No physical upload or hardware result is implied by software checks.
