# VIEWE 1.5" AMOLED Touch Knob (ESP32-S3), UEDX46460015-MD50ET

Notes for the AliExpress board "AMOLED ESP32 S3 LVGL Arduino 1.5 Inch 466*466 OLED IPS Circular Knob Rotary Round Touch Display" ([item 1005008352585162](https://www.aliexpress.com/item/1005008352585162.html)).

Researched 2026-10-02. "Verified" below means the item matches in at least two vendor sources (schematic, board header, BSP README). Items marked **(unverified)** come from a single source or conflict between sources.

`vendor/` (datasheets, schematic, drawings, vendor examples) is the maintainer's local copy and is not included in this repository; the vendor files come from [VIEWESMART's repository](https://github.com/VIEWESMART/UEDX46460015-MD50ESP32-1.5inch-Touch-Knob-Display) (§1).

---

## 1. Identification

| | |
|---|---|
| Maker | VIEWE (优奕视界 / Shenzhen VIEWE Technology), GitHub/Gitee org `VIEWESMART` |
| Model | **UEDX46460015-MD50ET** (also sold as `-MD50E`; the V1.1 spec sheet calls it `UEDX46460015-WB-A`. They are the same board family.) |
| Resold as | Spotpear "ESP32-S3-1.5-inch-Push-Knob", Amazon "UEAIDISP", plus many AliExpress listings with the same title |
| Not the same as | Waveshare ESP32-S3-Knob-Touch-LCD-1.8, Elecrow CrowPanel 2.1, Guition/JC boards. Those use different pinouts and drivers. |

The AliExpress page could not be read without a browser (the Chrome extension was offline), so the listing itself was not checked. The same title appears on AliExpress US [3256808166270410](https://www.aliexpress.us/item/3256808166270410.html), and the seller's Gitee pointer leads to the VIEWESMART org. Both point to this model. **To confirm on your unit**, check for a Ø~51.6 mm round metal shell with a knob ring, a 10-pin 0.5 mm FPC tail, and a separate small adapter board with USB.

Primary sources:
- GitHub (main): https://github.com/VIEWESMART/UEDX46460015-MD50ESP32-1.5inch-Touch-Knob-Display
- Gitee mirror (the "gitee.com/viewsmart" link is really `VIEWESMART`): https://gitee.com/VIEWESMART/UEDX46460015-MD50ESP32-1.5inch-Touch-Knob-Display
- Product page: https://viewedisplay.com/product/esp32-1-5-inch-466x466-round-amoled-knob-display-touch-screen-arduino-lvgl/
- Official LVGL port (LVGL 9.3, ESP-IDF): https://github.com/lvgl/lv_port_viewe_knob_15_espidf (Gitee mirror: VIEWESMART/lv_port_viewe_knob_15_espidf)
- ESP-IDF BSP: https://components.espressif.com/components/viewesmart/bsp_knob_15_md50et
- Spotpear wiki: https://spotpear.com/wiki/ESP32-S3-1.5-inch-Round-Rotary-OLED-TouchScreen-Knob-Display-466x466.html

---

## 2. Specs & pinout

### Hardware

| Block | Part | Notes |
|---|---|---|
| SoC | **ESP32-S3R8** (8 MB octal PSRAM in package) | dual LX7 @ 240 MHz, Wi-Fi 4 + BLE 5 |
| Flash | **16 MB, measured** on the user's unit (Puya, mfr 0x85, dev 0x2018) | the schematic shows a Z25Q64 (8 MB) part; that is not what is fitted |
| Display | 1.51" AMOLED, 466×466, **CO5300AF-42**, **QSPI**; brightness 450 cd/m² (seller copy of spec V2.0) vs 1000 cd/m² (vendor GitHub copy) | panel P/N `UE015WV-RB24-A021A`. Active area 38.3 mm. Driver/LVGL width is **472** (6-column offset). |
| Touch | **CST820** over I²C | CST816S-compatible. The ESP32_Display_Panel and ESPHome `cst816` drivers both work. |
| Knob | 2-phase rotary encoder (PHA/PHB) | encoder datasheet `EC110101R6D-HA1-011` is in `vendor/` |
| Button | knob press = 6×6 silent switch on **GPIO0** | this is also BOOT |
| Power | 5 V DC, 4.0–5.5 V. About 50 mA with the panel off and 100 mA typical (150 mA max) with it on. | LP3987H-3V3 LDO |
| USB | native USB D+/D− on the FPC, GPIO20/19 | no USB socket on the knob itself. USB is on the adapter board. |
| Not present | battery/charger, IMU, buzzer/speaker, mic, vibration motor, SD, RTC | none appear on the one-page schematic |
| Mechanical | Ø51.7 ±0.5 mm, ~25.2 mm deep, 70 mm FPC tail | drawing on spec PDF p.9 and `.dwg` in `vendor/` |

### GPIO map (verified: schematic, `BOARD_VIEWE_UEDX46460015_MD50ET.h`, and the BSP agree; exception below)

| Function | GPIO |
|---|---|
| LCD QSPI CS | 12 |
| LCD QSPI CLK | 10 |
| LCD D0 / D1 / D2 / D3 | 13 / 11 / 14 / 9 |
| LCD RST | 8 |
| **Panel power enable (VCI_EN)**, must be driven HIGH before LCD init | **17** |
| Touch SDA / SCL | **1 / 3** |
| Touch RST / INT | 2 / 4 |
| Encoder A (PHA) / B (PHB) | 6 / 5 |
| Button / BOOT | 0 |
| USB D− / D+ | 19 / 20 |

> Fact-check 2026-10-02: the vendor Arduino header sets touch `RST_IO=-1` / `INT_IO=-1` (polled, no reset) and names the controllers CST816S / SH8601 (compatible drivers). Only the schematic and the ESP-IDF BSP give touch RST=2 / INT=4.

> The pin table at the top of the vendor README lists touch as SDA=IO0, SCL=IO1, RST=IO3. **That table is wrong.** The schematic, the board header and the BSP all use SDA=1, SCL=3, RST=2, INT=4.

Other schematic labels **(unverified)**: GPIO7 is labelled "LCD-BL-EN" but has no visible route. GPIO21 is labelled "PWM". GPIO38 is labelled TE and is also on FPC pin 2.

### 10-pin FPC (0.5 mm pitch), from spec p.5

| Pin | Signal | ESP32-S3 |
|---|---|---|
| 1 | 5V | – |
| 2 | GPIO38 (adapter silkscreen "PB7") | GPIO38 |
| 3 | GND | – |
| 4 | RX2 | GPIO40 |
| 5 | TX2 | GPIO39 |
| 6 | RX (UART0) | GPIO44 |
| 7 | TX (UART0) | GPIO43 |
| 8 | CHIP-EN | EN |
| 9 | USB D+ | GPIO20 |
| 10 | USB D− | GPIO19 |

Adapter board: the VIEWE README describes USB-C with native USB. The LVGL port README describes micro-USB with a **CH340G** bridge. Which one you get depends on the kit revision.

**User's kit (checked from photo, 2026-10-02):** adapter silkscreen `USB-test-MD50-V5.1` / `202511`. USB-C (U1) wired straight to DP/DN, no USB-UART bridge chip visible → **native USB**, expect `/dev/cu.usbmodem*`, no driver. Two 10-pin 0.5 mm FPC sockets (J1 bottom, J3 right; silkscreen "TopOverlay is 2.1"), 4 corner mounting holes, 10-pin 2.54 mm header J2:

| J2 pin | Adapter silkscreen | Spec p.5 table | Spec p.9 drawing (rev A, 2026-03-16) |
|---|---|---|---|
| 1 | 5V | 5V ✓ | 5V |
| 2 | IO4 | GPIO38 ✗ | **GPIO18** ✗ |
| 3 | GND | GND ✓ | GND |
| 4 | IO8 | RX2/GPIO40 ✗ | GPIO40 |
| 5 | NC/IO18 | TX2/GPIO39 ✗ | GPIO39 |
| 6 | RX | RX (UART0) ✓ |
| 7 | TX | TX (UART0) ✓ |
| 8 | RST | CHIP-EN ✓ |
| 9 | DP | USB D+ ✓ |
| 10 | DN | USB D− ✓ |

Pin 2 has three different labels across three vendor sources (GPIO38 / GPIO18 / IO4). Pins 2/4/5 disagree with the spec (and on the knob, GPIO4 = touch INT, GPIO8 = LCD RST). The adapter is likely shared across MD50 variants. **Don't use J2 pins 2/4/5 as GPIOs until verified** (continuity test to the FPC or a GPIO toggle test). USB, UART0, EN and 5V/GND are safe.

---

## 3. Toolchain setup

Pick one. The **Arduino path** is the easiest way to get a first light. Use **ESP-IDF + BSP** for a serious project.

### A. Arduino IDE 2.x (vendor-supported path)
1. Add the boards URL `https://espressif.github.io/arduino-esp32/package_esp32_index.json`, then install **esp32 by Espressif ≥ 3.0.0**. The vendor PlatformIO example pins 3.1.1.
2. In the Library Manager, install:
   - `ESP32_Display_Panel` (Espressif) **≥ 1.0.3**. Click "Install all" to pull in `ESP32_IO_Expander` and `esp-lib-utils`.
   - `lvgl` **8.4.0**. Not v9: the vendor port is v8 only.
   - `ESP32_Knob`, `ESP32_Button`, `ui`: copy them from `vendor/UEDX46460015-MD50ET/Libraries/` into `~/Documents/Arduino/libraries/`.
3. Board settings (Tools menu):
   - Board **ESP32S3 Dev Module**
   - Flash Size **16MB**. If flash_id reports 8 MB, use 8MB.
   - Partition **16M Flash (3MB APP/9.9MB FATFS)**
   - PSRAM **OPI PSRAM** (required)
   - Flash Mode QIO 80 MHz, CPU 240 MHz
   - USB CDC On Boot **Enabled** (Serial over native USB). Set it to Disabled if you want logs on the FPC UART0 pins.
4. Do **not** accept the IDE's prompts to update libraries. The vendor FAQ warns that newer versions break the port.

### B. PlatformIO
- Vendor example: `vendor/UEDX46460015-MD50ET/examples/PlatformIO/encoder15/`. It uses a custom board `boards/ESP-LCD.json` (qio_opi, 16 MB, `default_16MB.csv`), pins arduino-esp32 **3.1.1**, and uses `lvgl#release/v8.4` with `-DARDUINO_USB_CDC_ON_BOOT=1`.
- `pio run -t upload -t monitor`.

### C. ESP-IDF (recommended for real work)
- Use **IDF ≥ 5.5**. The vendor tested v5.5.4. On macOS: install with EIM or `git clone -b v5.5.4 --recursive https://github.com/espressif/esp-idf && ./install.sh esp32s3 && . ./export.sh`.
- Vendor example: `vendor/UEDX46460015-MD50ET/examples/ESP-IDF/UEDX46460015-MD50E-IDF/`. Its `idf_component.yml` pulls `viewesmart/bsp_knob_15_md50et ^1.0.2` (bump to `^1.0.3`, latest 2026-08-12) and `lvgl/lvgl ^8.3.11`. The BSP also accepts LVGL 9.
- Key sdkconfig: `CONFIG_LV_COLOR_16_SWAP=y` (LVGL 8) or `LV_DRAW_SW_SUPPORT_RGB565_SWAPPED` (LVGL 9), plus a custom `partitions.csv` with a 3 MB factory app.
- LVGL 9.3 alternative: https://github.com/lvgl/lv_port_viewe_knob_15_espidf. It is the official LVGL port, with benchmarks around 25 fps on average.

---

## 4. First flash

Connect the knob's FPC to the adapter (contacts facing correctly, latch closed), then connect USB. The port shows up as `/dev/cu.usbmodem*` for native USB or `/dev/cu.wchusbserial*` for CH340.

### Hello world (checks that flashing works, no display)
```bash
ls /dev/cu.*
python -m esptool --chip esp32s3 -p /dev/cu.usbmodemXXXX flash_id   # confirm flash size + PSRAM chip
```
In Arduino, flash `Serial.begin(115200); Serial.println("hi");` with USB CDC On Boot enabled. If the upload fails, hold the **knob button (BOOT/GPIO0)** while you plug in USB, then retry.

### LVGL demo, Arduino
1. Open `vendor/UEDX46460015-MD50ET/examples/Arduino/simple_port/simple_port.ino`. The upstream copy is at File → Examples → ESP32_Display_Panel → Arduino → gui → lvgl_v8 → simple_port.
2. In `esp_panel_board_supported_conf.h`, set `ESP_PANEL_BOARD_DEFAULT_USE_SUPPORTED 1` and uncomment `#define BOARD_VIEWE_UEDX46460015_MD50ET`. Enable only that one board.
3. In `lv_conf.h`, set `LV_COLOR_16_SWAP 1`. Leave `LVGL_PORT_AVOID_TEARING_MODE` and `LVGL_PORT_ROTATION_DEGREE` alone.
4. Upload. The example registers the knob on GPIO6/5 and the button on GPIO0, and loads a SquareLine UI. To run the LVGL benchmark instead, uncomment `#include <demos/lv_demos.h>` and call `lv_demo_benchmark()`.
5. If the screen stays black, make sure GPIO17 is driven HIGH before `panel->begin()`. The PlatformIO example does this with `pinMode(17,OUTPUT); digitalWrite(17,HIGH);`.

### LVGL demo, ESP-IDF
```bash
cd vendor/UEDX46460015-MD50ET/examples/ESP-IDF/UEDX46460015-MD50E-IDF
idf.py set-target esp32s3
idf.py build flash monitor -p /dev/cu.usbmodemXXXX
```

### Prebuilt binaries
Use the Espressif Flash Download Tool (Windows only; the vendor copy was removed from `vendor/` to save 15 MB) or `python -m esptool write_flash`.

---

## 5. ESPHome and alternative firmwares

**ESPHome works.** Two community configs are saved in `vendor/esphome/`:
- `STB3-viewe-uedx46460015-example.yaml` ([STB3/esphome-Viewe_UEDX4646](https://github.com/STB3/esphome-Viewe_UEDX4646)). Uses the modern `mipi_spi` with `model: CO5300`, `psram: octal`, a GPIO17 HIGH in `on_boot` (priority 600), `cst816` touch on I²C 1/3, a `rotary_encoder` on 5/6, and the button on GPIO0.
- `vodidan-UEDX46460015-MD50E.yaml` ([vodidan/UEDX46460015-MD50E-ESPHome-config](https://github.com/vodidan/UEDX46460015-MD50E-ESPHome-config), mirrored on VIEWESMART Gitee). Uses the older `qspi_dbi` CUSTOM with a full init sequence, `width: 472` plus `offset_width: 6`, `enable_pin: 17`, an LVGL UI, and touch with `interrupt_pin: 4` and `reset_pin: 2`.

Minimum skeleton:
```yaml
esp32: { board: esp32-s3-devkitc-1, framework: { type: esp-idf } }
psram: { mode: octal, speed: 80MHz }
esphome:
  on_boot: { priority: 600, then: [ lambda: 'gpio_set_direction(GPIO_NUM_17, GPIO_MODE_OUTPUT); gpio_set_level(GPIO_NUM_17, 1);', delay: 100ms ] }
spi: { id: qspi, type: quad, clk_pin: GPIO10, data_pins: [GPIO13, GPIO11, GPIO14, GPIO9] }
i2c: { sda: GPIO1, scl: GPIO3 }
display: [{ platform: mipi_spi, model: CO5300, cs_pin: GPIO12, reset_pin: GPIO8, bus_mode: quad, dimensions: { width: 466, height: 466 }, color_depth: 16 }]
touchscreen: [{ platform: cst816, interrupt_pin: GPIO4, reset_pin: GPIO2 }]
sensor: [{ platform: rotary_encoder, pin_a: GPIO6, pin_b: GPIO5 }]   # swap A/B to flip direction
binary_sensor: [{ platform: gpio, pin: { number: GPIO0, inverted: true }, name: knob_press }]
```
Green edge line with `mipi_spi` CO5300: issue [esphome#15765](https://github.com/esphome/esphome/issues/15765) was **closed on 2026-04-18 (state "completed") after the reporter confirmed `offset_width: 6` removed the line**; no bug label. That reporter had a Waveshare 1.75" board. The ESPHome maintainer says `dimensions.offset_width: 6` does the same job as `esp_lcd_panel_set_gap`. The reporter still saw the line after adding a `rotation`. So: set `offset_width: 6` (STB3's YAML does) and test any rotation or transform before you settle on it. The issue's own suggestion of a missing set_gap call was wrong. ESPHome has **no board preset for this VIEWE model** (2026.9.1); use the generic `model: CO5300`, whose default size is 480×480, so set the dimensions explicitly. Forum thread: https://community.home-assistant.io/t/touch-knob-display-uedx46460015-md50esp32-1-5inch-example/982434

Other firmwares and projects for this exact board:
- [neproger/1.5inch_lvgl](https://github.com/neproger/1.5inch_lvgl): ESP-IDF + LVGL Home Assistant knob. Builds its room and device UI from HA areas, has MQTT toggles and a clock/weather screensaver.
- [oblaka3d/knobos](https://github.com/oblaka3d/knobos): ESP-IDF firmware for the VIEWE C3 1.28" and S3 1.5" knobs. Wi-Fi onboarding by QR code, web UI.
- [giobauermeister/viewe_lvgl_knob_ui](https://github.com/giobauermeister/viewe_lvgl_knob_ui): LVGL Pro editor demo of an air-conditioner controller.
- [MiyakoYakota/lvgl-viewe-display](https://github.com/MiyakoYakota/lvgl-viewe-display) and [eugenjkee/...-DalyBMS](https://github.com/eugenjkee/BOARD_VIEWE_UEDX46460015_MD50ET-DalyBMS): single-purpose apps.
- Arduino_GFX (moononournation) has an `Arduino_CO5300` class, which should work with the pins above. Not tested on this board **(unverified)**.
- OpenHASP and Tasmota: no known config for CO5300/this board. Treat them as unsupported.

---

## 6. Enclosures

The module already sits in its own round metal/plastic shell, so what you need is a **stand, mount, or base that also hides the FPC and adapter**.

- **No exact-fit printable was found** on Printables (API search for viewe, knob display, esp32 knob, 466x466, UEDX…), MakerWorld, Thingiverse or Cults3D as of 2026-10-02.
- Closest candidates to remix (all for different knob displays, so scale or redo the pocket to Ø51.7 mm):
  - Printables 1750358, stand for Elecrow CrowPanel 2.1" round knob: https://www.printables.com/model/1750358-stand-for-elecrow-crowpanel-21inch-hmi-esp32-rotar
  - Printables 1856277, Waveshare 1.8" knob desktop/mobile mounts: https://www.printables.com/model/1856277-waveshare-esp32-s3-knob-touch-lcd-18-mounts
  - Printables 1826861, Waveshare 1.8" knob holder: https://www.printables.com/model/1826861-waveshare-esp32-s3-18inch-knob-holder
  - Printables 1355637, Waveshare 1.8" AMOLED USB-C base: https://www.printables.com/model/1355637-waveshare-esp32-s3-18inch-amoled-touch-display-3d
  - MakerWorld 452986, "enclosure for 1.5" OLED display", is for a LOLIN C3 with a flat 1.5" OLED. **Not a fit.**
- **Design your own.** The vendor provides a 2D mechanical drawing: `vendor/UEDX46460015-MD50ET/2D drawings/UEDX46460015-MD50E.dwg` and spec PDF p.9. Key dimensions: Ø51.7 ±0.5 outer, 25.15 ±0.3 mm total depth, Ø38.21 display area, FPC 70 ±5 mm long, 0.5 mm pitch, 10 pins. **No STEP file is published.** Ask VIEWE support for one, or model a simple cylinder from the DWG. The back-view mounting features (screw bosses) are not dimensioned in the PDF, so open the DWG in FreeCAD/LibreCAD to check.
- Practical design: a desk puck about 60 mm in diameter with a Ø52 mm × ~20 mm pocket, a slot for the FPC, and a cavity underneath for the adapter board and USB cable.

---

## 7. Gotchas

1. **GPIO17 must be HIGH** before LCD init. It is the panel supply enable. Without it the screen stays black.
2. The **vendor README touch pin table is wrong** (see §2).
3. **Width is 472, not 466**, in the driver and LVGL buffer. Do not use `esp_lcd_panel_set_gap()` with the vendor BSP. Getting this wrong leaves a blank column on the left or a green line on the right. Exception: cinder's 180-degree rotation (MX/MY) sets a gap, (2, 14) because the mirror spans 480 columns and rows (checked on the knob, 0.9.44), to keep the visible columns 6..471 and rows 0..465 written (`docs/features.md`, "Display rotation").
4. Brightness on AMOLED is a panel command (0x51), not PWM. Use `bsp_knob_15_md50et_set_brightness()`. The vendor warns against raw-writing 0x51 over QSPI.
5. **PSRAM must be OPI/octal.** QSPI PSRAM settings crash or bootloop the board.
6. Use **LVGL 8.4** with the Arduino/PlatformIO vendor port and **LVGL 9.3** with the LVGL-official IDF port. Do not mix them.
7. GPIO0 is shared by the knob press and BOOT. Holding the knob down during reset puts the board in download mode.
8. The flash size claim (16 MB) conflicts with the Z25Q64 on the schematic. Run `python -m esptool flash_id` before choosing a partition table.
9. No USB socket on the knob itself. Losing or damaging the adapter or FPC means making a 0.5 mm 10-pin breakout.
10. The vendor spec still notes "Bluetooth and WIFI functions are still under test" (SoC Wi-Fi works fine in community firmware). Burn-in is possible on AMOLED, so use dimming or a screensaver for static UIs.
11. **No 90/270 in hardware.** CO5300 MADCTL (36h) has only MY and MX; the MV (row/column exchange) bit is "don't care" in the datasheet, so `esp_lcd_panel_swap_xy()` does nothing on this panel.

---

## Latest firmware & sources (checked 2026-10-02)

The Chrome extension was not connected, so the AliExpress listing (kit contents, adapter USB type) was not checked online. The adapter board was identified from a photo instead; see "User's kit" under the pinout (native USB-C, `USB-test-MD50-V5.1`).

### Vendor (VIEWE / 优奕视界)
| Source | Latest state |
|---|---|
| GitHub [VIEWESMART/UEDX46460015-MD50ESP32-1.5inch-Touch-Knob-Display](https://github.com/VIEWESMART/UEDX46460015-MD50ESP32-1.5inch-Touch-Knob-Display) | `main` only. **No tags or releases.** Last commit **2026-08-12** ("Update the IDF example, switch to BSP driver"; old examples removed). Gitee mirror [VIEWESMART/…](https://gitee.com/VIEWESMART/UEDX46460015-MD50ESP32-1.5inch-Touch-Knob-Display) is in sync (same 2026-08-12 commits). `gitee.com/viewsmart` returns 404; the real org is `VIEWESMART`. |
| Spec sheet | **V2.0, dated 2026-04-01** (in `vendor/`). V1.0 2024-10-10 → V1.1 2024-10-16 (repo links) → V1.2 2025-07-10 (adapter↔ESP32 pin map) → V2.0 (new mechanical drawing and images). Same copy on [VIEWESMART/wiki](https://github.com/VIEWESMART/wiki/blob/main/docs/assets/datasheet/UEDX46460015-MD50ET.pdf) (updated 2026-05-15). |
| Hardware revisions | Schematic is still `SCH.V2.0`. Across spec V1.1→V2.0 there is **no change in pinout, touch IC (CST820), driver (CO5300AF-42) or stated flash (16 MB)**. The Z25Q64 (8 MB) on the schematic is still unexplained, so read flash_id. |
| ESP-IDF BSP | [`viewesmart/bsp_knob_15_md50et`](https://components.espressif.com/components/viewesmart/bsp_knob_15_md50et) **1.0.3 (2026-08-12)**. Versions 1.0.0 to 1.0.3 were all published 2026-08-11/12. Uses `esp_lcd_co5300 ^2.1.0`, `esp_lvgl_adapter ^0.6.3`, `viewesmart/esp_lcd_touch_cst820 ^1.0.3` (2026-08-11), `button ^4`, `knob ^1`. LVGL 8 or 9. Requires IDF ≥ 5.5. Source: [VIEWESMART/Viewe-esp32-components](https://github.com/VIEWESMART/Viewe-esp32-components/tree/main/bsp/bsp_knob_15_md50et). Use `^1.0.3` rather than the example's `^1.0.2`. |
| LVGL 9 port | [lvgl/lv_port_viewe_knob_15_espidf](https://github.com/lvgl/lv_port_viewe_knob_15_espidf): LVGL 9.3.0, last push **2025-06-27** (stale). For LVGL 9.x today, prefer the BSP with `lvgl/lvgl ^9`. |
| Chinese site | [chinasunyee.com](https://www.chinasunyee.com/) is VIEWE's CN site. Its "1.5寸圆形智能旋钮屏（WIFI版）" page ([link](https://www.chinasunyee.com/Products/15cunyuanxingzhineng.html)) lists **400×400 TFT IPS**. That is a **different 1.5" knob variant** from this 466×466 AMOLED board. The page has no downloads. |
| Spotpear (CN reseller) | [spotpear.cn wiki](https://spotpear.cn/wiki/ESP32-S3-1.5-inch-Round-Rotary-OLED-TouchScreen-Knob-Display-466x466.html) links only the V1.1 spec PDF and the GitHub repo. No firmware. |

### Firmware binaries
- **No factory or demo `.bin` is published anywhere I checked**: the vendor repo and its full history (no `firmware/` path ever committed, although the README mentions one), the Gitee mirror (no releases), the VIEWESMART wiki, the Spotpear CN/EN wikis, and viewedisplay.com. `vendor/firmware/` was therefore not created.
- **Back up the stock firmware before your first reflash.** It is the only copy of the factory demo.
  ```bash
  # use esptool from an existing IDF/PlatformIO env; nothing new to install
  python -m esptool --chip esp32s3 -p /dev/cu.usbmodemXXXX flash_id          # note the size
  python -m esptool --chip esp32s3 -p /dev/cu.usbmodemXXXX -b 921600 read_flash 0 ALL stock_full.bin   # 'ALL' = detected size (esptool ≥4.6), else 0x1000000 or 0x800000
  # restore:
  python -m esptool --chip esp32s3 -p /dev/cu.usbmodemXXXX -b 921600 write_flash 0x0 stock_full.bin
  ```
- Flash Download Tool (Windows GUI): the vendor zip was **3.9.3** (dropped from `vendor/`). The current Espressif release is **3.9.11** at https://dl.espressif.com/public/flash_download_tool.zip ([docs](https://docs.espressif.com/projects/esp-test-tools/en/latest/esp32s3/production_stage/tools/flash_download_tool.html)). On a Mac use esptool (`python -m esptool`) instead.

### Upstream dependency versions
| Dep | Latest | Note |
|---|---|---|
| ESP-IDF | **v6.1** (2026-08-27), v6.0.3 (2026-09-02), v5.5.5 (2026-07-17) | vendor verified on 5.5.4. **Stay on 5.5.x**; IDF 6.x with this BSP is untested **(unverified)**. |
| arduino-esp32 core | **3.3.12** (2026-09-18). 4.0.0-RC1 (2026-09-23) is a prerelease. | vendor PIO pins 3.1.1. Avoid 4.0 RC. |
| ESP32_Display_Panel | **v1.0.4** release (2025-09-23). CHANGELOG lists v1.0.5 (2025-10-14) with no GitHub release. | CST820 and this VIEWE board were added in **v1.0.3**, so ≥1.0.3 is required. The board header is upstream as `BOARD_VIEWE_UEDX46460015_MD50ET`. |
| LVGL | 9.6.0 (2026-09-16) on the IDF registry | vendor Arduino port: **8.4.0 only**. |
| ESPHome | **2026.9.1** (2026-09-29) | generic `mipi_spi` `CO5300` model. See §5 for the closed green-line issue. |

### Chinese community and AI firmware
- **xiaozhi-esp32 (小智 AI)**: upstream [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) v2.5.0 (2026-09-10) has **no board config for this display**. VIEWE's own fork [VIEWESMART/xiaozhi-esp32](https://github.com/VIEWESMART/xiaozhi-esp32) (last push 2025-12-27) has none either. Porting is feasible, since xiaozhi already has CO5300 drivers (m5stack stopwatch, waveshare amoled boards) and a common `knob.cc`. But **this board has no mic or speaker**, so the voice assistant would need external I²S hardware on the FPC GPIOs (38/39/40), which are too few for full duplex. Not practical.
- CSDN, bilibili, 知乎, oshwhub (立创开源) and esp32.com 中文: web searches for "UEDX46460015", "1.5寸 旋钮屏 ESP32-S3 466", "CO5300 466 旋钮" and "旋钮屏 外壳" returned **no project, firmware or enclosure for this exact board**. Results were Waveshare 1.43/1.75/1.32" AMOLED boards, other knob designs (e.g. oshwhub "彩屏旋钮-ESP32S3-HUB-V1", a different PCB) and VIEWE resellers. The oshwhub search API needs a login (401), so oshwhub coverage is limited to the search engine.
- Other GitHub users of this board: [21cncstudio/project_aura](https://github.com/21cncstudio/project_aura) (ESP32-S3 air-quality station, LVGL/MQTT/HA; vendors the VIEWE board header; updated 2026-09-19), plus the projects listed in §5.

---

## 8. Local files (`vendor/`)

Not in this repository (copyrighted vendor material); listed so the citations above can be traced.

- `UEDX46460015-MD50ET V2.0 SPEC.pdf`: spec (pinouts, power, mechanical drawing p.9)
- `UEDX46460015-MD50ET SCH.V2.0_00.png`: schematic
- `UEDX46460015-MD50ET/`: shallow copy of the VIEWESMART GitHub repo at commit `15fbde3e` (2026-08-12). Removed: `.git`, `Libraries/lvgl-release-v8.4` (install LVGL 8.4 from the Library Manager instead) and the Flash Download Tool zip. About 100 MB.
  - `datasheet/`: CO5300, CST820 (EN + CN), panel UE015WV-RB24-A021A, encoder, switch
  - `examples/{Arduino,PlatformIO,ESP-IDF}`
  - `Libraries/`: the vendor-pinned ESP32_Display_Panel, ESP32_Knob, ESP32_Button, IO_Expander, esp-lib-utils, ui
  - `2D drawings/UEDX46460015-MD50E.dwg`
- `esphome/`: two community ESPHome YAMLs

## 9. Links

- Vendor repo: https://github.com/VIEWESMART/UEDX46460015-MD50ESP32-1.5inch-Touch-Knob-Display
- Gitee mirror: https://gitee.com/VIEWESMART/UEDX46460015-MD50ESP32-1.5inch-Touch-Knob-Display
- Product: https://viewedisplay.com/product/esp32-1-5-inch-466x466-round-amoled-knob-display-touch-screen-arduino-lvgl/
- VIEWE beginner Arduino tutorial: https://github.com/VIEWESMART/VIEWE-Tutorial/blob/main/Arduino%20Tutorial/Arduino%20Getting%20Started%20Tutorial.md
- IDF BSP: https://components.espressif.com/components/viewesmart/bsp_knob_15_md50et
- LVGL 9 port: https://github.com/lvgl/lv_port_viewe_knob_15_espidf
- ESP32_Display_Panel: https://github.com/esp-arduino-libs/ESP32_Display_Panel
- Spotpear wiki: https://spotpear.com/wiki/ESP32-S3-1.5-inch-Round-Rotary-OLED-TouchScreen-Knob-Display-466x466.html (older spec V1.1 PDF: https://cdn.static.spotpear.com/uploads/picture/learn/ESP32/ESP32-S3-1.5inch-push-knob/UEDX46460015-MD50E-V1.1-SPEC.pdf)
- ESPHome mipi_spi: https://esphome.io/components/display/mipi_spi/
- ESPHome CO5300 green-line issue: https://github.com/esphome/esphome/issues/15765
- HA forum CO5300 thread: https://community.home-assistant.io/t/esphome-mipi-spi-display-configuration-for-co5300/970359
