# Cinder bot: design and animation review

Baseline: `design/faces/canvas/BotNow.dc.html` is a live simulation of today's firmware. Its JS port of `bot_behavior.c` and `bot_shape.c` gives the same poses as the C code: 390 sampled poses across all six moods over 40 s, compiled on the host with the same seed, match to 5 decimals. The contact sheet is `design/faces/shots/botnow.png`. The numbers marked *(sim)* below come from 10 simulated minutes per mood (seed 777, 60 fps). "Redraw" means the eye geometry moved by 0.5 px or more, which is close to `bot_pose_same`.

**Cost model used below** (from features.md):
- A pose redraw (eyes only) is about 8 ms on core 0, so 1 extra redraw per second adds about 0.8 points of core 0.
- Moving or swapping the outline is one 448 px area: frames of 15–22 ms at 80 MHz, and the ring tears.
- A glint step is about 4.1 ms.
- Re-rendering the outline costs about 120 ms on the LVGL task.

## 1. What the bot does today

| Mood | Eyes (`eyes_for`, `bot_eye_strokes`) | Motion vocabulary (`bot_behavior.c`) | Ring and extras (`bot_view.c`) | *(sim)* redraws/s · still % · outline moves/min |
|---|---|---|---|---|
| idle | dash capsule 0.14×0.38 r ×1.15 (≈31×85 px), tilted up to 27° | `next_target`: 45 % rest (up-right 0.67, 0.77), 25 % viewer, 30 % random. Fixation lognormal 2.2 s. Blinks every 4 s (median). Sleepy after 300 s | grey ring at 55 %, 6 px | 7.9 · 87 % · 5 |
| sleepy | dash with a fixed heavy lid of 0.55 | slump 1.6 s (eyes −0.1). Looks down (y −0.5…−0.2). Fixation 8 s. Blinks every 2.8 s at 2.2× slower speed (0.57 s) | grey ring at 55 % | 10.0 · 83 % · 0 |
| working | dash | "Reading": `read_x` steps +0.28…0.45 from −0.6 to 0.7, then jumps back to −0.7. y fixed at −0.15. 10 % glances at the viewer. Fixation 0.9 s. Blinks every 6 s | green ring, a glint at 15 fps (3 s per turn), CLAUDE label and the Clawd mark. Glint chase every 5–15 min | 8.6 · 86 % · 0 |
| waiting | round pill 0.3×0.44 | 75 % looks at the viewer. A hop at +0.6 s, then every lognormal(4.5 s, 2.5–9 s), forever | amber ring, badge (`EASE_OUT_BACK`), label | 15.1 · 75 % · **244** (12 hops/min) |
| done | happy arc (7-point quadratic curve) | 50 % rest, 50 % viewer. Fixation 2.5 s. Blinks every 3.2 s | blue ring, badge, no label | 6.0 · 90 % · 10 |
| error | angry slashes at ±55°; triangle 1 (reach 0.4, eyes −0.12) | uniform random darts (±0.9 × ±0.7). Fixation 0.9 s. Blinks every 2.2 s | red triangle ring (snaps at triangle 0.5), label | 13.0 · 78 % · 35 |

**What already reads as alive.** There is a solid base, so keep it:
- Lognormal timing everywhere (`lognormal`).
- Per-eye blink lag of 0–20 ms, and a double blink 12 % of the time (`start_blink`).
- Big saccades (amplitude over 0.8) blink 60 % of the time.
- Saccades last 25 ms + 45 ms × amplitude with `EASE_OUT_BACK`. That is about a 7 % overshoot, around 10 px on a full sweep.
- The lean follows 30 ms late (follow-through).
- Perspective foreshortening (`fx`, `fy`).
- The eye shape changes behind closed lids (`swap_eyes_on_close`).
- The hop has anticipation, stretch, airtime and a landing squash (`hop_curve`).
- A glide (0.42 s) on mood entry.

**What reads as mechanical.**
- **Long horizons.** Each mood is one stationary random process with no arc over minutes. Working repeats the same left→right "typewriter" cycle about every 3.6 s for hours. Waiting hops about 12 times a minute forever, which habituates within a minute and is the most expensive animation. Done is the stillest mood (90 % still), so the reward is a still image. Sleepy is busier than working (10 redraws/s, 19.5 slow blinks/min) and never actually falls asleep.
- **Dead-still fixations.** Between saccades the eyes do not move at all. Idle holds up to about 6 s and sleepy up to about 7 s. Real eyes never freeze.
- **The blink is a capsule morph, not a lid.** `L(0.14,0.24) × L(0.38,0.06)` passes through a 0.19×0.22 dot at lid 0.5. That makes a "surprised dot" frame in every blink (sim idle at 0.85 s). Sleepy sits permanently at lid 0.55, so **sleepy eyes are two round dots** (sim sleepy row). They read as small round eyes, not heavy lids.
- **The tilt ignores which way the bot looks.** `lean` uses `fabs(cx)`, and `rise = 0.04·side·lean`. Looking left therefore tilts the eyes "\" and raises the right eye, exactly like looking right (sim idle at 3 s). Worth checking against the Swift original.
- **Mood change.** `bot_view_update` re-renders the outline on the LVGL task at once (about 120 ms), while the eyes are still open. The colour cuts *before* the lids close, the stall eats the 120 ms close phase of the blink, and the pop scales the eyes only. A uniform scale keeps the ratio at 1, so `rim_variant_for` stays on the base outline and the ring never pops.

**Design bugs visible in the simulation.**
1. **The badge bites the right eye in done (and in waiting when it looks up-right).** The black gap disc (r = 0.3 r = 59 px at 45°, created *after* the eye canvases, so it draws on top) cuts the top right of the right eye whenever the gaze is near rest. Done picks rest 50 % of the time. See the sim, done at 9 s.
2. **In error, the triangle's bottom edge slices the Clawd mark.** The mid-side is at 0.66 r ≈ 129 px below the centre, and the mark spans 98–146 px, so the CLAUDE label hangs outside the body.
3. The outline `dy = lround(…) & ~1` rounds negative values down (−1 → −2). Glance leans therefore shift the whole ring by 2 px on upward looks only. That is 5, 10 and 35 full-outline redraws a minute in idle, done and error, and nobody reads it as life.

**Proportions and colour.**
- The eyes (31–67 px strokes) are heavy next to a 6 px ring. The design system's other rings (Pomodoro, weather) are 12 px, and the 12 px glint is wider than the ring it rides, so it looks like a comet over a wire.
- The resting gaze up-right leaves the lower-left of the face empty in idle. That is good character; don't centre it.
- In working, the Clawd mark has its own eyes 98 px below the bot's, so two faces compete. Make it a prop the bot looks at (P3), not a second face.

## 2. Proposals (ranked; ★ = try first)

Effort: S under a day, M one to three days, L more. All new state is a few dozen bytes in `bot_t`, which is internal `.bss`: tiny, but `EXT_RAM_BSS_ATTR` is available if needed. Every new field must join `bot_pose_same` and the host tests.

**★ P1. Micro-saccades in fixations** (all moods except a chase or tracking)
- Change: while a fixation is longer than 0.6 s, fire a micro-saccade every lognormal(0.8 s, σ 0.4, 0.3–2.5 s). Amplitude 0.012–0.025 gaze (≈1.5–3 px on screen), random direction biased back toward the fixation target (so the eyes don't wander), 25 ms with `EASE_IN_OUT` (an overshoot at this size is sub-pixel). No lean, no blink coupling, and `next_saccade_at` stays unchanged. Sleepy gets half the amplitude and a 2 s median.
- Why it adds life: the biggest single cue that someone is behind the eyes. It removes the 2–7 s frozen holds.
- Reuses: `start_saccade_to` with a `micro` flag that skips the lean and blink branches.
- Device cost: 1–2 eye redraws per micro-saccade, about +1.5 redraws/s, so about +1.2 points of core 0. Eye areas only (2 areas), steps of 3 px or less, so no tearing. RAM: about 16 B.
- Effort: S.

**★ P2. Eye breathing, and a real sleep**
- Change, part 1: add a pose field `breath` in [−1, 1]. It is a sine whose period is jittered ±15 % per cycle: idle 4.2 s, working 3.2 s, waiting 2.6 s, done 3.6 s, sleepy 5.5 s. In `bot_eye_strokes` it changes the eye height by ×(1 + 0.025·breath) and moves the eye centre by +0.006·breath (≈1 px). Never put it into `scale_x/y`, which would pick outline variants.
- Change, part 2 (sleepy): breath drives the lid, lid = 0.80 + 0.12·breath. The eyes become near-flat dashes that open a crack on the inhale, which replaces the dot look. Replace the 2.8 s blinks with heavy blinks every lognormal(7 s): lid 1 for 0.4–1.2 s, reopening with `EASE_OUT_BACK`.
- Change, part 3 (asleep): after 10 min in sleepy (a new sub-state), the lid is 1 (closed lines) and saccades are off. Breath becomes a 1.5 px bob every 6 s. The ring fades to 25 % with one re-render.
- Why it adds life: a body that breathes. Sleepy becomes charming instead of "small dots", and asleep becomes calm.
- Reuses: lids, `slump`, the `swap_eyes_on_close` style of state.
- Device cost: awake breathing is about 2 redraws/s, so +1.5 points of core 0, eye areas only. Asleep is about 1 redraw/s against 10 today, so −7 points of core 0 overnight and less burn-in on the static ring.
- Effort: S for breathing, M with the sleep sub-state.

**★ P3. Reading v2 for working** (the mood seen most)
- Change: in `next_target` for `BOT_WORKING`:
  - Lines of 3–6 forward steps of 0.18–0.38, with a 12 % chance of a short regression (−0.10…−0.22).
  - The line y moves down the "page", −0.05 − 0.06·line for lines 0–4.
  - After the return sweep (already a big saccade, so it blinks 60 % of the time), the fixation is ×1.3 longer.
  - After 4–6 lines, a "page turn": a glance up (y +0.3) or at the viewer for 1.2 s, plus a blink.
  - 5 % of saccades glance at the Clawd mark (gaze (0, −0.35) for 0.6–1.0 s). Keep y at −0.35 or higher, or the eye bottoms reach the mark at 98 px.
- Why it adds life: an arc over about 20–40 s instead of a 3.6 s loop. The tool mark becomes the thing the bot is watching.
- Reuses: `read_x`, the saccade-blink coupling, `bot_look`.
- Device cost: the same saccade rate, so no change (about 8.6 redraws/s). RAM: 3 ints.
- Effort: S.

**★ P4. Waiting: escalate, then calm down**
- Change:
  - Phase A, 0–20 s: hops every lognormal(2.8 s, σ 0.25, 2–4.5 s). 25 % are double hops: a second hop at +0.55 s with 0.6× the height.
  - Phase B, 20 s–3 min: hops every lognormal(8 s, 5–14 s). In between, a "peek" (look at the viewer plus a double blink) every 4–6 s.
  - Phase C, after 3 min: no hops. Every 12–20 s a "nudge": squash variant −2 → +1 → base over 0.3 s (3 outline swaps), plus a badge pulse (1 → 1.18 → 1, `EASE_OUT_BACK`, 0.35 s). Every 5 min a reminder burst of 2 hops.
  - Restart at phase A when the waiting host or lead changes, or when the page is shown.
  - Vary each hop: height ×0.8–1.15, length 0.9–1.1 s.
- Why it adds life: attention that habituates more slowly (novelty plus escalation), and less of the hop at 12/min forever.
- Reuses: `next_hop_at`, `hop_curve`, the squash variants, the badge tween.
- Device cost: phase C has about 0.2 outline swaps/s against about 4/s today (244 moves/min). That frees roughly 6–8 points of core 0 and removes most hop tearing. Phase A costs the same as today.
- Effort: M (schedule by `mood_since`, plus host tests).

**P5. Free budget: take glance leans off the outline**
- Change: the outline `dy` comes from the hop only (`dy = -s_r·hop_dy·HOP_SCALE`, even). Keep the lean on the eyes. While there, make the even rounding symmetric (round to the nearest even number).
- Why it adds life: indirectly. It removes 2 px ring jumps that read as jitter, and it pays for P1 and P2.
- Device cost: removes about 35 full-outline redraws a minute in error and about 10 in done. That also lowers the 15–22 ms frame max outside hops.
- Effort: S. Do it alongside P1.

**P6. Mood-change beat without the 120 ms stall**
- Change:
  1. On a mood change, bump the generation and let the rim task (core 1) render the new base outline first, into variant slot 0 or 10 (it is rebuilt anyway, so no new buffer). Keep showing the old ring until that slot is ready, about 120 ms, which is when the 1.6× blink is closed.
  2. Swap the ring behind closed lids.
  3. When variants −1 and +1 are ready (+0.26 s), play a ring settle: −1 for 60 ms, +1 for 80 ms, then base.
- Why it adds life: the ring and the eyes change in the same beat, and the ring gets the pop the eyes already have.
- Reuses: the rim task, `VAR_ORDER`, the variants.
- Device cost: removes one 120 ms LVGL stall per mood change and adds 3 outline swaps once. RAM: 0.
- Effort: M.

**P7. Done should feel like a reward**
- Change, after the swap in P6:
  - At +0.35 s, one hop at 0.7× height.
  - At +1.3 s, a "giggle": two lid squeezes to 0.45, 120 ms each, 150 ms apart.
  - A victory lap: the glint runs once in the done colour. `glint_draw_cb` recolours the A8 masks, so pass `mood_rgb(s_rim_mood)` instead of `BOT_WORKING`, and allow the glint for 3 s after entering done.
- Why it adds life: done is rare and should feel earned. Today it is the stillest state.
- Reuses: the hop, the lids, the cached glint masks.
- Device cost: one hop (about 1 s of outline swaps, once), plus a glint lap of 45 × 4.1 ms over 3 s (about +6 points of core 0 for 3 s).
- Effort: S–M.

**P8. Fix the two collisions (design bugs)**
- Change, badge: create `s_badge_gap` and `s_badge_dot` *before* the eye canvases so the eyes draw over the gap, or shrink them to 0.24 r and 0.17 r. For badged moods, move the rest gaze to (0.5, 0.6).
- Change, error: hide the tool mark (keep the text), or lift it to 70–118 px below the centre while triangle > 0.5.
- Why it matters: a bitten eye and a sliced mascot read as glitches, not character.
- Device cost: 0 (draw order, constants).
- Effort: S.

**P9. Direction-aware tilt**
- Change: in `bot_eye_strokes`, use the sign of `cx` for both the dash rise and the tilt angle (`rise = 0.04·side·lean·sgn(cx)`, and the angle × `sgn(cx)`; the same for the 10° tilt of the round eyes).
- Why it adds life: a left glance then reads as a 3/4 head turn to the left. Today left and right look the same.
- Device cost: 0.
- Effort: S. Check whether the Swift version meant this.

**P10. Blink as a lid, with a droop**
- Change: shape the dash and angry lid interpolation so the eye flattens before it widens: height uses `lid^0.7`, width uses `lid^2`. During a blink, the eye centre drops by 0.015·lid (≈2 px).
- Why it adds life: removes the "surprised dot" frame, and the blink reads as weight.
- Device cost: the same blink frames, so 0.
- Effort: S.

**P11. Error: frustrated, not glitching**
- Change:
  - On entry, a shake: gaze x ±0.08, 3 cycles at 10 Hz, decaying over 0.3 s.
  - Then 70 % of looks go to the viewer or down-centre with fixations of 1.2–2.5 s, and 30 % are sharp side darts.
  - Every lognormal(25 s), a "huff": squash variant −2 for 120 ms, then a quick double blink.
- Why it adds life: today's uniform random darts at 0.9 s read as a malfunction. This reads as an emotion.
- Reuses: `bot_look`, `bot_blink`, the variants.
- Device cost: the shake is about 18 eye frames, once. The huff is 2 outline swaps every 25 s. With P5, error drops from about 35 to about 5 outline redraws a minute.
- Effort: S–M.

**P12. Ring weight**
- Change: `RIM_PX` 6 → 9. Keep r 196. The glint's 12 px band then sits on the ring instead of overhanging it. Check that the badge gap still clears the ring.
- Why: visual weight closer to the eyes and to the other 12 px rings.
- Device cost: a slightly longer re-render per variant (about +10 %, on core 1), 0 at runtime.
- Effort: S. Look at it on the knob before keeping it.

## 3. Push-back: what is overkill or out of budget

- **Pupils, highlights, a mouth or eyebrows.** Technically cheap (one more stroke per eye canvas), but they change the character. The minimal two-stroke face is the brand.
- **Continuous ring breathing, glow, colour fades or a triangle morph.** Every step is a 448 px redraw or a 120 ms re-render. Intermediate triangle outlines cost 434 KB of PSRAM each. Keep the snap behind the blink.
- **More squash variants, or squash on every saccade.** Each variant is +434 KB, and every swap is a full area that tears.
- **60 fps for the new micro-motion.** P1 and P2 move 0.5–3 px. The `bot_pose_same` threshold already rate-limits them; don't add more frames.
- **A "Zzz" particle trail.** At most one small sprite box at 10 fps or less, as face-design.md allows; P2's closed, breathing eyes say "asleep" for free.
- **Another go at tear sync.** It is settled (#42): design small, even steps instead.

**Suggested order:** P5 + P1 + P8 (one evening, mostly removing cost and fixing bugs), then P2 and P3 (where the hours are spent), then P4, then P6 + P7 together, since they share the mood-change beat.

## Measured: proposed vs today

How it was measured: BotNow (today) against BotNext (proposed, `design/faces/canvas/BotNext.dc.html`), with the same metric for both. Each artboard's component ran headless in Node at 60 fps, 3 seeds (777, 1234, 99), real timings (`waitingFast` off, `sleepAfter` 600 s except in the asleep row).
- **Redraw:** an eye point, eye width or lid clip moved by 0.5 px or more. That approximates `bot_pose_same`.
- **Outline move:** the outline variant, offset, colour or opacity changed. Each one is a 448 px area.
- The idle window stops at 290 s, before the switch to sleepy.

| Mood, window | Redraws/s today → proposed | Still % today → proposed | Outline moves/min today → proposed |
|---|---|---|---|
| idle (0-290 s) | 6.2 → 10.2 | 90 → 83 | 10.8 → 0.0 |
| sleepy (awake, 2-600 s) | 10.6 → 12.1 | 82 → 80 | 0.0 → 0.0 |
| asleep (BotNext only, 10-600 s) | – → 1.7 | – → 97 | – → 0.0 |
| working (2-600 s) | 9.9 → 12.6 | 83 → 79 | 0.0 → 0.0 |
| waiting, first 20 s | 16.9 → 33.4 | 72 → 44 | 519.0 → 1140.0 |
| waiting (0-600 s, real phases) | 16.1 → 15.8 | 73 → 74 | 482.7 → 131.6 |
| done, first 6 s | 12.7 → 24.7 | 79 → 59 | 26.7 → 380.0 |
| done (6-600 s) | 6.3 → 9.5 | 89 → 84 | 10.4 → 0.0 |
| error (2-600 s) | 13.8 → 15.8 | 77 → 74 | 36.6 → 0.0 |

How to read it:
- **The eyes do more:** +2 to +4 redraws/s in idle, working, done and error. That comes from the micro-saccades and the breath. At about 8 ms per redraw it is +2 to +3 points of core 0, in eye areas only, with steps of 3 px or less.
- **The outline does much less outside deliberate moments.** Glance leans no longer move it (idle 10.8 → 0, done 10.4 → 0, error 36.6 → 0 per minute). Waiting falls from 483 to 132 moves/min over 10 min, because phase 3 replaces the hops with nudges.
- **Where the outline moves more, it does so on purpose.** The first 20 s of waiting run about twice the outline moves of today (the escalation phase), and the first 6 s of done include the reward hop and the ring settle.
- **Asleep is nearly free:** 1.7 redraws/s, 97 % still, against 10.6 redraws/s for today's sleepy, which never sleeps.
- **The tilt now mirrors:** a left glance gives exactly the mirror image of a right glance (host check). Today the left-glance eye is off by 0.125 body radii (about 25 px).

**What BotNext changes beyond the review text:**
- **[P10] The lid is a clip line from the top**, not the exponent tweak in the review, which still passed through a dot. On the device this means zeroing the eye canvas's alpha rows above the lid line in `eye_raster`, which costs less than drawing them. The eye keeps its open shape and closes to a small crescent at the bottom.
- **[P8] Error fix:** the Clawd mark is left out of the label canvas in error, and the text stays below the triangle as a name plate. This was the cleanest of the three options: no geometry change, no clash with the eyes looking down, and zero cost (the label re-renders at a mood change anyway).
- **[P4] Double hop timing:** the second hop chains at 0.92 × the hop length, after the landing. The review's +0.55 s would cut the 1 s hop mid-air.
- **[P6] Swap timing:** the eye shape and the badge also wait for the ring swap, so all three change in the same lid closure.
