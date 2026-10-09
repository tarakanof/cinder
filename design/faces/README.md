# Cinder faces: design sources

Design work for the round 466×466 knob display: the bot (today and proposed), bot weather moments, the animated weather faces and the music faces. Everything here is a design artefact. The firmware does not read these files.

| Folder | What it holds |
|---|---|
| `canvas/` | The design-canvas sources: one `.dc.html` per face on the canvas, plus `canvas.json` (board positions, titles, row notes, order). |
| `gallery/` | A standalone viewer generated from `canvas/`. It needs no design runtime. |
| `tools/` | The generators that write `canvas/`, the gallery builder, and the screenshot and verification scripts. |
| `shots/` | Contact sheets from the design reviews. Each records its own point in time; the gallery is always current. |

The design review of the bot is in `docs/bot-review.md`.

## Rows

The gallery and the canvas show the same rows, in canvas order.

1. **Bot today vs proposed.** `BotNow` is a live simulation of the firmware bot: a JS port of `bot_behavior.c`, `bot_shape.c` and the drawing rules of `bot_view.c`. `BotNext` is the proposed bot, built from the same port with the changes P1–P12 from `docs/bot-review.md`. Both take the props `mood`; `BotNext` also takes `debug`, `sleepAfter` and `waitingFast`.
2. **Bot weather moments v2** (`B2-*`). Brief weather interruptions on the agent-status bot:
   - Glance (the periodic update)
   - Rain
   - Storm
   - Snow
   - Wind
   - Heat
   - Night
   - AfterRain
   - RainWorking (the quiet variant in the working mood)
3. **Weather faces:**
   - temperature: `AT1`, `AT3`–`AT6`
   - rain: `AR2`
   - rain forecast: `AD3`–`AD5`
   - wind: `AW2`–`AW4`
   - sun: `AS2`
   - air: `AA2`, `AA4`
   - next days: `AN1`
4. **Music faces:**
   - now playing and its alternatives: `M1`–`M4`, `M6`, `M15`
   - feedback: `M7`–`M9`
   - BLE remote: `M10`–`M12`
   - bot music moments: `M13`, `M14`

## Viewing

Open `gallery/index.html` in a browser. Each face plays in a 466×466 iframe, scaled down; click a title to open it full size.

On a face page:
- The controls under the face set its props: enum props as a menu, booleans as a checkbox, ranges as a slider. They are written to the URL, for example `B2-Rain.html?mood=idle` or `BotNext.html?mood=done&debug=true`.
- `?embed=1` hides the controls.
- The only network request is the Montserrat font from Google Fonts.

### The gallery shim

`canvas/` holds the sources for the design canvas.

A `.dc.html` file has three parts:
- an `<x-dc>` element holding a `<helmet>` (font link and a `<style>` with the keyframes);
- the face markup, with `{{name}}` holes;
- a `<script type="text/x-dc" data-props=…>` that holds the prop schema and, for some faces, a `class Component extends DCLogic`.

The canvas host supplies `support.js` and the design runtime. They are not part of this tree.

The gallery does not need them. `tools/gallery.py` inlines each face's markup and keyframes, and adds a small shim:
- `DCLogic` with `props`, `state` and `setState`;
- `{{hole}}` substitution from the prop defaults merged with `renderVals()`;
- `componentDidMount` and `componentWillUnmount`.

The shim re-renders only when the filled markup changes, so CSS animations keep running. `BotNow` and `BotNext` re-render every frame from their own `requestAnimationFrame` loop, exactly as on the canvas.

## Regenerating

Run from the repo root. You need Python 3.10+. The screenshot and verification scripts also need `playwright` (Chromium), `numpy` and `Pillow`.

```
python3 design/faces/tools/build_all.py     # every generator, then the gallery
python3 design/faces/tools/<gen>.py         # one generator
python3 design/faces/tools/gallery.py       # the gallery only
```

| Script | Writes | Built from |
|---|---|---|
| `temp.py` | AT1, AT3, AT4, AT5, AT6 | `tools/orig/AT1.dc.html`. AT4 and AT5 go through `opt2.py`. |
| `ad.py` | AD3, AD4, AD5 | `tools/orig/AD1.dc.html`, through `opt2.py` |
| `umbrella.py` | AR2 | `tools/orig/AR2.pre-umbrella.dc.html`, through `opt.py` |
| `wind.py` | AW2, AW3, AW4 | `tools/orig/AW2.dc.html` |
| `sun.py` | AS2 | `tools/orig/AS2.dc.html` |
| `air.py` (+ `plants.py`) | AA2, AA4 | `tools/orig/AA2.dc.html`, `AA3.dc.html` |
| `days.py` | AN1 | built from scratch |
| `music.py` | M1–M4, M6–M15 | built from scratch. M8 comes through `opt.py`, M15 through `opt3.py`. |
| `bot2.py` | B2-* (9 faces) | built from scratch, with the eye geometry ported from BotNow and BotNext |
| `gallery.py` | `gallery/*.html` | `canvas/` |

Where those inputs and libraries come from:
- `edit.py` holds the shared helpers: count-up, arrival keyframes, colour scales, end labels, marker and row faces.
- `opt.py`, `opt2.py` and `opt3.py` hold the device-optimizing transforms. They write no files of their own.
- `tools/orig/` holds the original artboards the generators edit.

`BotNow.dc.html`, `BotNext.dc.html` and `canvas.json` are hand-written; `canvas/` is their source. All other faces must come out of the generators byte-for-byte. To check, run `build_all.py` and diff `canvas/`.

The `canvas/` copies of `BotNow` and `BotNext` have no code comments. Their notes are below. The behaviour is unchanged: the seeded simulation output is identical before and after removing the comments.

## Verification

- **`tools/shoot.py NAMES [ms,…]`.** Builds a harness page per face and writes PNG frames to `design/faces/build/frames/`. Treat `build/` as scratch and keep it out of git.
  - The page inlines the face's style and markup and uses the local Montserrat 500 from `tools/fonts/` (SIL OFL 1.1, see `OFL.txt`).
  - Frames are taken in headless Chromium by pausing every CSS animation and setting `currentTime`.
  - Launch Chromium with `--disable-threaded-animation`. Without it, paused step-end opacity animations on SVG groups can paint stale frames.
- **`tools/bot2shot.py [names] [out.png]`.** Checks the B2 faces and writes `build/bot2-check.png`. For each face it:
  - takes shots at 300, 2000 and 5000 ms;
  - finds the changed pixels between consecutive frames on the 15 fps grid and maps them to the declared redraw regions (sprite box, eye box, text line, outline), so it can count the areas per frame;
  - measures the eye extents over the whole cycle in every mood the board offers, against the sprite box and the text line;
  - checks that the frame after the moment matches the plain bot pixel for pixel.
- **`tools/verify_gallery.py`.** Opens every gallery page.
  - The Google Fonts request is answered with the local font, and `window.__botSeed` is fixed.
  - It checks for script errors, that the face renders and that it animates.
  - BotNow and BotNext must show eye motion over 2 s.
  - Twelve faces are compared at 3000 ms against the harness frame (B2-Snow, B2-AfterRain, B2-RainWorking, AT4, AR2, AD3, AW4, AS2, AA4, AN1, M8, M15). All match exactly.

## How faces are modelled for the device

These rules come from the cinder face-design doc. They apply to every face marked device-optimized, which is all of the shipped weather, music and B2 faces:

- **Redraw areas.** Each redraw area costs about 3–4 ms. Above 32 areas LVGL redraws the whole screen, and there is no tear sync, so keep moving areas small.
- **No live image transforms.** No live image scale or rotate. Motion is translate or opacity, or pre-drawn poses switched with step-end opacity:
  - the windsock: 3 poses in an A-B-C-B cycle;
  - the vinyl label: 8 pre-rendered frames at 4 fps;
  - the umbrella: 3 opening poses.
- **Ambient motion.** At most 15 fps, inside one bounding box per layer. In CSS each keyframe interval gets `steps(n)` with n = interval × 15, so frames are 66.7 ms. Equal keyframe intervals keep the steps on the grid.
- **Bake what you can.** Gradients, fades and text are baked. Rain is drawn as A/B tile sheets moving inside a static clip, with a baked fade mask.
- **Every redraw area is a fixed box.** The faces' aria-labels state their area budget.

## Canvas timings vs the device

- **Weather and music faces.** On the canvas they loop every 6 s, with the arrival in 0–22 % (0–1.32 s).
  - The marker sweeps from the bottom (`wxarrive`, `cubic-bezier(0.2,0.8,0.2,1)`).
  - The number counts up on a strip. Frame k of N lands at 22·(1 − (1 − k/N)^(1/3)) % of the cycle, which follows the same ease.
  - On the device the arrival plays once, when the page is shown, and the ambient loops run on their own periods.
- **B2 bot weather moments.** On the canvas they loop in 6 s: the moment plays in 0–4.47 s, then 1.5 s of the plain bot. On the device a moment plays once (about 4.5 s) and the bot returns exactly to its previous state.
  - **When a moment plays:** on a weather change, or as a periodic update about every 30 min.
    - waiting / error: never. A change moment is queued until the status calms; periodic ones are skipped.
    - working: the quiet variant only. The sprite appears and the text line shows; the eyes keep working and the glint keeps orbiting.
    - idle / done: the full variant, with the eyes acting.
  - **Timeline in 15 fps frames:**
    - frames 0–2: the text line fades in over 3 steps (in working the tool icon hides on frame 0);
    - frames 3–8: the sprite rises 7 px and fades in;
    - 0.53–3.87 s: hold;
    - frames 59–64: the sprite sinks and fades out, and the eyes return;
    - frames 65–67: the text fades out (the tool icon returns on frame 67).

    The text never steps on a frame where the sprite box redraws.
  - **Layout:**
    - Sprite box: (156, 62)–(308, 152). Sprites are pre-rendered at 0.9 scale.
    - Text line: y 310–346, a straight 30 px line in sentence case and the weather colour.
    - The curved host label never changes.
    - The ring colour never changes during a moment; an outline re-render costs about 120 ms.
  - **Eye boxes per face** (measured, 3 px margin):

    | Face | Eye box |
    |---|---|
    | Glance, Rain | (138,158)–(327,265) |
    | Storm | (138,166)–(327,265) |
    | Snow | (125,163)–(340,284) |
    | Wind | (138,172)–(384,270) |
    | Heat | (138,173)–(327,265) |
    | Night | (125,176)–(327,295) |
    | AfterRain | (132,158)–(333,265) |
    | RainWorking | (90,176)–(368,300) |

  - **Budget:** full variant at most 2 areas per frame (eyes plus sprite, or eyes plus text); working at most 3 (adds the glint). The one exception is AfterRain's hop: about 1 s, one-off, and the whole outline moves and squashes as in normal bot life.
- **AR2 rain.** The rain runs as three tile sheets with periods of 1.33, 1.6 and 1.13 s, plus rim drips looping every 1.6 and 2.13 s. Splashes are timed to land on the drop's hit frame.
- **M8 track change.** The old cover holds for 0.25 s while the new one decodes, then fades out over 3 steps. A black frame shows at 350–416 ms, then the new cover fades in over 3 steps. The ring unwinds, re-tints on the black frame and regrows.
- **M15.** The vinyl label turns as 8 pre-rendered frames at 4 fps. The progress head and time update once a second.
- **Music layout** follows the firmware Now Playing page:
  - progress ring at the screen edge (r 222);
  - backdrop disk r 188;
  - volume arc r 207–215.

  All album art and names are invented. "Plex" and "Apple Music" appear only as plain text.

## Bot eyes: BotNow, BotNext and the B2 port

### BotNow

The artboard centre is 233,233; the device frame is 472×466 with CX 236. Times are seconds since the artboard mounted.

- On the device a sleepy bot stays asleep on an idle request; the artboard's mood switch forces idle.
- The outline re-renders on a mood or shape change, and the squash variants come ready one by one on core 1.

### BotNext changes, marked [Pn] as in the review

- **[P1] Micro-saccades.** They come lognormal(0.8 s) apart once a fixation has lasted 0.6 s. Each is 0.012–0.025 gaze (1.5–3 px), 25 ms ease-in-out, biased back toward the fixation target. There is no lean or blink, and the fixation end is unchanged.
- **[P2] Sleep.**
  - Breath: a sine whose period is jittered ±15 % per cycle. It moves eye height and y.
  - Sleepy lids ride the breath (0.80 ± 0.12).
  - The sleepy blink is heavy: 0.25 s close, a 0.4–1.2 s hold, then a 0.3 s reopen with a small overshoot.
  - Asleep after `sleepAfter` s of sleepy (10 min on the device): the lids shut over 1.2 s, the gaze settles low, and saccades and blinks stop.
- **[P3] Reading v2.** Lines of 3–6 steps with 12 % regressions. The line drifts down a page of 4–6 lines. After the return sweep comes a longer fixation, and a page turn brings a glance with a blink. There are rare glances at the viewer and at the Clawd mark.
- **[P4] Waiting.** It escalates in phases: phase 1 for 20 s, phase 2 until 3 min, then a reminder burst every 5 min (`waitingFast`: 6 s, 20 s, 45 s). The phases restart on entry; on the device also on a new waiting host and on page show.
  - The phase-3 nudge plays squash variant −2 then +1 (0.1 s each) and pulses the badge 1 → 1.18 → 1 over 0.35 s.
  - A double hop chains after the landing at +0.92 × hop length. The review's +0.55 s would cut the 1 s hop mid-air.
- **[P5] Outline motion.** The outline moves for hops only (the lean stays on the eyes), and the even-pixel rounding is symmetric.
- **[P6] Mood change.**
  - The new outline (shape picked from the mood) is rendered on core 1 into a spare variant slot (about 0.12 s) while the old ring stays.
  - It is swapped in once the lids are shut. Then the −1 / +1 squash variants (ready 0.24 / 0.36 s after the change) play a 60 + 80 ms settle as the eyes reopen.
  - The badge grows (ease-out-back) or shrinks from the ring swap, in the colour of the badged mood.
- **[P7] Done.** A hop at +0.5 s, a giggle squash on the happy eyes at +1.6 s, and one glint lap in the done colour from +2.2 s (3 s, starting at 12 o'clock), then calm. The glint hides while a squash variant shows.
- **[P8] Badge and error mark.** Badged moods rest at (0.5, 0.6), clear of the badge. In error the Clawd mark is left out of the label canvas because the triangle edge would cut it; the text stays.
- **[P9] Tilt.** Tilt and rise follow the gaze side, so a left glance mirrors a right one.
- **[P10] Lids.** The lid is a clip line from the top: on the device, zero the alpha rows above it in `eye_raster`. The capsule keeps its open shape and only thickens a little as it closes (dash ×(1 + 0.35·lid), round ×(1 + 0.15·lid)), so there is no round-dot frame. Happy eyes keep the old squint.
- **[P11] Error.** Frustrated gaze: mostly the viewer or down-centre with longer holds, sometimes a sharp side dart. A shake as the eyes reopen, then a huff every lognormal(12 s).
- **Debug counters.** An eye redraw is any eye point, width or lid that moves by 0.5 px or more. An outline move is any change of variant, offset, colour or opacity.

### How B2 approximates the firmware eyes in CSS

The B2 faces use the BotNow/BotNext eye geometry: R = 0.84 × 233, eye scale 1.15.

| Shape | Size |
|---|---|
| Dash (idle, working) | 31.5×85.5 px |
| Round (waiting, used for Snow) | 67.5×99 px |
| Happy (done) | 67.5 px wide, 24.8 px stroke |

They play it back as keyframes rather than a live simulation. The approximations:

- **Gaze moves.** Glides use `cubic-bezier(0.65,0,0.35,1)` over 0.42 s. Saccades use `cubic-bezier(0.34,1.4,0.64,1)` over 25 + 45 × amplitude ms. Lean offsets are folded into the same keyframes instead of trailing by 30 ms.
- **Lids.** The lid curve (75 ms close, 35 ms hold, 150 ms open, quadratic) uses quadratic-like cubic-béziers. The lid clip line is measured from the untilted capsule, which is up to 3 px off when the dash is tilted.
- **Foreshortening.** `fx` and `fy` are applied to the eye spacing but not to the stroke size, which differs by at most 3 %.
- **Hop squash.** It is continuous. The firmware picks one of 11 pre-rendered outline variants and moves the outline in even pixels.
- **Shape swaps** (round or happy and back) happen at a blink's full close, as in the firmware.

## Assets

- **Art.** All art is drawn in SVG for these faces. Album art and names are invented.
- **The Clawd mark** in BotNow and BotNext is drawn from the firmware's own tool-mark data. The B2 working face uses a neutral terminal-prompt placeholder instead.
- **Fonts.** The faces load Montserrat 500 from Google Fonts. The screenshot tools use the local copy in `tools/fonts/` (SIL Open Font License 1.1).
