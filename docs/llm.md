# Knob display: agent context

VIEWE **UEDX46460015-MD50ET** (also MD50E): ESP32-S3R8 in a Ø51.7 mm round housing, 1.51" 466×466 AMOLED, capacitive touch, rotating housing = encoder, push = button. Bought on AliExpress (item 1005008352585162). Human guide: `board.md` (cited, detailed); enclosure page: `enclosure.html`. Pins and versions were fact-checked against the schematic, spec PDF and BSP on 2026-10-02.

Issue numbers written as #N or cinder#N refer to the original private repository; Ember#N refers to the public [Ember](https://github.com/tarakanof/Ember) repository.

## Hardware facts

- No battery, IMU, buzzer, mic, speaker, SD or RTC. Power 5 V, ≤150 mA.
- No USB on the knob: 10-pin 0.5 mm FPC → adapter board. The user's adapter is **`USB-test-MD50-V5.1` (202511)**: USB-C wired to native USB (no bridge chip) → `/dev/cu.usbmodem*`, no driver.
- Flash: **16 MB measured** (the schematic's Z25Q64 / 8 MB is wrong for this unit).
- No factory firmware is published anywhere; the user's copy is in `../backup/` (see Open items).

## GPIO map (schematic + ESP-IDF BSP agree)

| Function | GPIO |
|---|---|
| QSPI CS / CLK | 12 / 10 |
| QSPI D0-D3 | 13 / 11 / 14 / 9 |
| LCD RST | 8 |
| Panel power VCI_EN (HIGH before LCD init) | 17 |
| Touch SDA / SCL / RST / INT (CST820, CST816S-compatible) | 1 / 3 / 2 / 4 |
| Encoder A / B | 6 / 5 |
| Button = BOOT | 0 |
| USB D− / D+ | 19 / 20 |

- Vendor README touch table (SDA0/SCL1/RST3) is wrong. The vendor Arduino header polls touch (RST/INT = -1) and names drivers SH8601/CST816S; both work.
- Driver/LVGL width is **472** (6-column offset), height 466. ESPHome: `offset_width: 6`. Visible GRAM columns 6..471, rows 0..465 of the CO5300's 480x480. Rotation: MADCTL has MX/MY only (no MV), so 0 and 180 only; 180 = MX+MY with gap (2, 14), assuming a 480x480 mirror window (unverified on the knob) (`features.md`, "Display rotation").
- Adapter header J2 pins 2/4/5 conflict across sources (pin 2: spec p.5 GPIO38, spec p.9 drawing GPIO18, silkscreen IO4; pins 4/5: GPIO40/39 vs IO8/NC-IO18). Pins 1,3,6-10 (5V, GND, RX, TX, EN, D+, D−) agree. Treat 2/4/5 as unknown until continuity-tested.

## Toolchain decisions

- Custom firmware: **ESP-IDF 5.5.5** + BSP `viewesmart/bsp_knob_15_md50et` 1.0.3, vendored in `firmware/components/bsp_knob/` (needs IDF ≥5.5) + LVGL 9. Not IDF 6.x (untested with BSP). The LVGL-org port `lv_port_viewe_knob_15_espidf` is stale (2025-06).
- Arduino path (vendor demo only): core 3.3.x, ESP32_Display_Panel ≥1.0.3, LVGL **8.4 only**, PSRAM **OPI**, `LV_COLOR_16_SWAP 1`.
- ESPHome alternative: `mipi_spi` `model: CO5300`, dimensions 466×466 with `offset_width: 6`, `psram: octal`, GPIO17 HIGH in `on_boot`. Configs in `../vendor/esphome/`.
- Gotchas: black screen = GPIO17 low; bootloop = wrong PSRAM mode; brightness via the driver's percent API (panel cmd 0x51) or a raw 0-255 WRDISBV (`main/panel_check.c`, encoded as opcode 0x02 in bits 24-31, command in bits 8-15), never a plain `tx_param(0x51)`; the vendor init table is required (the driver default does not work); the BSP `area_rounder_cb` rounds dirty areas to 2 px: windows need even x1/y1 and odd x2/y2 inside 472x466, else a green line shows at the right edge; holding the knob at reset enters download mode; AMOLED burn-in → true black, pixel shift, panel off when idle.

## Firmware status (`firmware/`, ESP-IDF project)

Step 1 (BSP bring-up) **done and verified on hardware 2026-10-02**: `firmware/main/main.c` with BSP 1.0.3, LVGL 9.5.0 (resolved), esp_lvgl_adapter. Boot ≈1.5 s to UI. Verified by the user: colours correct (R/G/B; no manual RGB565 swap needed with LVGL 9 + adapter), 460 px ring sits evenly in the glass (472×466 driver frame, centre x=236), button down/up/long-hold, touch tracking. **Encoder direction: iot_knob `KNOB_LEFT` fires on a clockwise turn** (`KNOB_EVENT_CLOCKWISE` in main.c). Partitions: nvs, otadata, phy, ota_0/ota_1 4 MB each (OTA from Ember since 0.9.16), coredump 64 KB, littlefs storage. Bootloader with app rollback (project version 2) after the one-time USB flash (`workflow.md`). Console on USB-Serial/JTAG. Build and flash: [`workflow.md`](workflow.md). Every feature since step 1 (design, measurements, decisions): [`features.md`](features.md). Note: the BSP registers only button DOWN/UP/LONG_HOLD; double-press must be timed in the app.

## Firmware plan: `firmware-plan.md` (pages, input model, Ember, BLE media remote, build order)

- BLE media remote is in scope: BLE HID consumer control (NimBLE, `esp_hid_device` example), bonded in NVS, works with Mac/iOS without Wi-Fi. The S3 has no Bluetooth Classic, so no audio streaming (A2DP); the board has no audio hardware anyway.
- Decided: **no vibration motor / haptics** (desk build in a printed enclosure).
- Input rule: push-and-turn changes page; plain turn/push act on the page (turn = volume on the Media page).

## Ember integration

The knob cannot take Ember's 32×8 awtrix frames. It is a **pull client**: with a device token one poll of `GET /v1/devices/self/view` (#24), a long-poll when Ember advertises it (#27); otherwise it polls `GET /state`, `/v1/pomodoro/state`, `/v1/weather/state`, `/v1/display/brightness` (no auth) and sends `POST /v1/pomodoro/{start,pause,resume,stop}` with the device token stored in NVS (from Ember's registry after pairing over USB; without one the knob is "Not paired" and makes no authenticated call, and no build carries a token since 0.9.16); with a device token it also checks in (`POST /v1/devices/self/checkin`, #8) and takes firmware updates from Ember (OTA, #10, `features.md`). Push = start/pause, long push = stop, turn = page. Runs alongside the TC001. Details: `firmware-plan.md`. Ember repo: `~/Github/Ember` (read its `AGENTS.md` before changing it).

## Enclosure

Design in `../enclosure/`: `knob_puck.scad` (parametric, 4 variants: a USB-C, b magnetic pogo dock, c Qi, d battery) and `make_stl.py` (stdlib STL builder). Human page: `enclosure.html` (generated from a separate source, not in this repository). The hub is the fixed mount (3× M2.5, 2 pillars, FPC exit), so the puck holds only the hub (ledge + slotted washer) and the body floats 1.0 mm above the deck. Which part turns (likely only the CNC ring) is still to check by hand; the design does not depend on it. Adapter size, hub step lengths and pillar/screw circles are EST until measured. FPC 70±5 vs a ~30 mm direct route: the slack goes into a service loop toward +y. Vendor drawing: Ø51.7±0.5, depth 25.15±0.3, display Ø38.21 (`../vendor/UEDX46460015-MD50ET/2D drawings/*.dwg`, spec p.9).

## Open items

**Resolved 2026-10-02: the power fault was the cables.** The earlier cables did not carry data or power to this adapter; with a working cable the screen runs the demo and the board enumerates as `/dev/cu.usbmodem*` (303a:1001). The adapter is fine; no seller claim needed.

**Measured on the user's unit:** flash **16 MB** (Puya, mfr 0x85, dev 0x2018), so the spec is right and the schematic's Z25Q64 is not what is fitted. ESP32-S3 rev v0.2, 8 MB embedded PSRAM.

**Stock firmware backed up:** `../backup/knob-stock-<date>-<mac>.bin` (16 MB, SHA-256 in `.sha256`, `verify_flash` matched). Content: ESP-IDF v5.3.1 example `example_qspi_with_ram`, built 2024-12-06; partitions nvs @0x9000, phy_init @0xF000, factory app @0x10000 (3 MB); no OTA slots. Restore: `python -m esptool -p $PORT -b 921600 write_flash 0x0 ../backup/knob-stock-<date>-<mac>.bin`.

J2 pins 2/4/5 · adapter board dimensions and hole spacing · which part of the housing rotates (hand check; the puck does not depend on it) · firmware route: ESP-IDF (recommended) or ESPHome.

## Files

`board.md` guide · `firmware-plan.md` · `features.md` (per-feature notes) · `workflow.md` (build, flash, PR) · `../vendor/` (spec V2.0 PDF, schematic, DWG, vendor repo snapshot, ESPHome YAMLs).
