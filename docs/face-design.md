# Weather and music faces: design and device plan

Status: design only (2026-10-08), nothing built on the device. The faces live on the "Cinder Weather" design canvas (claude.ai artifact, rows "Bot page: weather moments", "Weather faces" and "Music faces"; press Play to see the motion) and are mirrored in [`design/faces/`](../design/faces/README.md). This file records the design system and how to build it within the knob's measured limits ([`features.md`](features.md): bot face rendering, now-playing page, glint).

## Where things live

- [`design/faces/`](../design/faces/README.md): the canvas export (`canvas/`), a standalone animated gallery (`gallery/`), the generators and check scripts (`tools/`, `build_all.py` rebuilds the rest) and review shots (`shots/`). Its README explains each part and the build.
- [`bot-review.md`](bot-review.md): review of the real bot (`BotNow`, a port of the firmware) and the BotNext proposals P1-P12, with measurements.

## Design system (all faces)

- 466×466 round, true black background. One hero per face (icon or cover), one big glanceable value, one sub-line. No page captions: the hero and the unit say what the face is.
- **Edge ring** at r 222, 12 px, round caps, 270° gauge open at the bottom (weather) or full circle from 12 o'clock (music progress). The current value is a ring marker (black r 17 + r 10 ring, 5 px #F4F4F2) on weather; a bright head dot (r 8) with a small accent glow on music.
- **Colour carries the value**: temperature scale (−20 #B070E0 … 0 #66C2EC, 8 #4AC5B9, 12 #8ACF7A, 15 #E8D44A, 25 #F08A3A, 35 #FF5050, 40 #E0306A), Beaufort wind bands, European AQI bands, UV (3-5 #F0E641, 6-7 #F08A3A, 8-10 #FF5050, 11+ #B060E0), rain intensity (light #4F81B7, moderate #5C9CE0, heavy #3D7BF5). The big number, ring gradient and end labels use the same scale.
- **Type**: big values 96 px (`font_digits_96`), unit and sub-line 30 px, gauge end labels 36 px (`font_face_36`), secondary 24 px. Both 96 and 36 are compiled subsets from `firmware/tools/gen_face_fonts.py` (see Fonts).
- **Arrival** (on page show only): marker sweeps from the gauge start, the number counts up (step-end, ease-out, ~1.3 s), the hero slides or fades in. Then still, except small ambient loops.
- **Filled shapes, soft rim fade**: clouds, sun, moon fill with colour; side elements fade out between r 160 and r 195 instead of a hard clip.
- **Face dots** (6) at the bottom, r 206 arc; Rain has left-edge view dots (now / forecast).
- **Night**: the firmware dims the screen at night; faces never dim themselves.

## Rendering rules from the measurements

1. **Per area, not per pixel.** Every invalidated area costs ~3-4 ms (render setup + its own QSPI transfer); pixels are cheap in comparison. One merged box (12.8k px) 4.1 ms vs three small boxes (7.9k px) 7.8 ms. Beyond 32 areas LVGL redraws the full screen (`LV_INV_BUF_SIZE`). So: one box per moving layer per frame, never one per shape.
2. **No LVGL image transforms in motion.** Image scale was ~160 ms/frame on the bot outline. Rotation or zoom of a picture at runtime (vinyl label spin, cover "Ken Burns" zoom) is out; use a few pre-rendered frames or drop the effect.
3. **No tear sync** (TE not routed). Large moving areas tear; small, even steps hide it. Prefer one-off transitions to looping full-face motion.
4. **Bake once, blit often.** Gradients, soft fades, glows, scrims and text over images go into RGB565 / RGB565A8 images (PSRAM) when the data changes, like the bot outline and the curved host label.
5. **Ambient motion ≤ 15 fps** and only on the hero's bounding box; arrival up to 30 fps.

## Weather faces

| Face | Build notes |
|---|---|
| All gauges | The 150-piece gradient arcs on the canvas are a preview artefact. On the device: rasterise the ring once per data change with the existing analytic ring rasteriser and a colour LUT along the angle, keep it as one image; only the marker box moves. |
| Temperature (day/night) | Sky slot by `is_day` + cloud cover: clouds (white < 70 %, light grey < 90 %, dark grey overcast; night tones at night), big sun (hot, AT4), moon with phase (night, AT5). Pre-render each cloud/sun/moon sprite with its rim fade; the drift moves one box at ≤ 15 fps. AT4: the sun rays are static (baked); only the sun box (glow, UV pulse) and the heat shimmer move. AT5: 4 twinkling stars (2 areas), the moon and the other stars are static. Open: show the moon only when it is above the horizon (moonrise/moonset). Mercury, ticks, ° colour: one small box. UV advice line only in daytime and UV ≥ 3. |
| Rain now (AR2, umbrella) | No "Rain" caption. Rain is irregular A/B drop sheet tiles (three sheets, periods 1.33, 1.6 and 1.13 s) moving inside a static clip with a baked fade mask, not a regular grid; fewer drops than the first draft. 15 fps, 3 areas: left and right rain sheets, the band above the canopy. Edge drips slide off both umbrella sides; splashes land on the drop's hit frame. Dry zone under the canopy = fewer pixels to draw. |
| Rain forecast (AD3-AD5) | Countdown value ("2½ h", "45 min"). 2 areas at 15 fps, each box = cloud + its drop sheet. Light (AD4): one white cloud. Regular (AD5): lighter filled clouds from both sides. Heavy (AD3): grey shaded clouds from both sides. Drop slide along the ring: one small box during arrival. |
| Wind (AW2-AW4) | No "Wind" caption; the hero icon is 1.9× larger, using the freed space. Windsock with 3 baked poses per wind-speed level (limp, gentle, straight out), switched in an A-B-C-B cycle faster as the wind rises (667 / 333 / 133 ms); no live rotation or skew. 1 area (the sock and its streaks). |
| Sun | Rising sun sprite masked by the horizon; rays as one baked fan image. |
| Air | Flower healthy / wilted sprites; one falling petal or leaf = one small box. |
| Next days | Static after arrival; range bars and icons baked once per forecast update. |

## Music faces

- **Ember prepares the picture**: one 466 JPEG, already circle-cropped to r 208 (the disc ends before the edge ring at r 222) with the bottom scrim and rim vignette baked in, plus an `accent` colour (dominant colour, contrast-checked) in the view block. The knob decodes once (ROM TJpgDec, ~200-225 ms) and blits.
- **Memory**: 466² RGB565 = 434 KB, replacing today's backdrop (283 KB) + album circle (115-173 KB); net ~-0.1 MB PSRAM.
- **Paused / dimmed / volume dim**: LVGL image opacity or recolour on the same image, not a second greyscale copy (+434 KB). Desaturation from Ember as a separate `kind=paused` picture only if the recolour looks wrong on hardware.
- **Ring on black** (r 222, outside the cover): each progress step invalidates one box from the old head to the new head; head glow = cached A8 sprite. No repaint of the cover.
- **One ring, two modes**: turning switches the ring to volume (instant switch, then each detent invalidates only the added arc + the number box); 1.5 s after the last detent back to progress. No second arc.
- **Text** (title 30, artist 24, time 24) and a 44 px artist photo: render into one RGB565A8 image on track change; the time is its own small box updated once a second.
- **Track change** (M8): fade through black, started after the new cover has decoded: the old cover holds ~0.25 s, fades out in 3 steps, one black frame, the new cover fades in over 3 steps; the ring unwinds, re-tints on the black frame and regrows. No slide or push (that is ~1 s of full redraws with tearing). Both pictures ~0.9 MB PSRAM during the change.
- **No cover** (M15): vinyl only when there is no cover; no cover-on-vinyl variant. Plain label, spin = 8 baked label frames at 4 fps (1 area, the 104 px label). Grooves never redraw.
- **Burn-in**: existing rule (dim after 3 min, 8 px drift once a minute) stays; the drift is one full redraw per minute, fine.
- **BLE remote**: vector glyphs on black, the relative step arc on the edge ring: cheap.
- **Bot music moments**: headphones + notes are small sprites around the eyes; the beat bob moves one box (eyes + cups) at ≤ 30 fps.

## Bot weather moments (v2)

The bot page shows the coding agents' status first; weather only interrupts it briefly.

- **When**: on a weather change (rain starts or stops, storm, snow, heat ≥ 28°, strong wind, frost) and as a periodic update every 30 min by default. The periodic update is skipped when nothing changed since the last one; the interval is a knob setting.
- **Priority by mood**: waiting / error: no moment (a change moment waits until the status calms; periodic ones are dropped). Working: quiet variant, only the sprite and the text line; the eyes keep their working behaviour, the glint keeps orbiting, the host label stays. Idle / done: full variant with eye acting (glance, blink, squint, round eyes, happy hop).
- **Timeline** (15 fps frames, ~4.5 s): text line fades in (frames 0-2), sprite rises and fades in (3-8), hold to ~3.9 s, sprite and eye acting out (59-64), text fades out (65-67). Then the bot is exactly as before.
- **Eyes**: the real bot eye shapes (dash, round, happy), ported from `BotNow` / `BotNext`, not stand-ins.
- **Layout**: sprite box (156,62)-(308,152) above the eyes, sprites pre-rendered at 0.9 scale, filled style like the weather faces, pre-drawn poses switched by opacity. Text: one straight centred 30 px line under the eyes at y 310-346, sentence case, weather colour ("Rain until 14:30", "12° Cloudy", "Storm", "Snow −2°", "Wind 45 km/h", "Hot 31°", "Clear 9°", "Rain stops"). The curved host label never swaps; in working the tool icon hides while the line shows (frames 0-67).
- **Never**: change the ring colour (that is a ~120 ms outline re-render), full-ring effects, rain around the eyes, rotating ray rings.
- **Budget**: full variant ≤ 2 areas per frame (eyes + sprite, or eyes + text on its 6 fade frames); working ≤ 3 (+ glint). The one exception is AfterRain's hop: ~1 s, one-off, the whole outline moves and squashes as in normal bot life. Measured eye boxes per moment are in the `design/faces/` README.

## Fonts

Compiled: Montserrat 14/18/24/30/48 (LVGL built-in) plus two generated subsets (`firmware/tools/gen_face_fonts.py`, Montserrat Medium from LVGL's `scripts/built_in_font`, 4 bpp, host test `test/host/test_face_fonts.c`):

- `font_face_36`: gauge end labels, glyphs ` %+-.0-9:h°−`; line box of the built-in `lv_font_montserrat_36` (40 / base 7). Bitmaps 3,379 B.
- `font_digits_96`: big values, glyphs ` %+-0-9:°½−`; line box 107 / base 19 (built-in 36 scaled). Bitmaps 24,923 B.
- Neither is in `main/CMakeLists.txt` yet (only the host test compiles them); add them to `SRCS` with the first face that uses them. Add a glyph: edit `FONTS` in the script, rerun, run the host tests.
- Curved labels (title, host) keep using the arc_text path into a cached canvas.

## Build order

The BotNext port comes first (see Next session).

1. Temperature: baked gauge ring image + moving marker + count-up value on show; then the other weather gauges the same way.
2. Music: full-cover picture from Ember + accent, edge ring on black, cached text, opacity/recolour states, volume mode on the ring.
3. Hero sprites with baked rim fades (clouds, sun, moon, flower, windsock poses).
4. Rain drop sheets at 15 fps; measure areas/frame and ms in the stats log before adding more drops.
5. Track-change fade, vinyl fallback, bot music moments.
6. Bot weather moments (sprite box + text line on top of the existing bot view; trigger and priority logic is pure C, host-testable).

## Next session

**State**
- All of this is committed: this file, `docs/bot-review.md`, `design/faces/`, the face fonts and their generator and host test.
- The design canvas https://claude.ai/artifact/5EidnPkQVh6dRTag6nz5Wa is the live source for comments. `design/faces/canvas/` mirrors it; if the canvas is edited again, re-export it into `design/faces/canvas/`.
- Open `design/faces/gallery/index.html` in a browser to see every face animate.
- `python3 design/faces/tools/build_all.py` regenerates `canvas/` and `gallery/` from `tools/` + `tools/orig/`. `tools/verify_gallery.py` checks the pages (needs Playwright).

**Open decisions**
- B2 moment icon size.
- Whether waiting's loud first 20 s in BotNext is too much.
- Moon on the night face only when it is above the horizon.
- What Ember must provide: hourly rain / wind / gusts, daily forecast, cloud cover, UV index, moonrise/moonset, golden/blue hour times, full-size album cover + accent colour.

**Next steps, in order**
1. Port BotNext's changes into `firmware/components/bot/bot_behavior.c` / `firmware/main/bot_view.c` with host tests, one proposal at a time, checking invalidated areas on the device.
2. Write the Ember weather/music API spec from the data list above.
3. First device face: Temperature (baked gauge ring image + moving marker + count-up value).
4. Then music.

**Remember**
- Follow the live-device rules in [`AGENTS.md`](../AGENTS.md).
- The face fonts and their generator exist already (see Fonts); don't re-create them.
