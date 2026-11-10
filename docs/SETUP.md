# Setup and firmware updates

This is SmartVest v1.0. The four sketches support five boards because the camera sketch is compiled separately for front and rear.

| Board | Sketch | Board profile |
| --- | --- | --- |
| Main | `Main_and_Cameras/SmartVest_SJKS01_Main` | Classic ESP32 Dev Module, PSRAM disabled, Core Debug Level None |
| Sensing | `Sensing/SmartVest_SJKS01_Sensing` | Classic ESP32 Dev Module, PSRAM disabled |
| Nano | `Nano/SmartVest_INA226_Nano` | Classic ATmega328P Nano; choose its actual bootloader/port |
| Cameras | `Main_and_Cameras/SmartVest_SJKS01_Camera` | AI Thinker ESP32-CAM layout, PSRAM, OV3660 sensor |

The finalization build uses Arduino CLI 1.5.1, Arduino-ESP32 3.3.12, Arduino AVR 1.8.8, ArduinoJson 7.4.3, TinyGPSPlus 1.0.3 and INA226_WE 1.3.0. Select these exact core and library versions in Arduino IDE before compiling, or use the pinned CLI setup. Main no longer needs Adafruit INA219 or BusIO. Python 3 rebuilds the dashboard; Node.js plus Playwright and a browser run UI tests. Neither is needed merely to upload the included firmware sources with Arduino IDE.

Keep `partitions.csv` beside the Main sketch. Its two application slots are 0x1C0000 bytes each, NVS begins at 0x390000, and SPIFFS at 0x3A0000. Preserve the installed storage layout and do not erase all flash during an ordinary update. The release build checks the exact partition CSV and the conservative default size budget separately.

## Configuration

- Main `Config.h` contains the network, initial account, SMS and GPIO settings. An administrator login saved through Account takes precedence over the initial developer credentials. Run `python tools/configure.py` from the repository root to create the local headers and credentials before compiling.
- Main and Sensing share the meaning of `ARM_CRASH_DETECTION`, default enabled through `SV_ARM_CRASH_DETECTION=1`. Both boards must run a matching configuration and capability handshake.
- Sensing `PIEZO_IMPACT_DETECTION_ENABLED=true` enables impact classification as optional corroboration. Protected ADC input health remains part of readiness. Saved bank thresholds remain authoritative; source defaults are not proof of installed settings.
- Camera `CameraConfig.h` selects `front` or `rear`; the appropriate fixed key is selected automatically. Upload each board using its own role. Check both identities after upload.
- Camera firmware `SJKS-camera-1.0.3` retains `VERTICAL_FLIP` for both OV3660 cameras to correct upside down recordings. The correction is reapplied after camera recovery. Upload both camera roles, then record a new clip with each camera mounted normally and check that the scene is upright. Existing AVI files are unchanged.
- Nano normal operation is `SMARTVEST_NANO_USB_DIAGNOSTICS=0`, 19200 baud. USB-diagnostics mode uses 115200 and does not send relay packets.

Main defaults to the vest hotspot at `http://192.168.4.1/`, with optional `smartvest.local` and captive sign-in. A lack of Internet does not mean the local vest is unreachable. Home-Wi-Fi mode is station-only: adjust the camera SSID/Main URL as well if switching networks.

## Update sequence

For the camera recording library, upload **Main and both cameras**, selecting the correct front or rear role for each camera. Nano and Sensing do not need this update. Preserve the existing partition layout, settings and camera SD metadata. Reload an already open dashboard once after updating Main. Both account roles have a Cameras page with saved clip totals, separate normal and protected lists, five clips per page, recording times, card space and confirmed deletion controls. Older camera firmware continues reporting status but cannot browse or delete recordings through the new page.

Install **Main and Sensing together** for the 100/800 piezo behavior and readiness correction. Both cameras must use the supplied authenticated camera implementation. Nano remains compatible; a complete new installation uses all five role builds listed in [Verification](VERIFICATION.md).

1. Preserve the existing private configuration and follow the [pinouts](../hardware/pinouts.md).
2. Confirm board types, sensors, COM ports and Main's existing partition layout. Application images are not complete flash or NVS backups.
3. Upload matching Main and Sensing. Isolate or reset the Sensing link during the mixed version transition, then reconnect after both run the matching release.
4. Build/upload each camera with its own front or rear role and matching key. Old bearer based camera firmware cannot authenticate to this Main.
5. Read Main logs at 9600, Sensing/camera logs at 115200 and normal Nano telemetry at 19200.
6. Check restored calibration and readiness. First installation or a changed sensor/mount requires Upright → Forward → Neutral; recapture gyro bias if stationary readings drift.
7. Verify front/rear hit thresholds of 100 after the one time legacy migration, then confirm optional support starts at 800. Future explicitly saved version 3 overrides persist. See [impact settings](PIEZO_IMPACT.md).
8. Check contacts, SIM/registration, GPS, both camera identities/cards and fresh battery readings. Run the [physical acceptance checks](HARDWARE_ACCEPTANCE.md) before claiming integrated operation.

Preserve partitions, NVS/calibration and camera metadata during an ordinary update. Keep this private project and its pairing keys private. No device is flashed or real SMS sent by the build/check tools.

## Main build options

Keep `Main_and_Cameras/SmartVest_SJKS01_Main/build_opt.h` beside the Main sketch. Arduino ESP32 automatically reads this response file; it contains `-fno-exceptions`. The installed core enables C++ exceptions by default, but Main has no exception handlers. Disabling unused exception unwinding preserves flash for the crash light and the physical application reserve. Existing allocation checks remain in source; uncaught allocation/library failures still require recovery, with no new exception recovery promised. Do not add code requiring C++ exception handlers or remove this file without reviewing the build size and behavior. Camera/Sensing/Nano build flags are unchanged.

The RGB crash indicator is included in Main; see [crash indicator](CRASH_LED.md).

For piezo impact readings, trials and saved thresholds, install matching updated Main and Sensing. Start with [Impact sensors](PIEZO_IMPACT.md); the IMU crash rule remains active during these trials.

## Dashboard incident update

Upload matching Main and Sensing builds after this update; preserve existing partitions, NVS and calibration. Main migrates its valid previous incident journal on read. Earlier firmware does not understand the new journal layout, so retain a full NVS backup before any deliberate firmware downgrade. The application binaries are not NVS backups.

GPS supplies the incident clock when valid. A successful dashboard sign-in also supplies the phone/computer's date and time as a fallback; set that device's clock correctly. The dashboard displays Philippine time (UTC+08). Sign in again after a Main reboot if GPS time is unavailable. Without a trusted clock, the record explicitly says date and time are unavailable.

## Dashboard response memory update

Flash Main only for the compact Pairing / SMS status response and persistent overview layout, then reload the dashboard. Pairing controls load separately from live status and retry failed settings reads. Unavailable updates keep the page structure and navigation visible; write controls remain disabled until a current connection and required settings are available. A later failed update retains the previous values as stale.

The Main ESP32 page reports free, minimum and largest available heap values without a USB connection. Check these while running both cameras and opening dashboard pages on battery power. Host memory measurements and the firmware build do not measure the installed board's runtime heap.

## Rider sign-in battery and live-view update

Upload Main only, then reload the dashboard. The installed Nano does not need an update or USB cable. Main removes unnecessary history copies and repeated hashing from sign-in and updates the live-position presentation. Main calculates the filtered percentage from the existing raw bus and signed shunt measurements for the observed 10.6 to 11.65 V source-voltage range. The incoming Nano percentage is ignored. Both dashboards and the physical battery LED use the Main estimate. The developer voltage display is smoothed separately; raw measurements remain in its details. Background reads keep Refresh steady on both dashboards. Existing Sensing and camera firmware remain compatible because the battery packet layout is unchanged. Preserve saved accounts, contacts, calibration, partitions and camera metadata. Custom rider credentials are preserved during ordinary updates.


## Accounts and Reset data

The administrator Account page edits the administrator username/password through `/api/v1/admin/account`. The rider Account page edits only the rider account through `/api/v1/rider/account`. Each endpoint requires its own role, current password, and the normal origin/session/CSRF checks for writes. A credential change ends sessions for that role only. Password derivation remains PBKDF2 with the existing iteration count; saved administrator credentials survive restart and ordinary firmware uploads. The rider factory username `admin` is reserved from administrator renaming so a data reset cannot create a login collision.

Administrator Account also contains **Reset data**. Enter the administrator password, type `RESET`, and confirm. It clears the rider name and contacts, message history, incident records, and Main's recent camera events; it restores the rider username and initial password from your generated configuration and ends rider sessions. It keeps the administrator account, network settings, calibration, piezo thresholds, camera pairing, and recordings on camera SD cards. New camera events continue to arrive. Browser-local Ride Test recordings and exported files are not deleted.

Reset is refused while messages or incident camera protection are pending. A durable reset marker allows an interrupted reset to finish after reboot or storage recovery; the API reports HTTP 202 while completion is pending and HTTP 200 only after the completion write. The Account page checks completion automatically. Incident replay tombstones and the monotonic SMS job counter are retained. Uploading this firmware does not itself reset any data or account.

## Rider first-use setup

New or administrator-reset rider profiles open a three-step setup after sign-in: welcome, emergency contacts, and an optional riding-position preview. Save at least one contact before continuing. Introduction SMS is optional and starts unchecked in setup. The position view reads the same saved calibration as the administrator dashboard; skipping adjustment sends no calibration command. Position adjustment remains available later on Overview.

Completion is saved on Main, not in browser storage. A new phone, sign-out, restart, or removal of all contacts after completion does not restart the introduction. Administrator Reset data clears the completion flag. Existing named rider profiles from older firmware keep their dashboard when this firmware is installed; genuinely empty new/reset profiles enter setup. A setup that has saved contacts but has not completed resumes with those contacts retained.

Removing all contacts after setup disables contact-based alert readiness until contacts are added again. The setup completion endpoint requires a rider session, CSRF validation, a current settings revision and at least one saved contact. Failed completion writes do not mark setup complete.


The clip download feature requires Main and both camera roles from this release. It adds a temporary camera HTTP listener on port 8081 while an authenticated download is permitted. The phone or PC stays on SmartVest Wi-Fi and receives the video directly from that camera. Main carries only control and status data. Use a browser that supports file downloads, allow its download prompt if shown, and verify this direct connection during hardware acceptance. Nano, Sensing, private keys and the partition layout do not change for this feature.

## Clip list recovery update

Upload Main and both cameras with their correct roles for camera firmware 1.0.4. This update prevents old incident retries from blocking clip lists and deletion, allows bounded list reads during evidence verification, and recovers pending requests after navigation. New incident protection still stops deletion and downloads. Nano and Sensing need no update. Reload an already open dashboard once after flashing Main.

## Protection filename and time update

Upload Main and both camera roles for camera firmware 1.0.5, then reload the dashboard. Existing video and metadata remain supported. Verified old protected recordings receive incident or interruption filename labels on the card scan; no video payload is rewritten. Both dashboards show the protection reason and explain unavailable recording or protection times. Nano and Sensing need no update. Preserve the normal partition layout, pairing keys and camera metadata.

## Interrupted recording cleanup update

Upload Main and both camera roles for camera firmware 1.0.6, then reload an open dashboard. Both Cameras pages add Delete all interrupted recordings under each camera’s Manage saved recordings. This also clears recognized recovery copies and inactive fragments; incident recordings and current recording are kept. Older cameras can still browse with the new Main but cannot use this option until updated. Nano, Sensing, pairing keys, calibration and partitions are unchanged.
