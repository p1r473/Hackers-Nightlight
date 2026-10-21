# Lightjack bulb integration

Added 2026-10-21. Laboratory source, not a validated installation release.

This target reuses the GPLv3 WYZE application from this fork and the MIT LED driver from Nick Daria's Wyze loader. It does not use Pwn-Power. The existing board environments remain available; the new behavior is selected by `lightjack_wyze_poc`.

## Changes

- Route `/setcolor` through the actual Wyze I2C driver, GPIO18 SCL and GPIO19 SDA, with blue/green/red/white1/white2 channel order.
- Add `/lightjack` with color, white-channel and all-off controls. The upstream interface and credits remain at `/`.
- Preserve the upstream payload shape: string `brightness` and optional string `warm`, each 0..1024, plus integer `rgb.r`, `rgb.g`, `rgb.b`, each 0..255. Unknown or duplicate keys, missing RGB components, invalid types, trailing JSON, escape sequences and out-of-range values are rejected.
- Limit request bodies to 384 bytes and correctly assemble fragmented input. Invalid requests do not change the requested light state.
- Limit each channel to 128 of the driver's 1023 levels for initial measurements. This is a deliberate test limit, not the LED chip's bit depth or a validated safe operating envelope.
- Preserve white-mode payload semantics: positive `warm` controls white1 and `brightness` controls white2. Physical warm/cool assignment is unverified. A white1 input of 1 selects white mode but scales to zero; the test page uses this when only white2 is requested.
- Turn every channel off before a new output. A write failure attempts all-off, reports failure, and latches a fault until reboot. If the bus fails, physical off cannot be guaranteed.
- Validate the reference Wyze layout and identify an image whose project name is `wyze_loader` in the other OTA slot. Selecting it validates the image format, but does not authenticate its publisher.
- Select that loader for the next boot, intentionally. The loader's existing boot-selection function can select the retained app again without reflashing it. A failure before recovery selection succeeds has no power-cycle recovery guarantee.
- Disable the old `/update` download handler in this target so it cannot overwrite the retained loader. The old targets retain their original updater behavior.
- Wrap Arduino initialization to select recovery and check NVS before Arduino can reach its default-NVS autoformat branch. Initialization errors stop the main task. This does not promise byte-for-byte preservation from normal runtime or PHY-calibration writes.
- Reject direct PlatformIO upload and erase targets before their hardware actions. The standard upload recipe writes more than the app and is inappropriate for the retained Wyze layout.
- Match `WebServer` include-path capitalization to the tracked directory for case-sensitive hosts.

## Build

Use PlatformIO Core 6.1.18 and the pinned packages in `platformio.ini`. From this `WYZE` directory, run `pio run -e lightjack_wyze_poc` to compile only. The platform pin is 6.12.0, with Arduino 2.0.17 / ESP-IDF 4.4.7 for compatibility with this upstream source. This is deliberately separate from the ESP-IDF 6.1 HTTPS diagnostic application. It is not a recommendation to ship this older SDK as a product.

Windows may need a short PlatformIO package path to avoid command-length failures in the old GCC driver. Keep the package cache and project path short if compilation reports `CreateProcess` even though the compiler's minimal C probe succeeds.

The early guard depends on the `--wrap=initArduino` link option and the pinned Arduino core. The final ELF must show `app_main` calling `__wrap_initArduino`. Recheck this and NVS behavior before any framework upgrade. [ESP-IDF 4.4.7's partition manager](https://github.com/espressif/esp-idf/blob/v4.4.7/components/nvs_flash/src/nvs_partition_manager.cpp) returns `ESP_OK` immediately when that storage is already initialized.

Only an app image is a candidate for a later, verified loader session. Do not flash the generated bootloader, partition table or OTA initialization data. Do not use the old checked-in `bins` images for Lightjack. No device has been flashed or tested by this change.

## Host tests

Compile the actual mapper and receiver with the upstream cJSON source:

```sh
cc -std=c99 -DCJSON_HIDE_SYMBOLS -DCJSON_NESTING_LIMIT=8 -Iinclude src/lightjack/lightjack_levels.c src/main/cJSON.c test/test_lightjack.c -lm -o test-lightjack
./test-lightjack
cc -std=c99 -DLIGHTJACK_WYZE_POC -DCJSON_HIDE_SYMBOLS -DCJSON_NESTING_LIMIT=8 -Itest/stubs -Iinclude test/test_lightjack_adapter.c src/lightjack/lightjack_levels.c src/main/cJSON.c -lm -o test-lightjack-adapter
./test-lightjack-adapter
python test/test_upload_guard.py
```

The same test source was run with TinyCC 0.9.27 on Windows. Tests cover malformed bodies, every RGB value at full test brightness, fragmented reads, early EOF/timeouts, all-off ordering, and injected I2C callback failures. These tests exercise application logic with a simulated writer, not the physical I2C bus.

The second test uses the real adapter with host SDK doubles to check early startup, recovery-selection failures, NVS failures, HTTP responses and the driver-fault latch. The upload-guard test simulates command names without invoking any hardware command. Recorded results are in [verification/results.json](verification/results.json).

## Remaining boundaries

This preserves the upstream shared Wi-Fi password and unauthenticated HTTP management. Use only an isolated authorized laboratory. The separate authenticated HTTPS diagnostic application has a different security model; do not attribute its authentication to this target. An optional [SSH remote TCP target](LIGHTJACK-REMOTE.md) now compiles and has host checks. Its default build has no credentials, and no real SSH session, public relay or off-site path has been tested.

Verify the actual bulb model, firmware, chip revision, partitions and full private backup before any write. Then test boot, LED mapping, actual off behavior, I2C failures, power cycles, loader selection, temperatures, memory use and stability. Wyze app compatibility remains unproven. A successful build does not establish any of these hardware outcomes.

## Licenses and sources

- Base: [p1r473/Hackers-Nightlight](https://github.com/p1r473/Hackers-Nightlight), commit `0bac88ffeb195f7378a7c06843b2fff34e7f05a3`, [GPLv3](LICENSE). Lightjack glue, tests and controls carry GPL-3.0-only notices and the change date.
- Driver and partition reference: [p1r473/wyze-bulb-color-pwned](https://github.com/p1r473/wyze-bulb-color-pwned), commit `41ea9f2de79927e192416b868762fd985ff2cc26`, [MIT notice](licenses/wyze-bulb-color-pwned-MIT.txt). Driver changes add allocation checks and a target guard. The original register values and five-channel protocol are retained.
- Existing cJSON 1.7.17 retains its embedded Dave Gamble and contributor MIT notices.
- [PlatformIO platform 6.12.0 release](https://github.com/platformio/platform-espressif32/releases/tag/v6.12.0) records Arduino 2.0.17 compatibility. Exact build evidence and package archive hashes accompany this source snapshot.

The combined target is GPLv3 software. Preserve the MIT notices as well. Distributing a future binary or kit requires fulfilling the applicable corresponding-source and installation-information requirements, not merely linking an unrelated or newer upstream branch.
