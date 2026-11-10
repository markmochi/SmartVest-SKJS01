# Development and release maintenance

## Sources

Arduino sketch directories and their local `src` folders are required. Shared canonical headers live in `common`; sketch-local copies keep each Arduino build self-contained. They are intentionally retained, not accidental duplicates.

```text
python tools/sync_shared.py          # check copies
python tools/sync_shared.py --write  # synchronize after editing common
python -m pip install -r Main_and_Cameras/tools/ui-build/requirements.txt
npm ci --ignore-scripts --prefix Main_and_Cameras/tools/ui-build
python Main_and_Cameras/tools/build_ui.py
python testing/main/test_web_assets.py
```

UI generation requires Node.js/npm (tested with Node.js 24.22.1) and the exact build-only esbuild 0.28.2 and Terser 5.48.0 dependencies in `Main_and_Cameras/tools/ui-build/package-lock.json`. The install command disables package scripts. esbuild consolidates shared runtime and display modules. Terser uses two safe compression passes and module-local name shortening; unsafe transforms and property renaming are disabled; no build library is sent to the browser or stored in ESP32 RAM. Zopfli 0.2.3.post1 builds standard gzip with 100 optimization iterations. These pinned tools run only at build time. A missing/wrong dependency fails before overwriting existing generated output.

On Windows, use Python 3.13 to regenerate UI assets with the pinned Zopfli wheel. The repository includes the generated assets needed for a firmware-only build.

Edit UI inputs in `Main_and_Cameras/ui/shared`, `ui/rider`, `ui/developer` and the page generator. `ui-built` (minified modules), `ui/pages`, `ui/manifest.json` and `src/web/generated/WebAssets.h` are required generated deliverables. Do not remove them from a transfer. Keep the product release identity at **1.0** for this finalized release. `build_ui.py` fingerprints the source JS/CSS/SVG content and adds that hash to the asset URL, so regenerating UI after an edit automatically produces a new cache identity without changing the product version. Regenerate and transfer the pages, asset manifest and generated header together; never hand-edit only a generated output.

The source retains comments explaining units, wiring, concurrency, persistence, parsing and recovery. Obsolete release narration and comments that merely repeat declarations were removed. Comments do not generally affect firmware size; inactive drivers and embedded UI payloads do.

## Software checks

```text
python tools/run_checks.py
python tools/run_checks.py --host
python tools/run_checks.py --host --zig PATH_TO_ZIG
python tools/run_checks.py --browser
```

Use `--only` with root-relative test paths for focused reruns and `--output` for an external results folder. Tests are grouped in [testing](../testing/README.md); `--list` prints the selected inventory and optional checks. Each check runs in a separate process with a 180-second timeout, or 900 seconds for the full pin-profile matrix; `--timeout` overrides this limit. Host tests need a C++17 compiler, ArduinoJson and TinyGPSPlus headers. Set `SJKS_ARDUINOJSON` and `SJKS_TINYGPS` to their `src` directories. Browser tests need `SJKS_PLAYWRIGHT` and `SJKS_CHROMIUM_BINARY` pointing to the local Playwright module and browser executable.

Zig 0.14.1 is supported as a portable host compiler. The runner adapts executable suffixes and syntax-only calls. On Windows, host-only OpenSSL-shaped PBKDF2 and mbedTLS-shaped SHA/HMAC adapters use Windows CNG; firmware continues using mbedTLS. `testing/system/test_host_crypto.py` cross-checks that adapter against Python hashlib. Windows host runs do not claim ASan/UBSan. The camera integrity suite requires Pillow, independently decodes its JPEG frames, and injects checkpoint/recovery faults. Use `--sockets` on POSIX for the actual socket suite. Camera writer checks run with `--host`; `--media` additionally requires FFmpeg and ffprobe playback instead of allowing that phase to report a skip.

The Sensing suite also runs actual acquisition, calibration, detector, queue and paired-Main round trips:

```text
python testing/sensing/run_tests.py --zig PATH_TO_ZIG --main Main_and_Cameras/SmartVest_SJKS01_Main --arduinojson PATH_TO_ARDUINOJSON_SRC
```

Fixture hardware is simulated. These commands do not send real SMS or touch devices. Keep independent acceptance records from actual trials separately from software results.

## Build and manifest

Run `python tools/configure.py` once per deployment. It creates a shared hotspot configuration, separate initial account passwords and distinct camera pairing keys. A second invocation preserves the existing files. Keep the generated headers and `smartvest-credentials.txt` private. The standalone IMU Lab sketch uses the generated `LabWiFi.h`. For a separate access point, edit only that local header.

Install the pinned Arduino CLI, board cores and libraries listed in [release.json](../release.json). Use the ESP32 Arduino board package URL when installing the ESP32 core.

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.12 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install arduino:avr@1.8.8
arduino-cli lib install ArduinoJson@7.4.3 TinyGPSPlus@1.0.3 INA226_WE@1.3.0
```

The public manifest excludes generated private configuration. Use Git and the manifest to verify source files, and local build reports to identify configured firmware images.


```text
python tools/build_firmware.py --cli PATH_TO_ARDUINO_CLI --output BUILD_DIRECTORY
python tools/release_manifest.py --write
python tools/release_manifest.py --check
```

The optional IMU Lab sketch is separate from the five vest firmware targets. Compile it with `arduino-cli compile --fqbn esp32:esp32:esp32 "IMU Lab Test/SmartVest_IMU_Finalization_Direction_Fixed"` after generating the local configuration.

The build tool records every compiled sketch input hash and compiles Main, Sensing, Nano and both camera roles. It defaults to one compiler job to limit Windows memory use; increase `--jobs` only when sufficient memory is available. It changes camera roles only in disposable copies. `--config` supplies an optional Arduino CLI YAML file; `--targets` selects a subset. Main's supplied partition table is checked, and its flash tool requires at least 8 KiB below the default IDE size limit and 24 KiB within each preserved physical application slot. These are separate limits: the IDE uses 1,310,720 bytes, while each supplied application slot has 1,835,008 bytes.

`SOURCE_MANIFEST.sha256` is the one current source snapshot. External dependency versions, API version numbers and wire/schema versions are independent of SmartVest's v1.0 release number; never relabel those protocols as part of product version cleanup.

The manifest maps each delivered asset to its readable input hash and served file. The preview harness serves these exact files, so browser checks exercise the same JavaScript embedded in Main. Keep `ui-built` in transfers; remove `tools/ui-build/node_modules` from release archives and restore it with `npm ci` only when regenerating UI. See [dashboard design](DASHBOARD_DESIGN.md) for interaction and accessibility behavior.

The bounded HTTP transport tests execute production parser/socket scheduling with exact routing/header/response helpers read from the pinned SDK. Set `SJKS_ESP32_CORE` explicitly to the installed ESP32 core 3.3.12 directory; test setup reads it without editing it. Portable byte-queue tests run on Windows; the optional POSIX socket test is separate. Virtual-time request cycles do not measure physical Wi-Fi or CPU throughput.
