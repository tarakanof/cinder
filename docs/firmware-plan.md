# Knob firmware plan (Ember + BLE media remote)

Status: plan from 2026-10-02; the BLE media remote and Home Assistant pages are not started. What is built: `features.md`. Board: VIEWE UEDX46460015-MD50ET (see `board.md`). Stock firmware is backed up in `../backup/` (local only, git-ignored).

Issue numbers written as #N or cinder#N refer to the original private repository; Ember#N refers to the public [Ember](https://github.com/tarakanof/Ember) repository.

## Stack

- ESP-IDF 5.5.5 + BSP `viewesmart/bsp_knob_15_md50et` 1.0.3 (vendored in `firmware/components/bsp_knob/`) (display, touch, knob, button, brightness) + LVGL 9. Flash 16 MB, OPI PSRAM 8 MB.
- Wi-Fi (2.4 GHz) for Ember and, later, Home Assistant. BLE (NimBLE) for the media remote. Both share one radio with ESP-IDF software coexistence (`CONFIG_ESP_COEX_SW_COEXIST_ENABLE`).
- Partition table: two OTA app slots (so later updates can go over Wi-Fi) + NVS for Wi-Fi credentials, the device token, Ember URL and BLE bonds. OTA over both slots since 0.9.16 (#10).
- Logic (Ember JSON parsing, bot behaviour, input state machine) lives in plain C components with no `esp_*` calls, unit-tested on the Mac (host build). UI first in the LVGL SDL simulator at 466×466.

## Why not a drop-in TC001 replacement

Ember drives the TC001 by pushing 32×8 pixel frames to stock awtrix-ng firmware over its HTTP API. The knob has no awtrix-ng port and a 466×466 round AMOLED, so those frames don't map. The knob is a **pull client** instead, and both displays run side by side.

## Pages

| Page | Content | Source |
|---|---|---|
| **Bot** (home) | Ember bot face; mood from the winning session state; in waiting/error the host label (e.g. `M4`) under the face | `GET /state` (`render.source`, else the winning session), port of Ember's bot |
| **Pomodoro** | arc around the bezel + time left | `GET /v1/pomodoro/state` |
| **Media** | volume arc, play/pause icon, BLE link state | BLE HID (below) |
| **Usage** | 5 h / weekly usage gauges | `GET /v1/usage` |
| **Idle** | dim black clock + weather, pixel shift | `GET /v1/weather/state`, `/v1/meetings/state` |
| Debug | Wi-Fi, BLE, heap, Ember health | `GET /v1/clock/health` |

## Input model

One rule for every page: **push and turn** (hold the knob down while turning) changes the page. A plain turn and a plain push act on the current page. This keeps "turn" free for volume on the Media page.

| Input | Bot / Pomodoro page | Media page |
|---|---|---|
| Turn | eyes follow the ring | volume up / down (one step per detent) |
| Push | Pomodoro start / pause / resume | play / pause |
| Double push | — | next track |
| Long push | Pomodoro stop | previous track |
| Push and turn | change page | change page |
| Swipe up / down (touch, `swipe_pages`) | next / previous page | next / previous page |
| Hold 10 s, then turn right one full turn (still holding) | factory reset | factory reset |
| Touch (tap, decided on release) | wake / hop | mute toggle |

Detent count per turn and double-push timing need tuning on hardware.

The Wi-Fi **now-playing** page (Ember#280) uses the Media column over Ember instead of BLE: turn = volume (Plex / Apple Music's own level, 2 points per detent), push = play/pause, double push = next, long push = previous; touch only wakes the face. See `features.md` "Now-playing controls".

## Ember (Wi-Fi)

Read, no auth: `GET /state`, `/v1/pomodoro/state`, `/v1/usage`, `/v1/weather/state`, `/v1/meetings/state`, `/v1/clock/health`, `/v1/display/brightness` (every 60 s; panel fades to `level` 0-255, floor 10; failures keep the level).
Write, bearer token from NVS (`dev_tok`: the per-device token from Ember's registry after USB pairing; no build carries a token since 0.9.16): `POST /v1/pomodoro/{start,pause,resume,stop,skip}`, and with a device token `POST /v1/devices/self/checkin`.

## Provisioning (phases #6-#10)

1. Done (#6): settings in NVS namespace `cinder`, dev seed from Kconfig, setup face when no SSID, factory reset (hold 10 s, then turn right one full turn while holding).
2. Done (Ember#221): Ember server device registry `/v1/devices`. 3. Done (#7): USB provisioning over the console port, Improv Serial (Wi-Fi, scan, info; standard Improv, so ESP Web Tools should work too: untested) + `CINDER1` JSON lines (Ember URL, device id, token; status; reset; reboot). 4. Done (#8): device token, checkin every 60 s and on the `/state` epoch, settings applied live (brightness, pages, poll, bot timings), token rotation, 401 → "Not paired". Plain NVS, no NVS encryption, no eFuse burns; one knob.

## BLE media remote

The ESP32-S3 has BLE only (no Bluetooth Classic), so the knob **controls** playback; it cannot stream audio. It acts as a BLE HID device (HID over GATT), like a Bluetooth keyboard's media keys, so it works with the Mac, iPhone and iPad without an app and without Wi-Fi.

- **Stack:** NimBLE (smaller than Bluedroid). Start from the ESP-IDF example `examples/bluetooth/esp_hid_device` (BLE HID, supports ESP32-S3).
- **Report:** HID Consumer Control (usage page 0x0C): Volume Increment 0xE9, Volume Decrement 0xEA, Play/Pause 0xCD, Scan Next 0xB5, Scan Previous 0xB6, Mute 0xE2. Send a press then a release report.
- **Pairing:** advertise as "Ember Knob". Bond with LE Secure Connections ("Just Works", no display passkey needed) and store bonds in NVS so it reconnects automatically. Long press on the Media page while unpaired starts pairing; a Debug-page action clears bonds.
- **Volume display:** HID is one-way, so the knob cannot read the Mac's volume level. The Media page shows step feedback (an arc that moves with each detent), not the real level. The real level and track title can come later from Home Assistant over Wi-Fi.
- **Power and radio:** advertising and the connection stay at low duty; Wi-Fi polling of Ember continues alongside.
- To verify on hardware: macOS and iOS accept consumer-control reports from a BLE HID device (expected; standard keyboards do this), detent-to-volume feel, and coexistence latency while Wi-Fi polls.

## Build order

1. BSP bring-up: display, touch, knob, button; LVGL 9 "hello" on the knob.
2. Bot page, a port of Ember's bot (simulator first, then hardware).
3. Wi-Fi + Ember polling: bot mood and the Pomodoro page.
4. BLE media remote: pairing, consumer-control reports, Media page.
5. Idle face and AMOLED rules (dim, pixel shift, panel off).
6. OTA updates, then Home Assistant pages (media metadata, lights, Unraid).

## Open questions

- Polling interval vs. adding an SSE/long-poll endpoint to Ember (instant "waiting" alerts).
- Run both displays, or add a "headless" mode so Ember stops publishing to the TC001.
- Enclosure must also house the adapter board (USB-C on `USB-test-MD50-V5.1`).
