# cinder

ESP-IDF firmware for a round 1.5" AMOLED knob that sits on the desk as a display for [Ember](https://github.com/tarakanof/Ember). It shows Ember's bot face with the current agent status, a Pomodoro timer, the weather and what is playing, and you control it by turning, pushing and swiping.

**Status:** in daily use with Ember. The firmware version is `PROJECT_VER` in `firmware/CMakeLists.txt`. A Bluetooth media remote mode is planned but not started.

## You need Ember

[Ember](https://github.com/tarakanof/Ember) is a self-hosted status server (one Go binary) with a macOS menu bar app. It collects what your Claude Code and Codex sessions are doing and drives desk displays: an AWTRIX pixel clock and this knob. The knob gets everything from Ember: Wi-Fi and pairing come from the Ember app over USB, and then the agent status, Pomodoro, weather, now playing, settings and firmware updates come from the Ember server. Without Ember the knob has nothing to show: it stays on its setup face until it has Wi-Fi, then shows an idle bot face ("Not paired" once an Ember URL is stored).

Use **Ember v0.45.0 or later**. Older servers lack routes that this firmware calls: the device registry and checkin (v0.35.0), the knob view (v0.36.0), now-playing control (v0.41.0), crash reports (v0.44.0) and knob firmware updates (v0.45.0). The latest Ember release is recommended.

## Hardware

VIEWE **UEDX46460015-MD50ET** knob display:

- ESP32-S3R8, 16 MB flash, 8 MB octal PSRAM
- 1.5" 466×466 round AMOLED (CO5300, QSPI), CST820 capacitive touch
- the rotating housing is a rotary encoder, and pushing it is the BOOT button
- Wi-Fi 2.4 GHz and BLE 5; no battery, microphone or speaker

It is sold as the "VIEWE 1.5 inch round AMOLED knob" (for example on AliExpress, item 1005008352585162). Make sure the listing includes the USB adapter board.

The knob has no USB port of its own. It connects over a 10-pin FPC to the `USB-test-MD50-V5.1` adapter board, which wires USB-C straight to the ESP32-S3's native USB (no driver needed). The pin map and board facts are in [docs/board.md](docs/board.md). A 3D-printable desk enclosure is in [enclosure/](enclosure/README.md).

## Features

- **Bot face**: Ember's bot with a mood taken from the agent session that needs you most, and the host name of that session
- **Pomodoro**: an arc around the bezel; push to start or pause, long push to stop
- **Weather** and **now playing** pages, with volume, play/pause and track controls on the knob
- **Pairing over USB**: Wi-Fi through [Improv Serial](https://www.improv-wifi.com/serial/) and a per-device token, both sent by the Ember app
- **OTA updates from Ember**, with automatic rollback if a new image does not check in
- Brightness, page list and timings set from Ember; dimming and pixel shift against AMOLED burn-in

Per-feature design notes and measurements: [docs/features.md](docs/features.md). Pages and input model: [docs/firmware-plan.md](docs/firmware-plan.md).

## Build

You need ESP-IDF 5.5.5. The board support package is vendored in `firmware/components/bsp_knob/`; LVGL 9 and the BSP's driver dependencies come from the ESP component registry on the first build.

```sh
. ~/.espressif/tools/activate_idf_v5.5.5.sh   # or wherever your ESP-IDF export script is
cd firmware
test/host/run.sh                               # host unit tests, plain C, no board needed
idf.py build
```

Builds contain no secrets: no Wi-Fi password, server URL or token is compiled in. `tools/build_release.sh` makes a release build in its own directory and checks the image with `tools/secret_scan.py`.

Release images come only from the tag workflow `.github/workflows/release.yml`. Each [GitHub Release](https://github.com/tarakanof/cinder/releases) is immutable and holds `cinder.bin`, `cinder.elf` and `SHA256SUMS`. Releases after v0.9.28 also carry a build provenance attestation for each file; v0.9.28 and older have none. To check a downloaded file (replace `vX.Y.Z` with its tag):

```sh
gh attestation verify cinder.bin --repo tarakanof/cinder \
  --cert-identity https://github.com/tarakanof/cinder/.github/workflows/release.yml@refs/tags/vX.Y.Z
```

## Flash

Connect the adapter with a USB-C **data** cable. The board shows up as `/dev/cu.usbmodem*` on macOS (USB ID 303a:1001).

1. Back up the factory firmware before the first flash. It is not published anywhere:

   ```sh
   python -m esptool --chip esp32s3 -p <port> -b 921600 read_flash 0 ALL knob-stock.bin
   ```

   Run it in the ESP-IDF environment. ESP-IDF 5.5.5 ships esptool 4.x, which takes the underscore command names (`read_flash`, `write_flash`); esptool 5 accepts them too.

2. First flash, which writes the bootloader, partition table and app:

   ```sh
   idf.py -p <port> flash
   ```

3. Later flashes need the app only:

   ```sh
   idf.py -p <port> app-flash erase-otadata
   ```

   `erase-otadata` makes the knob boot the new app even if an OTA update left it on the other slot. It keeps Wi-Fi settings and pairing.

## Set up and pair

On first boot the knob shows a setup face. Connect it over USB to a Mac running the Ember app: the app sends the Wi-Fi network over Improv Serial, then pairs the knob by sending the Ember URL and a device token over the same link. After that the knob checks in with Ember every minute and gets its settings from there.

To factory-reset the knob, hold it down for 10 s, then turn it right one full turn while still holding.

## OTA via Ember

The knob takes firmware updates from the Ember server it is paired with. It never contacts GitHub or any other server. Upload a build to Ember with `firmware/tools/publish.sh`. It reads `EMBER_SERVER_URL` and the owner `EMBER_TOKEN` from `~/.config/ember/producer.env` (set `EMBER_ENV_FILE` to use another file):

- channel `test` (the default): the knob installs it only when you press **Update** in the Ember app;
- channel `release` (`--release`): knobs in Automatic mode install it when idle.

The new image must check in with Ember within 30 minutes or the bootloader rolls back to the previous one. Details: [docs/features.md](docs/features.md) ("OTA from Ember") and [docs/workflow.md](docs/workflow.md).

## Repository layout

| Path | Content |
|---|---|
| `firmware/` | ESP-IDF project: `main/` (app, Ember clients, views), `components/` (host-testable logic, BSP), `test/host/` (unit tests), `tools/` (release, OTA upload, screen snapshot, input simulation) |
| `enclosure/` | OpenSCAD desk puck and an STL generator |
| `docs/` | Board guide, feature notes, firmware plan, build workflow |

Where to read next: [docs/board.md](docs/board.md) for the hardware, [docs/features.md](docs/features.md) for how each feature works, [docs/workflow.md](docs/workflow.md) for building, releases and dev tools. `AGENTS.md` and `docs/llm.md` are condensed context for coding agents.

Issue numbers in the docs and code comments (#N, cinder#N) refer to the original private repository. Ember#N refers to the public Ember repository.

## Third-party material

In this repository:

- `firmware/components/bsp_knob/` is VIEWE's board support package `viewesmart/bsp_knob_15_md50et` 1.0.3 (Apache-2.0, its `LICENSE` is kept next to it), modified for this board; see the note in its README.
- `firmware/main/font_title_bold.c` holds glyphs of Montserrat Bold (Copyright 2011 The Montserrat Project Authors, SIL Open Font License 1.1; the licence text is in [`firmware/main/OFL.txt`](firmware/main/OFL.txt)), generated by `firmware/tools/gen_title_font.py`.
- The OpenAI mark on the bot page comes from [simple-icons](https://simpleicons.org/) 13.21.0 (CC0), and the Claude mascot is pixel art of Claude Code's terminal banner. Both are trademarks of their owners and only name the tool.

Linked into the firmware binaries (fetched at build time, not in this repository; versions in `firmware/dependencies.lock`):

- [ESP-IDF](https://github.com/espressif/esp-idf) 5.5.5 (Apache-2.0; some of its bundled libraries, such as FreeRTOS, lwIP and mbedTLS, keep their own licences).
- [LVGL](https://github.com/lvgl/lvgl) 9 (MIT), including its built-in Montserrat fonts.
- Espressif registry components `espressif/button`, `espressif/knob`, `espressif/esp_lcd_co5300`, `espressif/esp_lcd_touch` and `espressif/esp_lvgl_adapter` (Apache-2.0), and their dependencies under their own licences.
- `viewesmart/esp_lcd_touch_cst820` 1.0.3, the CST820 touch driver (Apache-2.0).

The vendor's datasheets, drawings, demo code and the board's factory firmware are copyrighted by their makers and are not in this repository.

## License

MIT, see [LICENSE](LICENSE). Third-party parts keep their own licences (above).
