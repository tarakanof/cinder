# J2 hardware test image

`firmware/hwtest/` is a separate ESP-IDF project for bringing up the J2 add-ons ([`hardware-expansion.md`](hardware-expansion.md)): the pin probe, the I2C sensors, the radar and the speaker. It shares the BSP and the new components (`sens`, `tone`, `j2`) with the app, and nothing else.

- **No Wi-Fi, no NVS, no Ember.** The image never calls `nvs_flash_init`, so pairing, Wi-Fi and settings stay as they are. While it runs the knob does not check in: Ember shows it offline.
- **RAM only.** Pins set over USB last until a reboot. Defaults come from the `CINDER_J2_*` Kconfig options (`components/j2/Kconfig`): SDA GPIO44, SCL GPIO43, speaker pins unset.
- **Never published.** It builds into `build-hwtest/` with its own `sdkconfig` and project name `cinder_hwtest`; `publish.sh` and `release.sh` never see it. CI builds it on every PR so it keeps compiling.
- Same partition table as the app; `app-flash` writes `ota_0` only. Core dumps are off in this image, so a crash here leaves nothing for the app to upload.

## Build

```sh
. ~/.espressif/tools/activate_idf_v5.5.5.sh
cd firmware
idf.py -C hwtest -B build-hwtest build
```

## Flash and restore (maintainer knob)

Flash only with the user's go-ahead, under the flash lock ([`workflow.md`](workflow.md), Flash). Note the firmware version the knob runs first (Ember `GET /v1/devices`, `fw`), so you can put the same release back.

```sh
idf.py -C hwtest -B build-hwtest -p <port> app-flash erase-otadata
```

Restore the release the knob ran (exact CI bytes, not a local build), then confirm a live checkin:

```sh
gh release download v<X.Y.Z> -R tarakanof/cinder -p cinder.bin -D /tmp/cinder-v<X.Y.Z>
python -m esptool --chip esp32s3 -p <port> write_flash 0x20000 /tmp/cinder-v<X.Y.Z>/cinder.bin
python -m esptool --chip esp32s3 -p <port> erase_region 0xf000 0x2000
```

Never run `idf.py -C hwtest flash`: it would rewrite the bootloader and partition table too (same bytes today, but that step needs the user's approval). `erase_region` on otadata is the same step as `erase-otadata`: the bootloader boots `ota_0`, NVS and `ota_1` stay. A dev build instead: `idf.py -p <port> app-flash erase-otadata` from `firmware/`.

## On the knob

The screen shows one of two views:

- **ROOM** (at boot): the chips found on the sensor bus and their latest readings: °C and %RH (AHT21), eCO2, TVOC and AQI with the ENS160 warm-up state, lux (BH1750), PRESENT or away (radar OUT through the PCF8574), hPa (BMP280, if one is on the bus), the bus pins and an error count.
- **GPIO N HIGH** (pin probe): **push** steps through GPIO43, 44, 38, 39, 40 and 18, holding one high at a time; **turn** goes back and forth; **hold** (or a push after the last one) returns to ROOM. GPIO4 and GPIO8, the other J2 candidates in the vendor sources, are the touch INT and LCD reset: the probe never drives them. It also skips the pin set as `radar_rx`, which the radar's TX drives.
- Logs and `HWT1` replies share the port; log lines are written whole, so they never split a reply.

## Host tool

`firmware/tools/hwtest.py` speaks the image's `HWT1 {json}` line protocol on the USB port (`--port`, default `/dev/cu.usbmodem1101`), opened without touching DTR/RTS.

| Command | Does |
|---|---|
| `info` | Version, free RAM, pins, the room snapshot |
| `probe` | Guided pin map: drives each GPIO high, asks which J2 pin reads ~3.3 V, prints the map and speaker pins to try |
| `drive <gpio> [--low]` / `stop` | Hold one candidate GPIO high or low / release it |
| `levels` | Reads each candidate with the internal pull-down: 1 = a module pulls it up (wired SDA/SCL) |
| `scan` | I2C addresses on the bus, with the chip each one probably is |
| `room [start\|stop]` | The room snapshot; start or stop the sensor task |
| `watch -s N [--json]` | One snapshot per second for N s |
| `pins --sda --scl --bclk --lrc --din --radar_rx --radar_bit --pcf` | Set pins for this boot (only probe GPIOs or -1, I2C, I2S and radar pins distinct); restarts the sensor task |
| `tone --hz --ms --vol` | Sine on the MAX98357A at 16 kHz; `vol` is % of half scale (default 20). The screen stays on ROOM, and the reply counts presence flips during the tone |
| `radar --ms` | LD2410 UART reports for ms (needs `--radar_rx`) |
| `raw '<json>'` | Any request |

## Bring-up order

1. **J2 header and pin map.** Unplug USB and the FPC, then solder the 10-pin header; nothing else connected. Flash, run `hwtest.py levels` once (every candidate should read 0 with nothing wired), then `hwtest.py probe` (or push through the steps with a multimeter on J2 pins 2, 4, 5, 6 and 7 against pin 3). GPIO43 should land on pin 7 and GPIO44 on pin 6; if not, pin 1 is not where you think. Record the map in `board.md` and `llm.md` (it closes the J2 open item).
2. **Modules, one at a time, off the knob first.** Power each from J2 pin 1 (5V) and pin 3 (GND) with its signal pins unconnected, and measure SDA, SCL, OUT and 3V3 to GND: about 3.3 V is fine, about 5 V means don't connect ([`hardware-expansion.md`](hardware-expansion.md), Voltage rules). The PCF8574 is the exception: its pull-ups follow its VCC, so it reads 5 V on 5V by design. Measure it powered from the ENS160 board's 3V3 pin instead (expect 3.3 V), and read its chip marking (PCF8574 or PCF8574A). Keep its address jumpers at 0x20; 0x23 is the BH1750's address.
   The LD2410 pin order depends on the variant (LD2410C: TX, RX, OUT, GND, VCC; plain LD2410: OUT, TX, RX, GND, VCC): wire by the silkscreen, never by a picture. Reversed VCC and GND kills the module.
3. **Sensor bus.** ENS160+AHT21 and BH1750 on 5V; PCF8574 on the ENS160 board's 3V3; SDA to J2 pin 6 (GPIO44), SCL to pin 7 (GPIO43). `levels` should show 43 and 44 at 1, `scan` should list 0x23, 0x38, 0x53 (or 0x52) and 0x20. The ROOM view fills in; the ENS160 says warm-up for about 3 min (start-up for its first hour ever).
4. **Radar.** VCC 5V, GND, OUT to PCF8574 P0 (another pin: `pins --radar_bit N`). Walk away and back: away/PRESENT. Optional: radar TX to a free probed GPIO, `pins --radar_rx N`, `radar` for distance and energy.
5. **Speaker.** Amp VIN 5V and GND (470 µF across them, the capacitor's striped leg to GND), BCLK/LRC/DIN to the three probed GPIOs that are left, `pins --bclk A --lrc B --din C`, then `tone --vol 10` and up. Listen for clicks at start and end; if a USB power meter is at hand, note the peak current at each volume. With nobody in front of the radar (aim it away from you and the laptop), play `tone --ms 10000 --vol 30`: the screen keeps showing presence, and the reply's `presence_flips` counts any false PRESENT.
6. **Record** what you measured (pin map, addresses, warm-up, lux in the room with the panel on and off, current per volume, radar false triggers) in `hardware-expansion.md` and `features.md`, then restore the app.
