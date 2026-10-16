#!/bin/bash
set -eu
D="$(cd "$(dirname "$0")" && pwd)"
C="$D/../../components/bot"
P="$D/../../components/pomo"
W="$D/../../components/weather"
H="$D/../../components/ember_host"
M="$D/../../components/dim"
G="$D/../../components/cinder_cfg"
V="$D/../../components/provision"
N="$D/../../components/net_policy"
O="$D/../../components/ota_policy"
PR="$D/../../components/panel_req"
PG="$D/../../components/pages"
LS="$D/../../components/link_state"
CP="$D/../../components/caps"
RT="$D/../../components/rotation"
DM="$D/../../components/demo"
RS="$D/../../components/refr_stats"
U="$D/../../components/usage"
IDF="${IDF_PATH:?source the ESP-IDF export script first (IDF_PATH)}"
J="$IDF/components/json/cJSON"
OUT_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cinder_test.XXXXXX")"
trap 'rm -rf "$OUT_DIR"' EXIT
OUT="$OUT_DIR/t"
ECL="$D/../../main/ember_client.c"
OTA_CALLS=$(grep -c 'ota_client_checkin_done(' "$ECL" || true)
OTA_BAD=$(grep 'ota_client_checkin_done(' "$ECL" | grep -vc 'ota_client_checkin_done(step\.ota_status, ' || true)
if [ "$OTA_CALLS" -eq 0 ] || [ "$OTA_BAD" -ne 0 ]; then
    echo "run.sh: FAIL every ota_client_checkin_done in ember_client.c must pass step.ota_status ($OTA_CALLS calls, $OTA_BAD others)" >&2
    grep -n 'ota_client_checkin_done(' "$ECL" >&2 || true
    exit 1
fi
echo "ota call sites: $OTA_CALLS pass step.ota_status"
OCL="$D/../../main/ota_client.c"
body() { awk -v f="$2" '$0 ~ "^(static )?[a-z_][a-z_0-9 ]*[ *]" f "\\(" && !/;$/ {on=1} on {print} on && /^}/ {exit}' "$1"; }
WIRING=""
body "$OCL" dl_write | grep -q 'ota_piece_write(&io, &d->yield_ms, ' || WIRING="$WIRING dl_write-must-call-ota_piece_write"
body "$OCL" dl_write | grep -q 'esp_ota_write(' && WIRING="$WIRING dl_write-must-not-call-esp_ota_write"
[ "$(grep -c 'esp_ota_write(' "$OCL")" -eq "$(body "$OCL" io_write | grep -c 'esp_ota_write(')" ] ||
    WIRING="$WIRING esp_ota_write-only-in-io_write"
for CALL in esp_ota_end esp_ota_set_boot_partition; do
    grep -B3 "= $CALL(" "$OCL" | grep -q 'ota_yield(&YIELD_IO);' || WIRING="$WIRING ota_yield-before-$CALL"
done
MAINC="$D/../../main/main.c"
for ARG in true false; do
    body "$MAINC" ota_frame | grep -q "bot_view_rim_hold($ARG);" || WIRING="$WIRING ota_frame-bot_view_rim_hold($ARG)"
done
if [ -n "$WIRING" ]; then
    echo "run.sh: FAIL OTA watchdog wiring:$WIRING" >&2
    exit 1
fi
echo "ota yield wiring: dl_write, esp_ota_end, esp_ota_set_boot_partition, rim hold"
BSP="$D/../../components/bsp_knob/bsp_knob_15_md50et.c"
body "$BSP" rotation_step | grep -q 'return r == KR_APPLIED;' || WIRING="$WIRING rotation_step-returns-KR_APPLIED"
body "$BSP" bsp_knob_15_md50et_rotation_frame |
    grep -q 'if (rotation_step() && s_disp) lv_obj_invalidate(lv_display_get_screen_active(s_disp));' ||
    WIRING="$WIRING rotation_frame-invalidates-on-applied"
if [ -n "$WIRING" ]; then
    echo "run.sh: FAIL rotation redraw wiring:$WIRING" >&2
    exit 1
fi
echo "rotation redraw wiring: lv_obj_invalidate on KR_APPLIED"
REFR="$(body "$MAINC" refr_cb)"
for EV in REFR_START RENDER_START REFR_READY; do
    grep -qxF "    lv_display_add_event_cb(handles.disp, refr_cb, LV_EVENT_$EV, NULL);" "$MAINC" || WIRING="$WIRING refr_cb-on-$EV"
done
echo "$REFR" | grep -qxF '        refr_stats_begin(&s_refr, s_frame_kind);' || WIRING="$WIRING refr_stats_begin-at-REFR_START"
echo "$REFR" | grep -qxF '        refr_stats_render(&s_refr, s_refr_areas, disp->inv_area_joined, n);' || WIRING="$WIRING refr_stats_render-at-RENDER_START"
echo "$REFR" | grep -qxF '    bool drew = refr_stats_end(&s_refr, d);' &&
    echo "$REFR" | grep -qxF '    if (drew) ota_client_note_frame();' || WIRING="$WIRING refr_stats_end-gates-ota_client_note_frame"
in_refr_lock() { awk -v want="$1" '/taskENTER_CRITICAL\(&s_refr_mux\);/ {l=1; e++} /taskEXIT_CRITICAL\(&s_refr_mux\);/ {l=0}
    /refr_stats_(end|take)\(/ { if (!l) bad=1 }
    /s_refr_(count|total_us|max_us)/ { if (l) n++; else bad=1 }
    index($0, want) { if (l) w++ } END {print n + 0; exit !(e == 1 && !l && w == 1 && !bad)}'; }
RN=$(echo "$REFR" | in_refr_lock 'bool drew = refr_stats_end(&s_refr, d);') &&
    SN=$(body "$MAINC" stats_task | in_refr_lock 'int64_t win_us = refr_stats_take(&s_refr, esp_timer_get_time(), kc, &kt);') ||
    WIRING="$WIRING refr-end-and-take-inside-the-s_refr_mux-section"
[ -n "$WIRING" ] || [ "$(grep -E 's_refr_(count|total_us|max_us)' "$MAINC" | grep -vcE '^static [a-z0-9_]+ s_refr_[a-z_]+(, s_refr_[a-z_]+)*;$')" -eq $((RN + SN)) ] ||
    WIRING="$WIRING refr-counters-only-in-the-refr_cb-and-stats_task-sections"
STATS="$(body "$MAINC" stats_task)"
echo "$STATS" | grep -qxF '    int failed_logged = 0, refused_logged = 0;' &&
    echo "$STATS" | grep -qxF '        if (failed > failed_logged) ESP_LOGE(TAG, "weather moments: %d starts since boot without the PSRAM arena or the bake task", failed);' &&
    echo "$STATS" | grep -A1 -F 'if (failed > failed_logged) ESP_LOGE(' | tail -1 | grep -qxF '        failed_logged = failed;' &&
    [ "$(grep -c 'failed_logged' "$MAINC")" -eq 3 ] && [ "$(grep -c 'weather moments: %d starts since boot without' "$MAINC")" -eq 1 ] ||
    WIRING="$WIRING stats-logs-moment-start-failures-only-when-the-count-grows"
echo "$STATS" | grep -qxF '        if (refused > refused_logged) ESP_LOGE(TAG, "weather moments: %d clouds since boot refused by wx_cloud_shape, drawn blank", refused);' &&
    echo "$STATS" | grep -A1 -F 'if (refused > refused_logged) ESP_LOGE(' | tail -1 | grep -qxF '        refused_logged = refused;' &&
    [ "$(grep -c 'refused_logged' "$MAINC")" -eq 3 ] &&
    body "$D/../../main/moment_view.c" moment_bake_task | grep -qxF '        atomic_fetch_add(&s_refused, wxm_paint(&s_m, &s_l, s_arena, bake_yield));' &&
    grep -qxF 'static atomic_int s_refused;' "$D/../../main/moment_view.c" && [ "$(grep -c 's_refused' "$D/../../main/moment_view.c")" -eq 3 ] &&
    body "$D/../../main/moment_view.c" moment_view_stats | grep -qxF '    *refused = atomic_load(&s_refused);' ||
    WIRING="$WIRING stats-logs-refused-moment-clouds-only-when-the-count-grows"
WMP="$D/../../components/weather/wx_moment_paint.c"
[ "$(grep -c 'cloud(&im' "$WMP")" -gt 0 ] && [ "$(grep -c 'cloud(&im' "$WMP")" -eq "$(grep -cxE ' +refused \+= cloud\(&im[^;]*\);' "$WMP")" ] &&
    grep -qxF 'static __attribute__((warn_unused_result)) int cloud(img_t *im, const wx_cloud_t *c, int tone)' "$WMP" &&
    CLOUD="$(awk '/^static __attribute__\(\(warn_unused_result\)\) int cloud\(/ {on=1} on {print} on && /^}/ {exit}' "$WMP")" &&
    echo "$CLOUD" | grep -qxF '    bool ok = wx_cloud_shape(c, &sh);' && echo "$CLOUD" | tail -2 | head -1 | grep -qxF '    return !ok;' &&
    body "$WMP" wxm_paint | tail -2 | head -1 | grep -qxF '    return refused;' || WIRING="$WIRING every-moment-cloud-counted-into-refused"
if [ -n "$WIRING" ]; then
    echo "run.sh: FAIL refresh stats wiring:$WIRING" >&2
    exit 1
fi
echo "refresh stats wiring: begin at REFR_START, areas at RENDER_START, end, take and the counters inside one s_refr_mux section; moment start failures and refused clouds logged when the count grows"
DIAGC="$D/../../main/diag.c"
APP="$(body "$MAINC" app_main)"
echo "$APP" | grep -n -e '    refr_stats_init(&s_refr, esp_timer_get_time());' -e '    diag_attach_refr(&s_refr, &s_refr_mux);' \
    -e '    ember_client_start();' -e 'lv_display_add_event_cb(handles.disp, refr_cb' | cut -d: -f2- | sed 's/(.*//' | uniq |
    tr -d ' \n' | grep -qx 'refr_stats_initdiag_attach_refrember_client_startlv_display_add_event_cb' ||
    WIRING="$WIRING refr-init-and-attach-before-ember_client_start-and-refr_cb"
[ "$(grep -c 'refr_stats_init(' "$MAINC")" -eq 1 ] || WIRING="$WIRING refr_stats_init-once"
WS="$(body "$DIAGC" window_start)"
REFR_NOW='/^    if / {i = ($0 ~ /^    if \(s_refr\) \{$/); x = ($0 ~ /^    if \(!s_refr\) \{$/)}
    /^    \} else \{$/ {x = i; i = 0} /^    \}$/ {x = 0; i = 0}
    /(^|[^A-Za-z_0-9])now *=[^=]/ {a++; if (!l && !x && (seen || !/^    (int64_t )?now = /)) o++}'
echo "$WS" | awk '/taskENTER_CRITICAL\(s_refr_mux\);/ {l=1} /taskEXIT_CRITICAL\(s_refr_mux\);/ {l=0; t=0}
    l && /^ +now = esp_timer_get_time\(\);$/ {t=1; seen=1} l && t && /^ +refr_stats_win_reset\(s_refr, now\);$/ {r=1}
    '"$REFR_NOW"'
    /^    s_win_start_us = now;$/ {s=1} END {exit !(r && s && a == 2 && !o)}' &&
    [ "$(echo "$WS" | grep -c 's_win_start_us = ')" -eq 1 ] ||
    WIRING="$WIRING window_start-stamps-and-resets-the-refr-window-in-one-s_refr_mux-section"
SAMPLE="$(body "$DIAGC" diag_sample)"
echo "$SAMPLE" | awk '/taskENTER_CRITICAL\(s_refr_mux\);/ {l=1} /taskEXIT_CRITICAL\(s_refr_mux\);/ {l=0; t=0}
    l && /^ +now = esp_timer_get_time\(\);$/ {t=1; seen=1} l && t && /^ +rw = refr_stats_win_read\(s_refr\);$/ {r=1}
    '"$REFR_NOW"'
    /^    int64_t period_us = now - s_win_start_us;$/ && r {p=1} END {exit !(p && a == 2 && !o)}' ||
    WIRING="$WIRING diag_sample-reads-the-refr-window-at-now-in-one-s_refr_mux-section"
echo "$SAMPLE" | awk '/^    if \(level == KS_DIAG_FULL\) \{$/ {f=1; d=0}
    { line=$0; o=gsub(/\{/, "{", line); cl=gsub(/\}/, "}", line) }
    f && d == 1 && /^ *\} *else/ {f=0}
    /st->(has_inv|inv_areas|inv_px) = / { if (f) n++; else bad=1 }
    f { d += o - cl; if (d == 0) f=0 }
    END {exit !(n == 3 && !bad)}' || WIRING="$WIRING inv-fields-set-only-inside-the-full-block"
[ "$(grep -c 'has_inv' "$DIAGC")" -eq 1 ] || WIRING="$WIRING has_inv-set-once"
if [ -n "$WIRING" ]; then
    echo "run.sh: FAIL checkin area window wiring:$WIRING" >&2
    exit 1
fi
echo "checkin area window wiring: attached before the ember task; window stamp with reset and sample time with read, each in one s_refr_mux section; inv fields only inside the full block"
WXC="$D/../../main/weather_client.c"
body "$WXC" weather_client_get | awk '/xSemaphoreTake\(s_lock/ {lock=1} /xSemaphoreGive\(s_lock/ {lock=0} /esp_timer_get_time\(/ {t++; if (!lock) bad=1} END {exit !(t == 1 && !bad)}' ||
    WIRING="$WIRING weather_client_get-reads-the-timer-under-the-lock"
body "$WXC" poll_task | grep -qxF '    static EXT_RAM_BSS_ATTR wx_obs_t obs;' || WIRING="$WIRING legacy-weather-obs-in-PSRAM"
if [ -n "$WIRING" ]; then
    echo "run.sh: FAIL weather wiring:$WIRING" >&2
    exit 1
fi
echo "weather wiring: weather_client_get reads the timer under the lock, the legacy obs in PSRAM"
grep -qF 'static void *json_malloc(size_t n) { return heap_caps_malloc_prefer(n, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT); }' "$MAINC" ||
    WIRING="$WIRING json_malloc-prefers-PSRAM"
body "$MAINC" app_main | sed 1,2d | grep -vE '^[[:space:]]*(ESP_LOG[A-Z]\(|$)' | grep -m1 -E '[A-Za-z_][A-Za-z_0-9]*\(' |
    grep -qF 'cJSON_InitHooks(&(cJSON_Hooks){.malloc_fn = json_malloc, .free_fn = free});' ||
    WIRING="$WIRING app_main-sets-the-cJSON-hooks-before-any-other-call"
if [ -n "$WIRING" ]; then
    echo "run.sh: FAIL cJSON hook wiring:$WIRING" >&2
    exit 1
fi
echo "cJSON hook wiring: app_main sets PSRAM-first hooks before anything parses"
TFC="$D/../../main/temp_face.c"
body "$TFC" temp_face_update | grep -qF 'if (wx_shift_tick(&s_shift, &WX_SHIFT_SQUARE, dt)) shift_root();' || WIRING="$WIRING update-steps-the-burn-in-shift"
body "$TFC" shift_root | grep -qF 'lv_obj_set_style_translate_y(s_root, dy, 0);' || WIRING="$WIRING shift-moves-the-root"
[ "$(grep -c 'wx_bake(' "$TFC")" -eq 1 ] && body "$TFC" bake_task | grep -qF 'wx_bake(t, s_io.kind, &s_buf, &s_io.out);' ||
    WIRING="$WIRING wx_bake-only-in-bake_task"
body "$TFC" bake_task | awk '/s_yield_at = t0;/ {y = NR} /wx_bake\(/ {p = NR} END {exit !(y && p > y)}' &&
    body "$TFC" temp_face_create | awk '/s_buf.yield = bake_yield;/ {y = NR} /xTaskCreatePinnedToCoreWithCaps\(bake_task, / {c = NR} END {exit !(y && c > y)}' &&
    body "$TFC" bake_yield | grep -qxF '    if (esp_timer_get_time() - s_yield_at < BAKE_YIELD_US) return;' &&
    body "$TFC" bake_yield | awk '/vTaskDelay\(1\);/ {d = NR} /s_yield_at = esp_timer_get_time\(\);/ {s = NR} END {exit !(d && s > d)}' &&
    WYUS=$(grep -o '^#define BAKE_YIELD_US [0-9]*' "$TFC" | awk '{print $3}') && [ "${WYUS:-0}" -ge 20000 ] && [ "$WYUS" -le 50000 ] ||
    WIRING="$WIRING wxbake-yields-a-tick-every-20-50-ms"
WXP="$W/wx_paint.c"
YROWS=$(awk '/^[a-z].*\(/ {sig = $0} /^[ ]+for \(int y = / {want = (sig ~ /wx_img_t \*im[,)]/) ? "row(im);" : (sig ~ /^static void annulus\(/) ? "row(&face);" : (sig ~ /^static void paint_(sock|sun_rays)\(/) ? "if (yield) yield();" : ""; if (want != "") {n++; getline nx; gsub(/^ +/, "", nx); if (nx != want) bad++}} END {print n + 0; exit !(n == 23 && !bad)}' "$WXP") ||
    WIRING="$WIRING every-image-ring-sock-and-ray-row-loop-starts-with-the-yield-hook-23-loops"
YIMGS=$(awk '/^(static )?(void|wx_box_t) (bake_[a-z]+|wx_bake|layer)\(/ && !/;$/ {on = 1} on && /^}/ {on = 0} on && /wx_img_t [a-z]+ = \{/ {s = $0; while (s !~ /\};/) {getline l; s = s l} n++; if (s !~ /\.yield = (b->)?yield[,}]/) bad++} END {print n + 0; exit !(n == 14 && !bad)}' "$WXP") ||
    WIRING="$WIRING every-bake-image-carries-the-yield-hook-14-images"
body "$WXP" wx_bake | grep -qxF '    b->ring->yield = b->yield;' || WIRING="$WIRING wx_bake-hands-the-ring-the-hook"
grep -qE 'wx_paint_(clear|ring|ring_rain|rays|stars|thermo|hero|clouds|umbrella|sheet|ramp|drip|splash|bolt|ring_days|day_icon|day_row|ring_sun|sun_scene|sun_rays|sun|ring_air|air_scene|petal)\(' "$TFC" &&
    WIRING="$WIRING no-bake-pass-on-the-LVGL-task"
body "$TFC" bake_task | grep -qF 'const wx_temp_t *t = wx_job_take(&s_job);' || WIRING="$WIRING bake_task-takes-through-wx_job"
body "$TFC" bake_task | grep -qF 'if (!t) continue;' || WIRING="$WIRING bake_task-skips-an-empty-take"
body "$TFC" bake_task | grep -qF 'wx_job_done(&s_job, ' || WIRING="$WIRING bake_task-publishes-through-wx_job"
grep -qE 'atomic_|JOB_(IDLE|BUSY|DONE)' "$TFC" && WIRING="$WIRING job-state-only-in-wx_job"
grep -qF 'static wx_job_t s_job;' "$TFC" || WIRING="$WIRING job-state-in-internal-RAM"
body "$TFC" temp_face_show | grep -qF 'act(wx_job_show(&s_job, on));' || WIRING="$WIRING show-through-wx_job"
body "$TFC" temp_face_hold | grep -qF 'act(wx_job_hold(&s_job, hold));' || WIRING="$WIRING hold-through-wx_job"
body "$TFC" temp_face_update | grep -qF 'int a = wx_job_step(&s_job, &t, dt);' || WIRING="$WIRING update-steps-wx_job"
body "$TFC" temp_face_update | grep -qF 'if (!(a & WX_DO_PAINT)) return;' || WIRING="$WIRING update-paints-only-on-WX_DO_PAINT"
body "$TFC" temp_face_update | grep -qF 'wx_temp_face(obs, s_job.baked ? &s_io.key : NULL, s_faces.pick, &t);' ||
    WIRING="$WIRING update-keeps-the-shown-band-on-the-picked-face"
body "$TFC" temp_face_update | grep -qxF '        s_face = t.face;' || WIRING="$WIRING update-records-the-shown-face"
body "$TFC" temp_face_show | grep -qF 'wx_faces_open(&s_faces);' && body "$TFC" temp_face_show | grep -qxF '        s_face = -1;' ||
    WIRING="$WIRING show-opens-on-the-auto-face-and-forgets-the-last-face"
body "$TFC" temp_face_update | grep -qxF '    if (s_job.visible) {' || WIRING="$WIRING update-resolves-the-face-without-buffers-too"
body "$TFC" temp_face_turn | grep -qF 'if (wx_faces_turn(&s_faces, wx_job_face(&s_job), s_face, detents)) wx_job_cut(&s_job);' &&
    body "$TFC" temp_face_pick | grep -qF 'if (face != WXF_AUTO && wx_job_face(&s_job) != face) wx_job_cut(&s_job);' &&
    [ "$(grep -c 'wx_job_cut(' "$TFC")" -eq 2 ] || WIRING="$WIRING turn-and-demo-pick-cut-only-on-a-new-face"
body "$TFC" act | awk '/if \(a & WX_DO_ARRIVE\) \{/ {on = 1} on && /s_t = 0;/ {t = 1} on && /s_tick = -1;/ {k = 1} END {exit !(t && k)}' ||
    WIRING="$WIRING act-replays-the-arrival"
body "$MAINC" page_weather_input | grep -qF 'if (in->detents) temp_face_turn(in->detents);' ||
    WIRING="$WIRING weather-page-turns-switch-faces"
body "$TFC" temp_face_update | grep -qF 'paint(&t, obs, offline, s_job.opa);' || WIRING="$WIRING update-paints-at-the-job-opa"
for ACT in 'if (a & WX_DO_RELEASE) release();' 'wx_job_acquired(&s_job, ok);' 'if (a & WX_DO_DETACH) detach();' \
    'if (a & WX_DO_BAKE) xTaskNotifyGive(s_task);' 'if (a & WX_DO_APPLY) apply(&s_io.key, &s_io.out, s_io.kind);'; do
    body "$TFC" act | grep -qF "$ACT" || WIRING="$WIRING act:$ACT"
done
[ "$(grep -c 'xTaskNotifyGive(' "$TFC")" -eq 1 ] || WIRING="$WIRING notify-only-in-act"
body "$TFC" detach | grep -qF 'show_obj(s_root, false);' || WIRING="$WIRING detach-hides-the-face"
for CV in s_bg s_hero 's_cl[i]' 's_strip[s][0]' 's_strip[s][1]' 's_top[s]' 's_bottom[s]' 's_cover[s]' 's_drip[i]' \
    's_splash[n]' s_bolt; do
    body "$TFC" detach | grep -qF "lv_canvas_set_buffer($CV, s_none, " || WIRING="$WIRING detach-$CV"
done
RAIN_CV='lv_canvas_set_buffer\(s_(strip|top|bottom|cover|splash|bolt|drip)'
[ "$(grep -cE "$RAIN_CV" "$TFC")" -eq $(( $(body "$TFC" detach | grep -cE "$RAIN_CV") + $(body "$TFC" rain_attach | grep -cE "$RAIN_CV") +
    $(body "$TFC" set_rain | grep -cE "$RAIN_CV") )) ] && ! body "$TFC" detach | grep -E "$RAIN_CV" | grep -vqF 's_none, ' ||
    WIRING="$WIRING rain-layers-attach-only-in-rain_attach-and-set_rain"
body "$TFC" apply | grep -qF 'if (t->rain.on) rain_attach(&t->rain);' || WIRING="$WIRING rain-attaches-on-a-scene-apply"
body "$TFC" apply | grep -qF 'if (t->cast.on) cast_attach(&t->cast);' || WIRING="$WIRING forecast-attaches-on-a-scene-apply"
for CV in 's_cloud[s]' 's_cstrip[s][0]' 's_cstrip[s][1]' 's_ctop[s]' 's_cbottom[s]'; do
    body "$TFC" detach | grep -qF "lv_canvas_set_buffer($CV, s_none, " || WIRING="$WIRING detach-$CV"
done
CAST_CV='lv_canvas_set_buffer\(s_(cloud|cstrip|ctop|cbottom)'
[ "$(grep -cE "$CAST_CV" "$TFC")" -eq $(( $(body "$TFC" detach | grep -cE "$CAST_CV") + $(body "$TFC" cast_attach | grep -cE "$CAST_CV") )) ] &&
    body "$TFC" cast_attach | grep -qF 'uint8_t *m = s_buf.scene;' || WIRING="$WIRING forecast-layers-attach-only-in-cast_attach-from-the-scene-buffer"
body "$TFC" paint | grep -qF 'const wx_cast_t *c = wx_cast_same_scene(&t->cast, &s_io.key.cast) ? &t->cast : &s_io.key.cast;' ||
    WIRING="$WIRING paint-keeps-the-baked-forecast-while-fading-out"
body "$TFC" paint | grep -qF 'const char *unit = wx_cast_value(c, wx_count_step(wx_cast_count_n(c), ta), txt, sizeof txt, &rgb);' &&
    body "$TFC" paint | grep -qF 'set_unit_text(unit);' || WIRING="$WIRING forecast-value-and-unit-from-wx_cast_value"
body "$TFC" set_cast | grep -qF 'bool rain = fo == 255 && gate > 0 && !c->dry;' || WIRING="$WIRING forecast-sheets-hidden-while-fading-and-when-dry"
body "$TFC" set_cast | grep -qF 'if (cloud) lv_obj_invalidate(s_cbox[s]);' || WIRING="$WIRING forecast-cloud-and-sheet-in-one-area"
body "$TFC" set_label | grep -qF 'lv_text_get_size(&sub, c->sub, &lv_font_montserrat_30, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_RECOLOR);' &&
    body "$TFC" set_label | grep -qF 's_shown.label_clear = wx_cast_label_place(c->label_deg, sz.x, sz.y, wx_cast_sub_box(sub.x, sub.y), &x, &y);' &&
    body "$TFC" set_label | grep -qF 'bool on = c->has_count && opa > 0 && s_shown.label_clear;' || WIRING="$WIRING forecast-label-clears-the-sub-line"
body "$TFC" apply | grep -qF 'if (t->wind.on) wind_attach(&t->wind);' || WIRING="$WIRING wind-attaches-on-a-scene-apply"
for CV in s_wsock s_wdark 's_wstreak[r]' 's_wleaf[i]'; do
    body "$TFC" detach | grep -qF "lv_canvas_set_buffer($CV, s_none, " || WIRING="$WIRING detach-$CV"
done
WIND_CV='lv_canvas_set_buffer\(s_w(sock|dark|streak|leaf)'
[ "$(grep -cE "$WIND_CV" "$TFC")" -eq $(( $(body "$TFC" detach | grep -cE "$WIND_CV") + $(body "$TFC" wind_attach | grep -cE "$WIND_CV") +
    $(body "$TFC" set_wind | grep -cE "$WIND_CV") )) ] && body "$TFC" wind_attach | grep -qF 'uint8_t *m = s_buf.scene;' &&
    [ "$(body "$TFC" set_wind | grep -cF 's_buf.scene + s_wlay.')" -eq 3 ] || WIRING="$WIRING wind-layers-attach-only-from-the-scene-buffer"
body "$TFC" paint | grep -qF 'const wx_wind_t *w = wx_wind_same_scene(&t->wind, &s_io.key.wind) ? &t->wind : &s_io.key.wind;' ||
    WIRING="$WIRING paint-keeps-the-baked-wind-while-fading-out"
body "$TFC" set_wind | grep -qF 'wx_wind_frame(w->level, w->have && fo == 255 ? (int)floor(s_t * WX_WIND_FPS) : -1, &f);' ||
    WIRING="$WIRING wind-moves-only-with-data-at-full-opacity"
body "$TFC" set_wind | grep -qF 'if (dirty) lv_obj_invalidate(s_wbox);' && [ "$(grep -c 'lv_obj_invalidate(s_wbox);' "$TFC")" -eq 2 ] ||
    WIRING="$WIRING wind-redraws-one-box"
body "$TFC" apply | grep -qF 'if (t->days.on) days_attach(&t->days);' || WIRING="$WIRING days-attach-on-a-scene-apply"
body "$TFC" detach | grep -qF 'lv_canvas_set_buffer(s_dicon[i], s_none, ' || WIRING="$WIRING detach-s_dicon[i]"
[ "$(grep -c 'lv_canvas_set_buffer(s_dicon' "$TFC")" -eq 2 ] && body "$TFC" days_attach | grep -qF 'uint8_t *m = s_buf.scene;' &&
    body "$TFC" days_attach | grep -qF 'lv_canvas_set_buffer(s_dicon[i], m + s_dlay.at[i], ' || WIRING="$WIRING days-sprites-attach-only-from-the-scene-buffer"
body "$TFC" paint | grep -qF 'const wx_days_t *d = &s_io.key.days;' || WIRING="$WIRING paint-shows-the-baked-days"
body "$TFC" face_kind | grep -qxF '    bool rows = s_days && t->days.have;' && body "$TFC" face_kind | grep -qxF '    show_obj(s_value, !rows);' &&
    [ "$(grep -c 'show_obj(s_value, ' "$TFC")" -eq 1 ] || WIRING="$WIRING days-hide-the-value-under-the-rows"
body "$TFC" face_kind | grep -qxF '    lv_obj_align(s_sub, LV_ALIGN_TOP_MID, 0, rows ? WX_DAYS_SUB_Y : SUB_Y);' || WIRING="$WIRING days-sub-line-under-the-rows"
[ "$(body "$TFC" apply | grep -A2 -xF '    if (t->cast.on || t->days.on || t->air.on) {')" = "$(printf '%s\n' \
    '    if (t->cast.on || t->days.on || t->air.on) {' '        show_obj(s_lo, false);' '        show_obj(s_hi, false);')" ] ||
    WIRING="$WIRING days-and-air-hide-the-gauge-end-labels"
body "$TFC" set_days | grep -qxF '    wx_days_frame(d, ta, &f);' && body "$TFC" set_days | grep -qxF '            lv_obj_set_y(s_drow[i], wx_days_row_box(&s_dlay, i, dy).y0);' &&
    body "$TFC" days_attach | grep -qxF '        wx_box_t rb = wx_days_row_box(&s_dlay, i, 0);' &&
    body "$TFC" days_attach | grep -qxF '        lv_obj_set_pos(s_drow[i], rb.x0, rb.y0);' &&
    body "$TFC" days_attach | grep -qxF '        lv_obj_set_size(s_drow[i], w, rb.y1 - rb.y0);' || WIRING="$WIRING days-rows-from-the-host-model"
body "$TFC" set_days | grep -qF 'int opa = f.opa[i] * fo / 255, dy = f.dy[i], lo = f.lo[i], hi = f.hi[i];' || WIRING="$WIRING days-rows-fade-with-the-face"
body "$TFC" set_days | grep -qF 'if (dirty) lv_obj_invalidate(s_dbox);' && [ "$(grep -c 'lv_obj_invalidate(s_dbox);' "$TFC")" -eq 2 ] ||
    WIRING="$WIRING days-redraw-one-box"
body "$TFC" apply | grep -qxF '        if (t->sun.on) sun_attach();' || WIRING="$WIRING sun-attaches-on-a-scene-apply"
for CV in s_srays s_ssun; do
    body "$TFC" detach | grep -qF "lv_canvas_set_buffer($CV, s_none, " || WIRING="$WIRING detach-$CV"
done
SUN_CV='lv_canvas_set_buffer\(s_s(rays|sun), '
[ "$(grep -cE "$SUN_CV" "$TFC")" -eq 4 ] && [ "$(body "$TFC" sun_attach | grep -cE "$SUN_CV")" -eq 2 ] &&
    body "$TFC" sun_attach | grep -qxF '    uint8_t *m = s_buf.scene;' || WIRING="$WIRING sun-sprites-attach-only-from-the-scene-buffer"
body "$TFC" sun_create | grep -qxF '    s_ssun = canvas(s_sbox, LV_COLOR_FORMAT_RGB565A8);' &&
    body "$TFC" sun_create | awk '/s_srays = a8\(s_root, WX_SUN_GOLD_RGB\);/ {r = NR} /s_sbox = box_obj\(s_root\);/ {b = NR} END {exit !(r && b && r < b)}' ||
    WIRING="$WIRING sun-clipped-by-its-box-over-the-rays"
body "$TFC" face_kind | awk '/^    if \(!s_sun\) \{/ {on = 1} on && /show_obj\(s_srays, false\);/ {r = 1} on && /show_obj\(s_sbox, false\);/ {b = 1} on && /^    \}/ {on = 0} END {exit !(r && b)}' ||
    WIRING="$WIRING sun-layers-hidden-on-other-faces"
body "$TFC" face_kind | grep -qxF '    show_obj(s_thermo, !s_rain && !s_cast && !s_wind && !s_days && !s_sun && !s_air);' ||
    WIRING="$WIRING sun-and-air-hide-the-thermometer"
body "$TFC" paint | grep -qF 'const wx_sun_t *sn = wx_sun_same_scene(&t->sun, &s_io.key.sun) ? &t->sun : &s_io.key.sun;' ||
    WIRING="$WIRING paint-keeps-the-baked-sun-while-fading-out"
body "$TFC" paint | grep -qxF '        const char *unit = wx_sun_value(sn, wx_count_step(wx_sun_count_n(sn), ta), txt, sizeof txt);' &&
    body "$TFC" paint | grep -qxF '        set_value(txt, unit ? WX_SUN_VALUE_RGB : GREY_RGB);' || WIRING="$WIRING sun-value-from-wx_sun_value"
body "$TFC" paint | grep -qxF '        set_marker(sn->frac * (float)a, sn->have && sn->has_now);' && body "$TFC" paint | grep -qxF '        set_sun(sn, ta, fo);' ||
    WIRING="$WIRING sun-marker-and-arrival"
body "$TFC" paint | grep -qF 's_sun ? sn->sub_rgb : ' || WIRING="$WIRING sun-sub-line-colour"
body "$TFC" set_sun | grep -qxF '    wx_sun_frame(ta, &f);' && body "$TFC" set_sun | grep -qxF '        lv_obj_set_y(s_ssun, f.dy);' &&
    body "$TFC" set_sun | grep -qxF '    int rays = hero ? f.rays_opa * fo / 255 : 0;' || WIRING="$WIRING sun-frames-from-the-host-model"
body "$TFC" set_sun | grep -qxF '    bool hero = wx_sun_hero(sn), dirty = false;' && body "$TFC" set_sun | grep -qxF '    show_obj(s_sbox, hero);' &&
    body "$TFC" set_sun | grep -qxF '    show_obj(s_srays, rays > 0);' || WIRING="$WIRING sun-and-rays-shown-only-with-a-sun"
body "$TFC" set_sun | grep -qxF '        lv_obj_set_style_image_opa(s_ssun, (lv_opa_t)fo, 0);' &&
    body "$TFC" set_sun | grep -qxF '        lv_obj_set_style_image_opa(s_srays, (lv_opa_t)rays, 0);' || WIRING="$WIRING sun-and-rays-fade"
for LINE in 'lv_obj_set_pos(s_srays, r.x0, r.y0);' 'lv_obj_set_pos(s_sbox, b.x0, b.y0);' 'lv_obj_set_size(s_sbox, b.x1 - b.x0, b.y1 - b.y0);' \
    'show_obj(s_sbox, true);' 's_shown.sun_dy = s_shown.sun_opa = s_shown.rays_opa = INT_MIN;'; do
    body "$TFC" sun_attach | grep -qxF "    $LINE" || WIRING="$WIRING sun_attach:$LINE"
done
body "$TFC" paint | grep -qxF '        set_unit_text(unit);' && [ "$(body "$TFC" paint | grep -cxF '        set_unit_text(unit);')" -eq 2 ] ||
    WIRING="$WIRING sun-and-forecast-set-their-unit"
body "$TFC" face_kind | grep -qxF '    s_unit_on = s_rain || s_cast || s_wind || s_sun || s_air;' &&
    body "$TFC" face_kind | grep -qF 's_rain ? -unit / 2 : s_cast || s_wind || s_days || s_sun || s_air ? 0 : deg / 2, VALUE_Y);' ||
    WIRING="$WIRING sun-and-air-value-centred-with-the-unit"
body "$TFC" apply | grep -qxF '        strlcpy(s_lo_txt, t->sun.rise, sizeof s_lo_txt);' && body "$TFC" apply | grep -qxF '        strlcpy(s_hi_txt, t->sun.set, sizeof s_hi_txt);' ||
    WIRING="$WIRING sun-end-label-texts"
body "$TFC" temp_face_create | grep -qxF '    sun_create();' || WIRING="$WIRING sun-objects-created"
body "$TFC" set_sun | grep -qxF '    if (dirty) lv_obj_invalidate(s_sbox);' && [ "$(grep -c 'lv_obj_invalidate(s_sbox);' "$TFC")" -eq 2 ] ||
    WIRING="$WIRING sun-redraws-one-box"
body "$TFC" apply | grep -qxF '        fixed_label(s_lo, s_lo_txt, t->sun.have, WX_SUN_HZN_RGB);' &&
    body "$TFC" apply | grep -qxF '        fixed_label(s_hi, s_hi_txt, t->sun.have, WX_SUN_HZN_RGB);' || WIRING="$WIRING sun-end-labels-are-the-sun-times"
body "$TFC" apply | grep -qxF '        if (t->air.on) air_attach(&t->air);' || WIRING="$WIRING air-attaches-on-a-scene-apply"
body "$TFC" detach | grep -qF 'lv_canvas_set_buffer(s_apetal, s_none, ' || WIRING="$WIRING detach-s_apetal"
[ "$(grep -c 'lv_canvas_set_buffer(s_apetal, ' "$TFC")" -eq 2 ] &&
    body "$TFC" set_air | grep -qF 'lv_canvas_set_buffer(s_apetal, s_buf.scene + p.rot * WX_PETAL_BOX * WX_PETAL_BOX * 3, ' ||
    WIRING="$WIRING air-petal-frames-from-the-scene-buffer"
body "$TFC" air_create | grep -qxF '    s_apetal = canvas(s_abox, LV_COLOR_FORMAT_RGB565A8);' || WIRING="$WIRING air-petal-in-its-box"
body "$TFC" face_kind | grep -qxF '    if (!s_air) show_obj(s_abox, false);' && body "$TFC" air_attach | grep -qxF '    show_obj(s_abox, a->wilted);' ||
    WIRING="$WIRING air-petal-only-on-the-wilted-flower"
body "$TFC" set_air | grep -qxF '    wx_air_petal(aq->wilted && fo == 255 ? (int)floor(s_t * WX_AIR_FPS) : -1, &p);' &&
    body "$TFC" set_air | grep -qxF '            lv_obj_set_pos(s_apetal, p.x - b.x0, p.y - b.y0);' || WIRING="$WIRING air-petal-from-the-host-model"
body "$TFC" set_air | grep -qxF '    if (dirty) lv_obj_invalidate(s_abox);' && [ "$(grep -c 'lv_obj_invalidate(s_abox);' "$TFC")" -eq 1 ] ||
    WIRING="$WIRING air-redraws-one-box"
body "$TFC" paint | grep -qF 'const wx_air_t *aq = wx_air_same_scene(&t->air, &s_io.key.air) ? &t->air : &s_io.key.air;' ||
    WIRING="$WIRING paint-keeps-the-baked-air-while-fading-out"
body "$TFC" paint | grep -qxF '            set_value(txt, wx_air_rgb(v));' && body "$TFC" paint | grep -qxF '            set_unit_text("AQI");' &&
    body "$TFC" paint | grep -qxF '        set_marker(aq->frac * (float)a, aq->have);' && body "$TFC" paint | grep -qxF '        set_air(aq, fo);' ||
    WIRING="$WIRING air-value-marker-and-petal"
body "$TFC" paint | grep -qxF '        const char *word = aq->have ? wx_air_word(aq->band) : "";' && body "$TFC" paint | grep -qF 's_air ? wx_air_band_rgb(aq->band) : ' ||
    WIRING="$WIRING air-band-word-in-its-colour"
body "$TFC" rain_attach | grep -qF 'uint8_t *m = s_buf.scene;' && body "$TFC" set_rain | grep -qF 's_buf.scene + s_lay.drip[' ||
    WIRING="$WIRING rain-layers-live-in-the-scene-buffer"
body "$TFC" paint | grep -qF 'set_rain(r, sprite_opa, fo == 255 && !r->dry);' || WIRING="$WIRING paint-drives-the-rain-frames-not-when-dry"
body "$TFC" paint | grep -qF 'const wx_rain_t *r = wx_rain_same_scene(&t->rain, &s_io.key.rain) ? &t->rain : &s_io.key.rain;' ||
    WIRING="$WIRING paint-keeps-the-baked-rain-while-fading-out"
body "$TFC" paint | grep -qF 'set_unit(r->has_pop ? wx_rain_rgb(r->intensity) : GREY_RGB);' || WIRING="$WIRING paint-colours-the-unit-like-the-value"
SET_RAIN=$(cat <<'END'
static void set_rain(const wx_rain_t *r, int opa, bool shown)
{
for (int s = 0; s < WX_SHEETS; s++) show_obj(s_sheet[s], shown);
if (!shown) return;
wx_rain_frame_t f;
wx_rain_frame(r, (int)floor(s_t * WX_RAIN_FPS), &f);
wx_rain_ops_t op;
wx_rain_ops(&s_shown.rain, s_shown.rain_opa, &f, opa, &op);
s_shown.rain_opa = opa;
for (int s = 0; s < WX_SHEETS; s++) {
if (op.strip_y[s]) {
lv_obj_set_y(s_strip[s][0], op.strip_top[s][0]);
lv_obj_set_y(s_strip[s][1], op.strip_top[s][1]);
}
if (op.opa) {
lv_obj_set_style_image_opa(s_strip[s][0], (lv_opa_t)op.layer_opa, 0);
lv_obj_set_style_image_opa(s_strip[s][1], (lv_opa_t)op.layer_opa, 0);
}
}
for (int i = 0; i < WX_DRIPS; i++) {
if (op.drip_buf[i])
lv_canvas_set_buffer(s_drip[i], s_buf.scene + s_lay.drip[op.drip_pose[i]], WX_DRIP_W, WX_DRIP_H, LV_COLOR_FORMAT_A8);
if (op.drip_y[i]) lv_obj_set_y(s_drip[i], op.drip_top[i]);
if (op.drip_opa[i]) lv_obj_set_style_image_opa(s_drip[i], (lv_opa_t)op.drip_alpha[i], 0);
show_obj(s_drip[i], op.drip_on[i]);
}
for (int n = 0; n < WX_SPLASHES; n++) {
if (op.splash_y[n]) lv_obj_set_y(s_splash[n], op.splash_top[n]);
if (op.splash_opa[n]) lv_obj_set_style_image_opa(s_splash[n], (lv_opa_t)op.splash_alpha[n], 0);
show_obj(s_splash[n], op.splash_on[n]);
}
if (op.opa) lv_obj_set_style_image_opa(s_bolt, (lv_opa_t)op.layer_opa, 0);
show_obj(s_bolt, op.bolt_on);
s_shown.rain = f;
}
END
)
[ "$(body "$TFC" set_rain | sed 's/^[[:space:]]*//')" = "$SET_RAIN" ] || WIRING="$WIRING set_rain-passes-wx_rain_ops-values-verbatim"
for LINE in 'memset(&s_shown.rain, 0xFF, sizeof s_shown.rain);' 's_shown.rain_opa = -1;'; do
    body "$TFC" rain_attach | grep -qxF "    $LINE" || WIRING="$WIRING rain_attach-resets:$LINE"
done
body "$TFC" face_kind | grep -qxF '    s_shown.value_w = -1;' || WIRING="$WIRING face_kind-resets-the-text-measure"
body "$TFC" set_value | grep -qxF '        if (s_unit_on && sz.x != s_shown.value_w) {' &&
    body "$TFC" set_value | grep -qxF '            s_shown.value_w = sz.x;' || WIRING="$WIRING set_value-aligns-the-unit-on-a-width-change"
[ "$(grep -c 'show_obj(s_sheet\[s\], ' "$TFC")" -eq 2 ] && body "$TFC" face_kind | grep -qF 'show_obj(s_sheet[s], false);' ||
    WIRING="$WIRING sheets-shown-only-by-set_rain"
body "$TFC" set_rain | grep -qF 'wx_rain_frame(r, (int)floor(s_t * WX_RAIN_FPS), &f);' || WIRING="$WIRING rain-frames-at-15-fps"
body "$TFC" release | grep -qF 'detach();' && body "$TFC" release | grep -qF 'wx_job_released(&s_job);' ||
    WIRING="$WIRING release-detaches-and-tells-wx_job"
grep -qF 'lv_canvas_set_buffer(s_bg, s_buf.bg, ' "$TFC" && [ "$(grep -c 'lv_canvas_set_buffer(s_bg, s_buf.bg, ' "$TFC")" -eq 1 ] &&
    body "$TFC" apply | grep -qF 'lv_canvas_set_buffer(s_bg, s_buf.bg, ' || WIRING="$WIRING bg-attached-only-on-apply"
[ "$(grep -c 'wx_ring_blit(' "$TFC")" -eq 1 ] && body "$TFC" apply | grep -qF 'wx_ring_blit(s_buf.ring, s_buf.bg);' ||
    WIRING="$WIRING ring-blit-only-in-apply"
body "$TFC" apply | awk '/s_front \^= 1;/ {f = NR} /sprite\(s_hero, s_hero_buf\[s_front\], / {if (f) h = NR} /s_buf\.hero = s_hero_buf\[!s_front\];/ {if (h) b = 1} END {exit !b}' &&
    [ "$(grep -c 'sprite(s_hero, ' "$TFC")" -eq 1 ] && [ "$(grep -c 's_buf.hero = ' "$TFC")" -eq 3 ] ||
    WIRING="$WIRING hero-shows-the-front-and-bakes-the-back"
body "$TFC" apply | awk '/if \(kind & WX_BAKE_SCENE\) \{/ {s = NR} /\} else if \(kind & WX_BAKE_RING\) \{/ {e = NR}
    /^        for \(int i = 0; i < WX_CLOUDS_MAX; i\+\+\) \{$/ {l = NR}
    /sprite\(s_cl\[i\], on \? s_buf\.scene \+ o->cloud_at\[i\] : s_none, on \? o->cloud\[i\] : \(wx_box_t\)\{0, 0, 0, 0\}\);/ {c = NR}
    /wx_drift_start\(&s_cd, o->nclouds, o->cloud_back, esp_random\(\)\);/ {d = NR}
    /^    \}/ && s && !d {out = 1} END {exit !(s && l > s && c > l && d > c && e > d && !out)}' &&
    [ "$(grep -c 'wx_drift_start(' "$TFC")" -eq 1 ] &&
    [ "$(grep -c 'sprite(s_cl\[' "$TFC")" -eq 1 ] && [ "$(grep -c 'lv_canvas_set_buffer(s_cl\[' "$TFC")" -eq 1 ] ||
    WIRING="$WIRING a-scene-apply-shows-every-cloud-sprite-and-starts-new-drift-paths"
body "$TFC" detach | grep -qxF '    for (int i = 0; i < WX_CLOUDS_MAX; i++) lv_canvas_set_buffer(s_cl[i], s_none, 2, 2, LV_COLOR_FORMAT_RGB565A8);' &&
    body "$TFC" temp_face_create | grep -qxF '    for (int i = 0; i < WX_CLOUDS_MAX; i++) s_cl[i] = canvas(s_root, LV_COLOR_FORMAT_RGB565A8);' &&
    body "$TFC" temp_face_create | grep -qxF '    for (int i = 0; i < WX_CLOUDS_MAX; i++) lv_obj_add_flag(s_cl[i], LV_OBJ_FLAG_HIDDEN);' ||
    WIRING="$WIRING every-cloud-canvas-created-hidden-and-detached"
body "$TFC" set_sprites | grep -qxF '        for (int i = 0; i < WX_CLOUDS_MAX; i++) lv_obj_set_style_image_opa(s_cl[i], (lv_opa_t)opa, 0);' &&
    [ "$(grep -c 'lv_obj_set_style_image_opa(s_cl\[' "$TFC")" -eq 1 ] || WIRING="$WIRING every-cloud-fades-with-the-sprites"
body "$TFC" set_sprites | grep -qxF '    int i = drift ? wx_drift_step(&s_cd) : -1;' &&
    body "$TFC" set_sprites | grep -qxF '    lv_obj_set_pos(s_cl[i], s_out.cloud[i].x0 + s_cd.dx[i], s_out.cloud[i].y0 + s_cd.dy[i]);' &&
    [ "$(grep -cE 'lv_obj_set_(pos|x|y)\(s_cl\[' "$TFC")" -eq 1 ] && [ "$(grep -c 'wx_drift_step(' "$TFC")" -eq 1 ] &&
    body "$TFC" paint | grep -qxF '    set_sprites(sprite_opa, temp && s_t >= WX_ARRIVE_S);' ||
    WIRING="$WIRING drift-moves-only-the-stepped-cloud-and-only-on-the-temperature-face-after-the-arrival"
body "$TFC" temp_face_update | awk '/^    s_t \+= dt;$/ {t = NR} /^    if \(s_t >= WX_ARRIVE_S\) wx_drift_tick\(&s_cd, dt\);$/ {if (t == NR - 1) k = 1} END {exit !k}' &&
    [ "$(grep -c 'wx_drift_tick(' "$TFC")" -eq 1 ] && [ "$(grep -c '&s_cd' "$TFC")" -eq 3 ] &&
    [ "$(grep -cE '(^|[^A-Za-z_0-9])s_cd([^A-Za-z_0-9]|$)' "$TFC")" -eq 5 ] &&
    ! grep -qE '(^|[^A-Za-z_0-9])s_cd(\.[a-z_]+(\[[^]]*\])?)? *(([-+*/%&|^]|<<|>>)?=[^=]|\+\+|--)|(\+\+|--) *s_cd([^A-Za-z_0-9]|$)' "$TFC" && ! body "$TFC" set_sprites | grep -q 's_t' ||
    WIRING="$WIRING the-drift-runs-on-its-own-clock-so-a-show-that-rewinds-s_t-never-freezes-it"
grep -qxF '_Static_assert(WX_SCENE_ALIGN % LV_DRAW_BUF_ALIGN == 0, "cloud sprites start on LVGL'"'"'s draw buffer alignment");' "$TFC" ||
    WIRING="$WIRING cloud-sprite-offsets-asserted-on-LVGL-draw-buffer-alignment"
grep -qxF 'static EXT_RAM_BSS_ATTR wx_drift_t s_cd;' "$TFC" && grep -qxF 'static EXT_RAM_BSS_ATTR lv_obj_t *s_cl[WX_CLOUDS_MAX];' "$TFC" ||
    WIRING="$WIRING drift-state-in-PSRAM"
[ "$(grep -c 'heap_caps_free(' "$TFC")" -eq 1 ] && grep -qF '#define DROP(p) do { heap_caps_free(p); (p) = NULL; } while (0)' "$TFC" ||
    WIRING="$WIRING free-only-through-DROP"
[ "$(body "$TFC" acquire | grep -c 'psram(')" -eq "$(body "$TFC" release | grep -c 'DROP(')" ] || WIRING="$WIRING release-drops-every-buffer"
for ARG in true false; do
    body "$MAINC" ota_frame | grep -q "temp_face_hold($ARG);" || WIRING="$WIRING ota_frame-temp_face_hold($ARG)"
done
if [ -n "$WIRING" ]; then
    echo "run.sh: FAIL temp face wiring:$WIRING" >&2
    exit 1
fi
echo "temp face wiring: burn-in shift on the root, bake and job state through wx_job, wxbake yielding a tick every ${WYUS} us (hook in ${YROWS} row loops and ${YIMGS} bake images), detach on re-bake, OTA hold, one drifting sprite per cloud, rain, forecast, wind, days, sun and air layers, face turns"
WJC="$W/wx_job.c"
body "$WJC" park | awk '/atomic_compare_exchange_strong\(&j->state, &queued, WX_JOB_IDLE\)/ {cas = NR} /atomic_load\(/ {if (!cas) early = 1; if ($0 ~ /atomic_load\(&j->state\) == WX_JOB_RUNNING/) run = 1} /atomic_store\(/ {st = 1} END {exit !(cas && run && !early && !st)}' ||
    WIRING="$WIRING park-cancels-by-CAS-then-loads-RUNNING"
body "$WJC" wx_job_take | grep -qF 'if (!atomic_compare_exchange_strong(&j->state, &queued, WX_JOB_RUNNING)) return NULL;' &&
    ! body "$WJC" wx_job_take | grep -qE 'atomic_(load|store)\(' || WIRING="$WIRING take-claims-by-CAS-only"
if [ -n "$WIRING" ]; then
    echo "run.sh: FAIL wx_job CAS wiring:$WIRING" >&2
    exit 1
fi
echo "wx_job CAS wiring: park cancels by CAS then loads RUNNING, take claims by CAS only"
BVC="$D/../../main/bot_view.c"
RSC="$C/rim_slots.c"
body "$BVC" rim_task | grep -qxF '        rim_slots_run(&s_slots, rim_raster_cb, rim_yield, &work);' || WIRING="$WIRING rim_task-runs-rim_slots_run"
body "$RSC" rim_slots_run | grep -qF 'if (!rim_job_take(&s->job, &gen, &shape, &tone)) return;' || WIRING="$WIRING rim_task-skips-a-held-take"
body "$RSC" rim_slots_run | grep -qF 'if (!rim_job_live(&s->job, gen) || !atomic_load(&s->vars)) break;' || WIRING="$WIRING rim_task-stops-a-dead-job"
if [ -n "$WIRING" ]; then
    echo "run.sh: FAIL rim wiring:$WIRING" >&2
    exit 1
fi
echo "rim wiring: rim_task skips a held take and stops a dead job"
body "$BVC" bot_view_update | grep -qF 'int dy = bot_rim_dy(p, s_r, HOP_SCALE);' &&
    ! body "$BVC" bot_view_update | grep -q 'offset_y \* HOP_SCALE)) & ~1' || WIRING="$WIRING outline-dy-from-bot_rim_dy"
body "$BVC" label_draw | grep -qF 'bool marked = bot_label_mark(s_rim_mood);' &&
    body "$BVC" label_draw | grep -qF 'const uint8_t *mark = marked ? tool_mark(' &&
    body "$BVC" label_draw | grep -qF 'bool icon = marked && (' || WIRING="$WIRING label-icon-from-bot_label_mark"
body "$BVC" label_draw | grep -qxF '    const char *sub = s_rim_mood == BOT_WAITING || s_rim_mood == BOT_DONE ? s_host.sub : "";' &&
    body "$BVC" label_draw | grep -qF '(sub[0] ? LABEL_ICON_SUB_R : LABEL_ICON_R)' &&
    body "$BVC" label_draw | grep -qxF '    arc_line(&layer, &lv_font_montserrat_18, sub, LABEL_SUB_R, text_c);' || WIRING="$WIRING host-line-only-while-waiting-or-done"
body "$BVC" bot_view_create | awk '/s_badge_gap = circle_create\(/ {g = NR} /s_badge_dot = circle_create\(/ {d = NR}
    /s_eye\[e\]\.canvas = canvas_create\(/ {e = NR} END {exit !(g && d && e && g < d && d < e)}' || WIRING="$WIRING badge-created-before-the-eye-canvases"
body "$BVC" bot_view_rim_pose | grep -qxF '    s_rim_want = bot_rim_key(p->mood, p->asleep);' &&
    body "$BVC" bot_view_rim_pose | grep -qxF '    s_rim_face = bot_rim_key(p->face, p->asleep);' &&
    body "$BVC" bot_view_rim_pose | grep -qxF '    rim_sync(t);' &&
    body "$BVC" bot_view_update | grep -qxF '    rim_sync(t);' || WIRING="$WIRING outline-job-from-the-mood-and-swap-from-the-face"
! body "$BVC" bot_view_update | grep -q 'rim_raster(' || WIRING="$WIRING no-outline-render-on-the-lvgl-task"
body "$RSC" rim_slots_run | awk '/if \(!atomic_load\(&s->own\) && atomic_load\(&s->base_gen\) != gen\) \{/ {o = NR} /raster\(ctx, s->buf\[RIM_SLOT_SPARE\], shape, tone, 1\.0f, 1\.0f\);/ {r = NR}
    /atomic_store\(&s->base_gen, gen\);/ {g = NR} /\(v == RIM_SLOT_SPARE && !atomic_load\(&s->own\)\)\) continue;/ {s = NR} END {exit !(o && r == o + 1 && g > r && s > g)}' ||
    WIRING="$WIRING rim_task-base-first-into-the-spare-slot"
body "$RSC" slots_start | awk '/rim_slots_show\(s, RIM_SLOT_BASE, ops\);/ {s = NR} /atomic_store\(&s->own, false\);/ {o = NR} /rim_job_new\(/ {j = NR} END {exit !(o && s < o && o < j)}' ||
    WIRING="$WIRING rim_start-leaves-the-variants-before-the-job"
body "$RSC" slots_swap | awk '/s->buf\[RIM_SLOT_SPARE\] = old;/ {p = NR} /rim_slots_show\(s, RIM_SLOT_BASE, ops\);/ {v = NR} /atomic_store\(&s->own, true\);/ {o = NR} /ops->swapped\(/ {b = NR} /ops->wake\(/ {n = NR} END {exit !(p && p < v && v < o && o < b && b < n)}' ||
    WIRING="$WIRING rim_swap-badge-and-label-with-the-outline"
grep -qxF '    int v = rim_slots_sync(&s_slots, s_rim_want, s_rim_face, s_rim_ratio, t, &RIM_OPS);' "$BVC" || WIRING="$WIRING rim_sync-on-the-frame-time"
body "$BVC" bot_view_tick | sed -n 3p | grep -qxF '    rim_sync(t);' || WIRING="$WIRING bot_view_tick-syncs-the-outline-first"
grep -qxF '    d.recolor = lv_color_hex(ring_glint_color(mood_rgb(s_rim_mood), GLINT_MIX));' "$BVC" || WIRING="$WIRING glint-in-the-outline-colour"
body "$BVC" lap_step | grep -qxF '    return bot_lap_step(s_lap_at, t, GLINT_STEP_S, GLINT_STEPS);' &&
    body "$BVC" bot_view_update | grep -qxF '    s_lap_at = p->lap_at;' || WIRING="$WIRING lap-from-the-pose-start"
body "$BVC" rim_raster | grep -qF 'mood_rgb(bot_rim_mood(tone)), bot_rim_alpha(tone));' || WIRING="$WIRING outline-alpha-from-bot_rim_alpha"
body "$BVC" eye_raster | grep -qxF '    float lid_y = (float)bot_lid_line_y(j->ys, j->n, j->width, j->lid, j->keep);' &&
    body "$BVC" eye_raster | grep -qxF '    bot_raster_lid_box(lid_y, &j->oy, &j->h);' &&
    body "$BVC" eye_raster | grep -qF 'bot_raster_stroke_below(scratch, NULL, (uint8_t *)(eye->buf + w * h), lid_y, w, h,' &&
    ! body "$BVC" eye_raster | grep -q 'bot_raster_stroke(' &&
    body "$BVC" eyes_draw | grep -qF '.width = width[e], .lid = st[e].lid, .keep = st[e].keep};' || WIRING="$WIRING eyes-clipped-at-the-lid-line"
if [ -n "$WIRING" ]; then
    echo "run.sh: FAIL bot view wiring:$WIRING" >&2
    exit 1
fi
echo "bot view wiring: outline dy from bot_rim_dy, label icon from bot_label_mark, host line while waiting or done, badge under the eyes, tone and alpha from bot_rim_tone, mood beat (#115), eyes clipped at the lid line (#116)"
MVC="$D/../../main/moment_view.c"
fbody() { awk -v f="$2" '$0 ~ "^static __attribute__\\(\\(noinline\\)\\) [a-z_]+ " f "\\(" {on=1} on {print} on && /^}/ {exit}' "$1"; }
UGL=""
body "$MAINC" page_bot_show | awk '/^    if \(!on\) \{$/ {o = 1; next} o && /^    }$/ {o = 0} o && /^        usage_view_close\(\);$/ {c = 1} END {exit !c}' || UGL="$UGL page_bot_show-false-closes-the-usage-screens"
body "$MAINC" pages_pause | grep -qxF '    pages_hide_all();' && body "$MAINC" ota_frame | grep -qxF '            pages_pause();' || UGL="$UGL the-OTA-face-hides-every-page"
body "$MAINC" page_bot_input | awk '/^    if \(usage_view_input\(&u, t\)\) return;$/ {r = NR} /bot_(push|look|set_mood_user)\(/ && !b {b = NR} /^    for \(int n = in->pushes \+ in->touches; n > 0; n--\) bot_push\(&s_bot, t\);$/ {p = NR} END {exit !(r && b > r && p > r)}' ||
    UGL="$UGL page_bot_input-gives-the-screens-every-input-they-take-then-falls-through-to-the-bot"
body "$MAINC" page_bot_frame | awk '/^    bot_under_usage\(usage_view_frame\(t\)\);$/ {u = NR} /^    if \(s_usage_on\) return;$/ {r = NR} /bot_view_|bot_wait_restart\(/ && !b {b = NR} /^    bot_view_set_hint\(usage_view_hint\(\)\);$/ {h = NR} END {exit !(u && r > u && b > r && h > r)}' ||
    UGL="$UGL page_bot_frame-hides-the-bot-under-the-screens-and-feeds-the-hint"
fbody "$MAINC" page_tick | awk '/^    usage_view_poll\(\);$/ {p = NR} /^    usage_view_set_offline\(offline\);$/ {o = NR} /^    pages_dispatch\(&s_nav, in, t\);$/ {d = NR} /^    bool bot_page = pg->frame == page_bot_frame && !usage_view_open\(\);$/ {b = NR} END {exit !(p && o > p && d > o && b > d)}' ||
    UGL="$UGL page_tick-polls-and-marks-offline-before-the-input"
grep -qxF 'static EXT_RAM_BSS_ATTR bool s_usage_on;' "$MAINC" || UGL="$UGL s_usage_on-in-PSRAM"
if [ -n "$UGL" ]; then
    echo "run.sh: FAIL usage screens wiring:$UGL" >&2
    exit 1
fi
ORB=""
body "$MAINC" page_bot_frame | awk '/^    if \(s_usage_on\) return;$/ {r = NR} /^    if \(wx_shift_tick\(&s_bot_shift, &WX_SHIFT_ORBIT, f->dt\)\) bot_shift_root\(\);$/ {o = NR} END {exit !(r && o == r + 1)}' ||
    ORB="$ORB page_bot_frame-steps-the-orbit-only-on-screen"
body "$MAINC" bot_shift_root | grep -qxF '    wx_shift_xy(&s_bot_shift, &WX_SHIFT_ORBIT, &dx, &dy);' &&
    body "$MAINC" bot_shift_root | grep -qxF '    lv_obj_set_style_translate_x(s_bot_page, dx, 0);' &&
    body "$MAINC" bot_shift_root | grep -qxF '    lv_obj_set_style_translate_y(s_bot_page, dy, 0);' || ORB="$ORB the-orbit-translates-the-bot-page"
body "$MAINC" bot_shift_root | grep -qxF '    lv_obj_set_style_translate_x(s_join_label, dx, 0);' &&
    body "$MAINC" bot_shift_root | grep -qxF '    lv_obj_set_style_translate_y(s_join_label, dy, 0);' &&
    [ "$(grep -c 'lv_obj_set_style_translate_[xy](s_join_label, ' "$MAINC")" -eq 2 ] &&
    body "$MAINC" join_label_create | grep -qxF '    s_join_label = lv_label_create(lv_layer_top());' || ORB="$ORB the-join-label-on-the-top-layer-orbits-with-the-bot-page"
[ "$(grep -c 'bot_shift_root()' "$MAINC")" -eq 1 ] || ORB="$ORB bot_shift_root-only-on-a-step"
fbody "$MAINC" page_tick | grep -qxF '    page_frame_t f = {.t = t, .dt = dt, .offline = offline};' || ORB="$ORB page_tick-passes-the-frame-dt"
grep -qxF 'static EXT_RAM_BSS_ATTR wx_shift_t s_bot_shift;' "$MAINC" || ORB="$ORB s_bot_shift-in-PSRAM"
body "$MAINC" app_main | grep -qxF '    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);' || ORB="$ORB the-screen-is-not-scrollable-so-the-shifted-page-shows-no-scrollbars"
body "$MVC" sprite | grep -qxF '    lv_area_t box = on_screen(WXM_BOX_X, WXM_BOX_Y, WXM_BOX_W, WXM_BOX_H);' &&
    body "$MVC" text | grep -qF 'u = on_screen(WXM_TEXT_X, WXM_TEXT_Y, WXM_TEXT_W, WXM_TEXT_H);' &&
    body "$MVC" on_screen | grep -qxF '    lv_obj_get_coords(s_parent, &c);' || ORB="$ORB moment-areas-follow-the-shifted-page"
if [ -n "$ORB" ]; then
    echo "run.sh: FAIL bot orbit wiring:$ORB" >&2
    exit 1
fi
echo "usage screens wiring: closed on a page change and under the OTA face, the opening tap's frame all theirs (a push beside it dropped), the bot's input when they don't open, offline marks them stale"
for ARG in true false; do
    body "$MAINC" ota_frame | grep -qF "moment_view_hold($ARG);" || WIRING="$WIRING ota_frame-moment_view_hold($ARG)"
done
body "$MAINC" ota_frame | grep -qF 'moment_unact(now_s());' || WIRING="$WIRING ota_frame-ends-the-act"
fbody "$MAINC" page_tick | awk '/moment_tick\(t, have_wx \? &wx : NULL, calm, bot_page\);/ {m = NR} /gc_tick\(/ {g = NR} /f\.pose = bot_pose\(&s_bot, t\);/ {p = NR} END {exit !(m && m < g && m < p)}' ||
    WIRING="$WIRING page_tick-runs-moment_tick-before-the-chase-and-the-pose"
fbody "$MAINC" page_tick | awk '/^    else if \(!ds\.view_on && s_scene_resume_at == 0\) moment_tick\(/ {m = NR} /^    bot_read_moment\(&s_bot, moment_view_busy\(\), t\);$/ {r = NR} /f\.pose = bot_pose\(&s_bot, t\);/ {p = NR} END {exit !(m && r == m + 1 && r < p)}' ||
    WIRING="$WIRING page_tick-reads-around-a-moment-before-the-pose"
MT="$(fbody "$MAINC" moment_tick)"
for L in 'if (!s_ks_lv->weather_moments && !moment_view_busy()) moment_view_release();' 'if (moment_view_busy() && (!bot_page || !s_ks_lv->weather_moments)) {' '} else if (moment_view_busy() && (s_bot.mood != s_moment_mood || calm)) {' \
    'moment_view_abort(t);' 'moment_view_stop();' '.blocked = moment_view_busy() || music_view_busy() || mm_recent(&s_mm, t) || calm || s_chase.phase != GC_IDLE,' '.on = s_ks_lv->weather_moments,' \
    '.every_s = s_ks_lv->weather_every_min * 60};' '.bot_mood = (int)s_bot.mood, .shown = bot_page && !s_bot.asleep,' 'if (wxm_tick(&s_wxm, &in, &m) && moment_view_start(&m, t)) {' 's_moment_mood = s_bot.mood;' \
    'wxm_shown(&s_wxm, &m, t);' 'bot_act_begin(&s_bot, t);' 'double tau = moment_view_poll(t);' \
    'while (tau >= 0 && s_cue < s_cue_n && tau >= s_cues[s_cue].t) moment_cue(&s_cues[s_cue++], t);' 's_chase.hold = moment_view_busy();'; do
    printf '%s\n' "$MT" | grep -qF "$L" || WIRING="$WIRING moment_tick:$L"
done
[ "$(grep -c 'wxm_shown(' "$MAINC")" -eq 1 ] && printf '%s\n' "$MT" | grep -B1 -F 'wxm_shown(&s_wxm, &m, t);' | head -1 | grep -qF 'if (wxm_tick(&s_wxm, &in, &m) && moment_view_start(&m, t)) {' ||
    WIRING="$WIRING moment_tick-records-a-moment-only-once-it-started"
body "$MVC" moment_view_start | grep -qF 's_retry_at = t + MV_RETRY_S;' || WIRING="$WIRING start-retries-a-failed-allocation-later"
[ "$(printf '%s\n' "$MT" | grep -cF 'moment_unact(t);')" -eq 2 ] || WIRING="$WIRING moment_tick-ends-the-act-on-stop-and-abort"
MC="$(body "$MAINC" moment_cue)"
for L in 'case WXM_GLIDE: bot_act_gaze(&s_bot, c->a, c->b, true, t); break;' 'case WXM_SACCADE: bot_act_gaze(&s_bot, c->a, c->b, false, t); break;' \
    'case WXM_LID: bot_act_lid(&s_bot, c->a, c->b, t); break;' 'case WXM_BLINK: bot_act_blink(&s_bot, c->a, t); break;' \
    'bot_act_eyes(&s_bot, c->eyes == WXM_EYES_ROUND ? BOT_EYES_ROUND : c->eyes == WXM_EYES_HAPPY ? BOT_EYES_HAPPY : -1, t);' \
    'case WXM_HOP: bot_act_hop(&s_bot, t); break;' 'bot_act_end(&s_bot, c->a > 0, c->b, t);'; do
    printf '%s\n' "$MC" | grep -qF "$L" || WIRING="$WIRING moment_cue:$L"
done
body "$MAINC" moment_unact | grep -qF 'if (s_acting && s_bot.mood != s_moment_mood) bot_act_drop(&s_bot, t);' &&
    body "$MAINC" moment_unact | grep -qF 'else if (s_acting) bot_act_end(&s_bot, true, 0.3, t);' && body "$MAINC" moment_unact | grep -qF 's_cue = s_cue_n;' ||
    WIRING="$WIRING moment_unact-ends-the-act-and-the-cues"
body "$MAINC" page_bot_frame | tail -2 | grep -qF 'moment_view_frame(t);' || WIRING="$WIRING page_bot_frame-draws-the-moment-last"
body "$MAINC" page_bot_frame | grep -qF 'bot_view_chase(chase->chasing || moment_view_busy() || music_view_busy(), chase->chasing && chase->label_opa > 0);' ||
    WIRING="$WIRING page_bot_frame-merges-the-eyes-into-one-area-during-a-moment"
body "$MAINC" page_bot_frame | grep -qxF '    bot_view_set_offline(join_glyph_shown(f->offline, s_join_shown));' &&
    body "$MAINC" join_frame | grep -qxF '    join_label_t show = join_label_pick(&in);' &&
    body "$MAINC" join_frame | grep -qxF '    s_join_shown = show;' || WIRING="$WIRING page_bot_frame-asks-join_glyph_shown-with-the-shown-join-label"
JF="$(body "$MAINC" join_frame)"
for L in '.setup = s_setup,' '.online = ember_client_online(),' '.join_failures = ember_client_join_failures(),' \
    '.unauthorized = ember_client_link() == EMBER_LINK_UNAUTHORIZED,' '.has_url = config_store_has_ember_url(),' \
    '.has_device = config_store_has_device(),' '.update_knob = ember_client_view_compat() == EMBER_VIEW_UPDATE_KNOB,' \
    '.update_ember = ember_client_view_compat() == EMBER_VIEW_UPDATE_EMBER,'; do
    printf '%s\n' "$JF" | grep -qxF "        $L" || WIRING="$WIRING join_frame:$L"
done
body "$BVC" bot_view_create | grep -qxF '    offline_glyph_raster(s_offline_a8);' &&
    grep -qxF 'static EXT_RAM_BSS_ATTR uint8_t s_offline_a8[OFFLINE_GLYPH_W * OFFLINE_GLYPH_H];' "$BVC" &&
    [ "$(grep -c 'offline_glyph_raster(' "$BVC")" -eq 1 ] || WIRING="$WIRING offline-glyph-rastered-once-into-PSRAM"
body "$MAINC" app_main | grep -qF 'moment_view_create(s_bot_page);' && body "$MAINC" app_main | grep -qF 'wxm_init(&s_wxm);' ||
    WIRING="$WIRING app_main-creates-the-moments"
grep -qF 'static EXT_RAM_BSS_ATTR wxm_trigger_t s_wxm;' "$MAINC" || WIRING="$WIRING trigger-in-PSRAM"
[ "$(grep -c 'wxm_paint(' "$MVC")" -eq 1 ] && body "$MVC" moment_bake_task | grep -qF 'atomic_fetch_add(&s_refused, wxm_paint(&s_m, &s_l, s_arena, bake_yield));' ||
    WIRING="$WIRING wxm_paint-only-in-the-bake-task-with-the-yield-hook"
body "$MVC" moment_bake_task | awk '/s_yield_at = t0;/ {y = NR} /wxm_paint\(/ {p = NR} END {exit !(y && p > y)}' &&
    body "$MVC" bake_yield | grep -qF 'if (esp_timer_get_time() - s_yield_at < MV_YIELD_US) return;' &&
    body "$MVC" bake_yield | awk '/vTaskDelay\(1\);/ {d = NR} /s_yield_at = esp_timer_get_time\(\);/ {s = NR} END {exit !(d && s > d)}' &&
    YUS=$(grep -o '^#define MV_YIELD_US [0-9]*' "$MVC" | awk '{print $3}') && [ "${YUS:-0}" -ge 20000 ] && [ "$YUS" -le 50000 ] ||
    WIRING="$WIRING bake-yields-a-tick-every-20-50-ms"
body "$MVC" moment_bake_task | grep -qF 'if (!atomic_compare_exchange_strong(&s_bake, &queued, BAKE_RUNNING)) continue;' &&
    body "$MVC" moment_bake_task | awk '/wxm_paint\(/ {p = NR} /atomic_store\(&s_bake, BAKE_DONE\);/ {d = NR} END {exit !(p && d > p)}' ||
    WIRING="$WIRING bake-claims-by-CAS-and-publishes-after-the-paint"
body "$MVC" moment_view_poll | grep -qF 'if (s_state == MV_BAKING && atomic_load(&s_bake) == BAKE_DONE && (s_cancel || t >= s_play_at)) {' ||
    WIRING="$WIRING poll-attaches-a-landed-bake-after-the-pre-roll"
body "$MVC" moment_view_start | grep -qF 's_play_at = t + (m->quiet ? 0 : WXM_PRE_S);' &&
    body "$MVC" moment_view_start | grep -qF 'if (s_held || s_state != MV_IDLE || atomic_load(&s_bake) != BAKE_IDLE) return false;' &&
    body "$MVC" moment_view_start | grep -qF 'if (s_l.total > WXM_ARENA || t < s_retry_at) return false;' && body "$MVC" moment_view_start | grep -qF 'if (!ensure()) {' ||
    WIRING="$WIRING start-waits-for-an-idle-bake-and-the-hold"
body "$MVC" moment_view_abort | grep -qF 'int k = wxm_abort_frame(s_k);' || WIRING="$WIRING abort-before-any-frame-ends-it"
body "$MVC" moment_view_frame | grep -qF 'int d = wxm_diff(&s_l, s_any ? &s_shown : NULL, &f);' || WIRING="$WIRING frame-diffs-against-the-shown-frame"
body "$MVC" sprite | sed -n 2,4p | grep -qF 'lv_obj_invalidate_area(s_parent, &box);' || WIRING="$WIRING sprite-invalidates-its-box-first"
body "$MVC" text | awk '/lv_obj_invalidate_area\(s_parent, &u\);/ {u = NR} /bot_view_icon_redraw\(&icon\);/ {i = NR} /lv_obj_set_style_opa\(s_text/ {o = NR} END {exit !(u && u < i && u < o)}' ||
    WIRING="$WIRING text-invalidates-the-line-and-the-icon-as-one-area-first"
body "$MVC" hide_all | grep -qF 'if (s_any && s_shown.icon_off && bot_view_icon_off(false, &a)) bot_view_icon_redraw(&a);' ||
    WIRING="$WIRING hide_all-returns-a-hidden-icon"
body "$MVC" text | grep -qF 'bool turns = s_m.quiet && (s_any && s_shown.icon_off) != f->icon_off;' || WIRING="$WIRING text-joins-the-icon-only-when-it-turns"
body "$MVC" ensure | grep -qF 'if (s_task && !s_arena) s_arena = heap_caps_malloc(WXM_ARENA, MALLOC_CAP_SPIRAM);' &&
    [ "$(grep -c 'heap_caps_malloc(' "$MVC")" -eq 1 ] || WIRING="$WIRING arena-in-PSRAM-only-on-a-start"
body "$MVC" moment_view_release | grep -qF 'if (!s_arena || s_state != MV_IDLE || atomic_load(&s_bake) != BAKE_IDLE) return;' ||
    WIRING="$WIRING release-only-when-nothing-bakes-or-plays"
STK=$(grep -o 'moment_bake_task, "moment", [0-9]*' "$MVC" | awk '{print $3}')
python3 -c "import json,sys; sys.exit(json.load(open('$D/../../tools/stack_walk.json'))['tasks']['moment']['stack'] != int('$STK'))" ||
    WIRING="$WIRING moment-stack-matches-stack_walk.json"
body "$BVC" label_crop | grep -qF 'icon_apply();' && body "$BVC" label_draw | grep -qF 's_icon_box = a;' || WIRING="$WIRING label-redraw-keeps-the-icon-state"
for DEF in '#define CX 236.0' '#define CY 233.0' '#define FILL 0.84' '#define HOP_SCALE 0.45'; do
    grep -qxF "$DEF" "$BVC" || WIRING="$WIRING test_moment-geometry:$DEF"
done
grep -qF '#define BOT_VIEW_EYE_SCALE 1.15' "$D/../../main/bot_view.h" || WIRING="$WIRING test_moment-eye-scale"
if [ -n "$WIRING" ]; then
    echo "run.sh: FAIL weather moment wiring:$WIRING" >&2
    exit 1
fi
echo "weather moment wiring: OTA hold, trigger and cues before the pose, aborts, chase hold, bake by CAS on its task yielding a tick every ${YUS} us, one area per change, reading around a quiet moment"
MUT="$(fbody "$MAINC" music_tick)"
for L in 'if (np_client_get(&s_mm_np, &s_mm_gen))' 'mm_in_t in = {.t = t, .have = s_mm_np.have && on, .playing = playing,' 'bool playing = s_mm_np.have && s_mm_np.info.state == NP_PLAYING;' \
    'if (music_view_busy() && (!on || !bot_page)) {' 'music_view_stop();' \
    '} else if (music_view_busy() && (s_bot.mood != s_mm_mood || calm || offline || !playing)) {' 'music_view_abort(t);' \
    '.rest = moment_mood(s_bot.mood) == WXM_REST, .shown = bot_page && !s_bot.asleep,' \
    '.blocked = !on || offline || calm || moment_view_busy() || music_view_busy() || s_chase.phase != GC_IDLE,' '.other_at = s_wxm.last_at};' \
    's_mm_mood = s_bot.mood;' 'bot_act_begin(&s_bot, t);' 'double tau = music_view_poll(t);' \
    'if (c[s_mm_cue].op == MM_HAPPY) bot_act_eyes(&s_bot, BOT_EYES_HAPPY, t);' 'if (music_view_busy()) s_chase.hold = true;'; do
    printf '%s\n' "$MUT" | grep -qF "$L" || WIRING="$WIRING music_tick:$L"
done
[ "$(grep -c 'mm_shown(' "$MAINC")" -eq 1 ] && printf '%s\n' "$MUT" | grep -B1 -F 'mm_shown(&s_mm, t);' | head -1 | grep -qF 'if (mm_tick(&s_mm, &in) && music_view_start(t)) {' ||
    WIRING="$WIRING music_tick-records-a-moment-only-once-it-started"
[ "$(printf '%s\n' "$MUT" | grep -cF 'music_unact(t);')" -eq 3 ] || WIRING="$WIRING music_tick-ends-the-act-on-stop-abort-and-the-end-cue"
body "$MAINC" music_unact | grep -qxF '    if (s_mm_acting && s_bot.mood != s_mm_mood) bot_act_drop(&s_bot, t);' &&
    body "$MAINC" music_unact | grep -qxF '    else if (s_mm_acting) bot_act_end(&s_bot, true, 0.3, t);' || WIRING="$WIRING music_unact-ends-the-act"
fbody "$MAINC" page_tick | awk '/^    bot_read_moment\(&s_bot, moment_view_busy\(\), t\);$/ {r = NR}
    /^    music_tick\(t, calm, offline, bot_page, !s_scene_on && !ds\.view_on && s_scene_resume_at == 0\);$/ {m = NR}
    /gc_tick\(/ {g = NR} /f\.pose = bot_pose\(&s_bot, t\);/ {p = NR} END {exit !(r && m == r + 1 && m < g && m < p)}' ||
    WIRING="$WIRING page_tick-runs-music_tick-after-the-weather-moment-before-the-chase-and-the-pose"
fbody "$MAINC" page_tick | awk '/^    bot_view_rim_pose\(&f\.pose, t\);$/ {s = NR} /^    f\.pose\.eye_dy -= music_view_eye_px\(t\) \/ bot_view_radius_px\(\);$/ {e = NR}
    /^    pages_frame\(&s_nav, &f\);$/ {p = NR} END {exit !(s && e == s + 1 && p > e)}' || WIRING="$WIRING page_tick-adds-the-bob-to-the-pose-before-the-frame"
for L in 'music_view_hold(true);' 'music_unact(now_s());' 'music_view_hold(false);'; do
    body "$MAINC" ota_frame | grep -qxF "            $L" || body "$MAINC" ota_frame | grep -qxF "        $L" || WIRING="$WIRING ota_frame:$L"
done
[ "$(body "$MAINC" scene_start | grep -A3 -xF '    if (music_view_busy()) {' | tr -d ' \n')" = 'if(music_view_busy()){music_view_stop();music_unact(t);}' ] ||
    WIRING="$WIRING scene_start-stops-the-music-moment"
[ "$(body "$MAINC" page_bot_frame | tail -3 | head -2 | tr -d ' \n')" = 'music_view_frame(t);moment_view_frame(t);' ] ||
    WIRING="$WIRING page_bot_frame-draws-the-music-moment-before-the-weather-moment"
NPC="$D/../../main/nowplaying_client.c"
body "$NPC" np_client_feed | awk '/xSemaphoreGive\(s_lock\);/ {g = NR} /ESP_LOG[A-Z]\(/ {n++; if (!g) bad = 1} END {exit !(g && n && !bad)}' ||
    WIRING="$WIRING np_client_feed-logs-only-after-the-lock"
if [ -n "$WIRING" ]; then
    echo "run.sh: FAIL music moment wiring:$WIRING" >&2
    exit 1
fi
echo "music moment wiring: trigger, stop, abort and act in music_tick after the weather moment, the bob on the pose, OTA hold, demo stop, draw order, the now-playing log outside its lock"
body "$ECL" poll_task | awk '/^        demo_mark_ready\(\);$/ {r = NR} /np_client_service\(/ {n = NR} END {exit !(r && n && r < n)}' ||
    WIRING="$WIRING demo-ready-before-the-art-fetch"
body "$ECL" apply_view | awk '/np_client_feed\(/ {n++} /demo_flags\(/ {if (n) {a++; if ($0 == "    demo_flags(want, want && e->quiet);") ga++} else {b++; if ($0 == "    if (want) demo_flags(true, e->quiet);") gb++}}
    END {exit !(n == 1 && b == 1 && gb == 1 && a == 1 && ga == 1)}' || WIRING="$WIRING apply_view-sets-the-demo-flags-before-the-now-playing-feed-and-clears-them-after"
nword() { grep -ow -- "$1" | wc -l | tr -d ' '; }
for F in s_demo_view_on s_demo_quiet; do
    [ "$(nword "$F" < "$ECL")" -eq 3 ] && [ "$(body "$ECL" demo_flags | nword "$F")" -eq 1 ] &&
        [ "$(body "$ECL" ember_client_demo_status | nword "$F")" -eq 1 ] ||
        { WIRING="$WIRING $F-only-in-its-declaration-demo_flags-and-ember_client_demo_status"; grep -nw -- "$F" "$ECL" >&2; }
done
[ "$(nword demo_flags < "$ECL")" -eq 4 ] ||
    { WIRING="$WIRING demo_flags-defined-once-and-called-only-in-apply_view-and-demo_sync"; grep -nw demo_flags "$ECL" >&2; }
[ "$(body "$ECL" demo_flags | sed -n '3,6p' | tr -d ' \n')" = 'taskENTER_CRITICAL(&s_demo_mux);s_demo_view_on=view_on;s_demo_quiet=quiet;taskEXIT_CRITICAL(&s_demo_mux);' ] &&
    body "$ECL" ember_client_demo_status | awk '/taskENTER_CRITICAL\(&s_demo_mux\);/ {l = 1} /taskEXIT_CRITICAL\(&s_demo_mux\);/ {l = 0} /s_demo_view_on|s_demo_quiet/ {r++; if (!l) bad = 1} END {exit !(r == 2 && !bad)}' ||
    WIRING="$WIRING demo-view-and-quiet-flags-written-and-read-together-under-s_demo_mux"
[ "$(grep -c 'np_client_feed(' "$ECL")" -eq 1 ] && body "$ECL" apply_view | grep -qxF '    np_client_feed(e->has_np ? &e->np : NULL, want);' ||
    WIRING="$WIRING apply_view-marks-a-demo-np-block"
[ "$(body "$ECL" demo_sync | awk '/^    if \(P->vp\.legacy \|\| !P->vs\.have_view \|\| P->vs\.unsupported\) \{$/ {on = 1; next} on && /^    }$/ {exit} on' | tr -d ' \n')" = \
    'P->demo_gen=gen;np_client_drop_demo();demo_flags(false,false);return;' ] || WIRING="$WIRING demo_sync-early-return-drops-the-demo-np-block-and-clears-the-demo-flags"
printf '%s\n' "$MUT" | awk '/^        s_mm_key = mm_track_key\(/ {k = NR} /(^|[^a-z_.>])on[ \t]*([-+*\/%&|^]|<<|>>)?=([^=]|$)/ {n++; if ($0 == "    on = mm_block_on(on, s_mm_np.demo);") o = NR}
    /\(!on \|\| !bot_page\)/ && !u {u = NR} END {exit !(k && n == 1 && o == k + 1 && u > o)}' || WIRING="$WIRING music_tick-ignores-a-demo-np-block"
[ "$(grep -c 'atomic_store(&s_quiet_dim, ' "$ECL")" -eq 1 ] &&
    body "$ECL" apply_view | grep -qxF '    atomic_store(&s_quiet_dim, demo_quiet_dim(playing, atomic_load(&s_quiet)));' ||
    WIRING="$WIRING quiet-dim-only-in-apply_view"
[ "$(grep -c 'scene_gate(t, ' "$MAINC")" -eq 3 ] || WIRING="$WIRING three-scene_gate-call-sites"
fbody "$MAINC" page_tick | grep -qxF '    if (scene_gate(t, in.detents || in.pushes || in.touches || in.longs || in.wakes || in.hswipes)) in = (page_input_t){0};' ||
    WIRING="$WIRING page_tick-gates-its-input"
body "$MAINC" scene_frame | sed -n 3p | grep -qxF '    if (scene_gate(t, inputs_pending())) drop_inputs();' || WIRING="$WIRING scene_frame-gates-first"
body "$MAINC" frame_cb | awk '/^    scene_frame\(t\);$/ {f = NR} /^    if \(scene_gate\(t, steps \|\| swipes\)\) steps = swipes = 0;$/ {g = NR} /page_go\(steps, t\);/ {p = NR} END {exit !(f && f < g && g < p)}' ||
    WIRING="$WIRING frame_cb-gates-steps-and-swipes"
body "$MAINC" scene_frame | grep -qxF '    } else if (ended && (s_scene_gate.on || (!st.playing && !timed))) {' || WIRING="$WIRING scene_frame-resume-branch"
body "$MAINC" scene_start | grep -qxF '    demo_gate_start(&s_scene_gate);' || WIRING="$WIRING scene_start-clears-the-gate"
body "$MAINC" scene_start | grep -qxF '    s_scene_half = s_half;' && body "$MAINC" scene_start | grep -qxF '    gc_resume(&s_chase, s_chase.work_s, s_chase.wait_s);' ||
    WIRING="$WIRING scene_start-pauses-the-chase"
body "$MAINC" scene_end | awk '/gc_restore\(&s_chase, &s_scene_chase, / {r = NR} /^    gc_half_chase_restore\(&s_half, &s_scene_half\);$/ {h = NR} END {exit !(r && h > r)}' ||
    WIRING="$WIRING scene_end-restores-the-half-chase"
fbody "$MAINC" page_tick | grep -qxF '    if (s_scene_on && s_scene_act.kind == DEMO_ACT_ASLEEP) bot_sleep(&s_bot, t);' ||
    WIRING="$WIRING page_tick-asleep-act-sleeps"
body "$MAINC" scene_end | grep -qxF '    else if (s_scene_mood0 == BOT_SLEEPY) bot_sleep_in(&s_bot, s_scene_sleep_in, t);' &&
    body "$MAINC" scene_start | grep -qxF '    s_scene_sleep_in = bot_asleep_in(&s_bot, t);' &&
    body "$MAINC" scene_start | tail -2 | head -1 | grep -qxF '    if (!s_bot.asleep || b->act.kind == DEMO_ACT_SLEEPY) bot_sleep_in(&s_bot, INFINITY, t);' &&
    body "$MAINC" scene_end | grep -qF 'bool slept = s_scene_act.kind == DEMO_ACT_SLEEPY || s_scene_act.kind == DEMO_ACT_ASLEEP;' ||
    WIRING="$WIRING scene_end-wakes-after-asleep"
fbody "$MAINC" page_tick | grep -qxF '    int req = s_scene_on ? s_scene_chase_req : atomic_exchange(&s_chase_req, -1);' &&
    [ "$(grep -c 'atomic_store(&s_chase_req, ' "$MAINC")" -eq 1 ] || WIRING="$WIRING server-chase-deferred-during-a-scene"
fbody "$MAINC" page_tick | grep -qxF '    atomic_store(&s_chase_status, s_scene_on ? 1 : !glint ? 2 : s_chase.phase != GC_IDLE ? 1 : 0);' &&
    body "$MAINC" scene_start | sed -n 3,4p | tr -d '\n' | grep -qxF '    s_scene_on = true;    atomic_store(&s_chase_status, 1);' &&
    [ "$(grep -c 'atomic_store(&s_chase_status, ' "$MAINC")" -eq 2 ] || WIRING="$WIRING chase-request-busy-during-a-scene"
[ "$(fbody "$MAINC" page_tick | grep -A10 -xF '        gc_aim_t aim = gc_half_chase_step(&s_half, psi, CHASE_LEAD_DEG, &h, &deg);')" = "$(printf '%s\n' \
    '        gc_aim_t aim = gc_half_chase_step(&s_half, psi, CHASE_LEAD_DEG, &h, &deg);' '        if (!h.follow) {' \
    '            bot_hold_gaze(&s_bot, t + 0.5);' '            if (h.blink) bot_blink(&s_bot, t);' '        }' '        if (aim == GC_AIM_SNAP) {' \
    '            if (s_bot.tracking) bot_track_end(&s_bot, t);' '            bot_look_still(&s_bot, chase_gaze(&ref, 0, chase->orbit), 0, t);' \
    '        } else if (aim == GC_AIM_TRACK) {' '            chase_track(&ref, deg, chase->orbit, t);' '        }')" ] && [ "$(grep -c 'gc_half_chase_step(' "$MAINC")" -eq 1 ] && ! grep -q 's_half\.' "$MAINC" ||
    WIRING="$WIRING half-chase-aims-through-gc_half_chase_step"
body "$MAINC" ota_frame | awk '/^            s_ota_paused = true;$/ {p = NR} /^            ember_client_demo_input\(\);$/ {i = NR}
    /^            if \(s_scene_on\) scene_end\(now_s\(\)\);$/ {e = NR} /^            s_scene_resume_at = 0;$/ {r = NR} /^            pages_pause\(\);$/ {q = NR}
    END {exit !(p && i == p + 1 && e == i + 1 && r == e + 1 && q == r + 1)}' || WIRING="$WIRING ota-ends-a-playing-or-offered-demo"
if [ -n "$WIRING" ]; then
    echo "run.sh: FAIL demo scene wiring:$WIRING" >&2
    exit 1
fi
echo "demo scene wiring: ready before the art fetch, demo view and quiet flags set together and cleared after the now-playing feed, the demo np block marked, dropped on an early demo_sync and ignored by music_tick, flags used nowhere else, quiet dim, three gates, resume branch, gate and chase per scene, asleep act, chase requests busy from scene_start, half chase through gc_half_chase_step, OTA ends a playing or offered demo"
body "$MAINC" page_bot_show | grep -qxF '    if (on && lv_obj_has_flag(s_bot_page, LV_OBJ_FLAG_HIDDEN)) s_bot_shown = true;' ||
    WIRING="$WIRING page_bot_show-flags-hidden-to-shown"
body "$MAINC" page_bot_frame | awk '/^    if \(ember_host_lead_new\(&s_wait_host, &host\)\) bot_wait_restart\(&s_bot, t, true\);$/ {r = NR} /^    else if \(s_bot_shown\) bot_wait_restart\(&s_bot, t, false\);$/ {e = NR} /^    s_bot_shown = false;$/ {c = NR} /^    s_wait_host = host;$/ {h = NR} END {exit !(r && e == r + 1 && c == r + 2 && h == r + 3)}' ||
    WIRING="$WIRING page_bot_frame-latches-a-new-lead-restart-not-a-shown-one"
fbody "$MAINC" page_tick | awk '/^    bot_wait_scene\(&s_bot, s_scene_on \? s_scene_n : 0, t\);$/ {d = NR} /^    if \(s_scene_on && s_scene_act\.kind == DEMO_ACT_ASLEEP\) bot_sleep\(/ {m = NR} END {exit !(d && m == d + 1)}' ||
    WIRING="$WIRING page_tick-sets-the-demo-scene-before-the-mood"
body "$MAINC" scene_start | grep -qxF '    if (!++s_scene_n) s_scene_n = 1;' || WIRING="$WIRING scene_start-numbers-the-scene"
body "$ECL" view_host | grep -qxF '    ember_host_lead_info(v->source, v->lead, v->hosts, v->lead_project, v->waiting, v->done, host);' ||
    WIRING="$WIRING view_host-fills-lead-and-count"
if [ -n "$WIRING" ]; then
    echo "run.sh: FAIL waiting restart wiring:$WIRING" >&2
    exit 1
fi
echo "waiting restart wiring: page shown, new lead or a higher count, demo scene number before the mood"
grep -A1 -F '    bot_view_set_options(s_ks_lv->source_label, s_ks_lv->working_ring);' "$MAINC" | grep -qxF '    bot_set_ring(&s_bot, s_ks_lv->working_ring);' ||
    WIRING="$WIRING bot-told-the-working-ring-setting"
fbody "$MAINC" page_tick | awk '/^    bot_rim_ready\(&s_bot, bot_view_rim_feed\(s_bot\.mood, s_bot\.asleep, bot_page\)\);$/ {r = NR} /^    f\.pose = bot_pose\(&s_bot, t\);$/ {p = NR}
    /^    bot_view_rim_pose\(&f\.pose, t\);$/ {s = NR} END {exit !(r && p == r + 1 && s == p + 1)}' ||
    WIRING="$WIRING page_tick-feeds-the-outline-before-the-pose"
if [ -n "$WIRING" ]; then
    echo "run.sh: FAIL mood beat wiring:$WIRING" >&2
    exit 1
fi
body "$MAINC" page_bot_frame | grep -qF 'if (pose && s_drawn && chase->chasing && p->face == s_last_pose.face && t - s_pose_drawn_at < ' &&
    body "$MAINC" page_bot_frame | grep -qxF '        bot_view_update(p, t);' || WIRING="$WIRING beat-frame-drawn-through-the-chase-throttle"
if [ -n "$WIRING" ]; then
    echo "run.sh: FAIL mood beat wiring:$WIRING" >&2
    exit 1
fi
echo "mood beat wiring: page_tick feeds the outline job right before the pose and syncs it right after, on every page; the beat frame draws through the chase throttle, with the frame time"
CFLAGS="-std=c11 -D_DEFAULT_SOURCE -DCJSON_NESTING_LIMIT=32 -Wall -Wextra -Werror -O1"
cc $CFLAGS "-DRJ_RACE_POINT(j,at)=rj_race_point(j,at)" "-DRS_RACE_POINT(s,at)=rs_race_point(s,at)" -I"$C/include" "$C/bot_behavior.c" "$C/bot_shape.c" "$C/bot_raster.c" "$C/ring_glint.c" "$C/arc_text.c" "$C/glint_chase.c" "$C/tool_marks.c" "$C/label_wipe.c" "$C/orbit_table.c" "$C/rim_job.c" "$C/rim_beat.c" "$C/rim_slots.c" "$C/offline_glyph.c" "$D/test_bot.c" -lm -lpthread -o "${OUT}_bot"
cc $CFLAGS -I"$C/include" "$C/bot_behavior.c" "$C/bot_shape.c" "$C/music_moment.c" "$D/test_music_moment.c" -lm -o "${OUT}_music"
cc $CFLAGS -I"$C/include" -I"$P/include" -I"$LS/include" "$C/bot_raster.c" "$P/pomo.c" "$LS/link_state.c" "$P/pomo_ring.c" "$D/test_pomo.c" -lm -o "${OUT}_pomo"
cc $CFLAGS -I"$C/include" -I"$W/include" "$W/weather_face.c" "$W/weather_scene.c" "$C/bot_raster.c" \
    "$D/test_weather.c" -lm -o "${OUT}_weather"
cc $CFLAGS -I"$W/include" "$W/weather_face.c" "$W/wx_temp.c" "$W/wx_shift.c" "$W/wx_rain.c" "$W/wx_paint.c" "$W/wx_faces.c" "$W/wx_cast.c" "$W/wx_wind.c" "$W/wx_days.c" "$W/wx_sun.c" "$W/wx_clock.c" "$W/wx_air.c" "$D/test_temp.c" -lm -o "${OUT}_temp"
cc $CFLAGS -I"$W/include" "$W/weather_face.c" "$W/wx_temp.c" "$W/wx_shift.c" "$W/wx_rain.c" "$W/wx_paint.c" "$W/wx_faces.c" "$W/wx_cast.c" "$W/wx_wind.c" "$W/wx_days.c" "$W/wx_sun.c" "$W/wx_clock.c" "$W/wx_air.c" "$D/test_cast.c" -lm -o "${OUT}_cast"
cc $CFLAGS -I"$W/include" "$W/weather_face.c" "$W/wx_temp.c" "$W/wx_shift.c" "$W/wx_rain.c" "$W/wx_paint.c" "$W/wx_faces.c" "$W/wx_cast.c" "$W/wx_wind.c" "$W/wx_days.c" "$W/wx_sun.c" "$W/wx_clock.c" "$W/wx_air.c" "$D/test_rain.c" -lm -o "${OUT}_rain"
cc $CFLAGS -I"$W/include" "$W/weather_face.c" "$W/wx_temp.c" "$W/wx_shift.c" "$W/wx_rain.c" "$W/wx_paint.c" "$W/wx_faces.c" "$W/wx_cast.c" "$W/wx_wind.c" "$W/wx_days.c" "$W/wx_sun.c" "$W/wx_clock.c" "$W/wx_air.c" "$D/test_faces.c" -lm -o "${OUT}_faces"
cc $CFLAGS -I"$W/include" "$W/weather_face.c" "$W/wx_temp.c" "$W/wx_shift.c" "$W/wx_rain.c" "$W/wx_paint.c" "$W/wx_faces.c" "$W/wx_cast.c" "$W/wx_wind.c" "$W/wx_days.c" "$W/wx_sun.c" "$W/wx_clock.c" "$W/wx_air.c" "$D/test_umbrella.c" -lm -o "${OUT}_umbrella"
cc $CFLAGS "-DWJ_RACE_POINT(j,at)=wj_race_point(j,at)" -I"$W/include" "$W/weather_face.c" "$W/wx_temp.c" "$W/wx_shift.c" "$W/wx_rain.c" "$W/wx_cast.c" "$W/wx_wind.c" "$W/wx_days.c" "$W/wx_sun.c" "$W/wx_clock.c" "$W/wx_air.c" "$W/wx_job.c" \
    "$D/test_wx_job.c" -lm -o "${OUT}_wx_job"
cc $CFLAGS -I"$W/include" "$W/weather_face.c" "$W/wx_temp.c" "$W/wx_shift.c" "$W/wx_rain.c" "$W/wx_paint.c" "$W/wx_faces.c" "$W/wx_cast.c" "$W/wx_wind.c" "$W/wx_days.c" "$W/wx_sun.c" "$W/wx_clock.c" "$W/wx_air.c" "$D/test_wind.c" -lm -o "${OUT}_wind"
cc $CFLAGS -I"$W/include" "$W/weather_face.c" "$W/wx_temp.c" "$W/wx_shift.c" "$W/wx_rain.c" "$W/wx_paint.c" "$W/wx_faces.c" "$W/wx_cast.c" "$W/wx_wind.c" "$W/wx_days.c" "$W/wx_sun.c" "$W/wx_clock.c" "$W/wx_air.c" "$D/test_days.c" -lm -o "${OUT}_days"
cc $CFLAGS -I"$W/include" "$W/weather_face.c" "$W/wx_temp.c" "$W/wx_shift.c" "$W/wx_rain.c" "$W/wx_paint.c" "$W/wx_faces.c" "$W/wx_cast.c" "$W/wx_wind.c" "$W/wx_days.c" "$W/wx_sun.c" "$W/wx_clock.c" "$W/wx_air.c" "$D/test_sun.c" -lm -o "${OUT}_sun"
cc $CFLAGS -I"$W/include" "$W/weather_face.c" "$W/wx_temp.c" "$W/wx_shift.c" "$W/wx_rain.c" "$W/wx_paint.c" "$W/wx_faces.c" "$W/wx_cast.c" "$W/wx_wind.c" "$W/wx_days.c" "$W/wx_sun.c" "$W/wx_clock.c" "$W/wx_air.c" "$D/test_air.c" -lm -o "${OUT}_air"
cc $CFLAGS -I"$C/include" -I"$W/include" "$C/bot_behavior.c" "$C/bot_shape.c" "$W/weather_face.c" "$W/wx_temp.c" "$W/wx_shift.c" "$W/wx_rain.c" "$W/wx_paint.c" "$W/wx_faces.c" \
    "$W/wx_cast.c" "$W/wx_wind.c" "$W/wx_days.c" "$W/wx_sun.c" "$W/wx_clock.c" "$W/wx_air.c" "$W/wx_moment.c" "$W/wx_moment_paint.c" "$D/test_moment.c" -lm -o "${OUT}_moment"
cc $CFLAGS -I"$W/include" -I"$D" -include "$D/moment_pixel.h" "$W/weather_face.c" "$W/wx_temp.c" "$W/wx_shift.c" "$W/wx_rain.c" "$W/wx_paint.c" "$W/wx_faces.c" "$W/wx_cast.c" "$W/wx_wind.c" "$W/wx_days.c" "$W/wx_sun.c" "$W/wx_clock.c" "$W/wx_air.c" \
    "$W/wx_moment.c" "$W/wx_moment_paint.c" "$D/test_moment_paint.c" -lm -o "${OUT}_moment_paint"
cc $CFLAGS -I"$C/include" -I"$W/include" "$C/bot_behavior.c" "$C/bot_shape.c" "$W/weather_face.c" "$W/wx_temp.c" "$W/wx_shift.c" "$W/wx_rain.c" "$W/wx_paint.c" "$W/wx_faces.c" \
    "$W/wx_cast.c" "$W/wx_wind.c" "$W/wx_days.c" "$W/wx_sun.c" "$W/wx_clock.c" "$W/wx_air.c" "$W/wx_moment.c" "$W/wx_moment_paint.c" "$D/test_cloud.c" -lm -o "${OUT}_cloud"
cc $CFLAGS -I"$H/include" -I"$M/include" "$H/ember_host.c" "$M/dim.c" "$D/test_ember.c" -o "${OUT}_ember"
cc $CFLAGS -I"$G/include" "$G/cfg.c" "$G/reset_gesture.c" "$D/test_cfg.c" -o "${OUT}_cfg"
cc $CFLAGS -I"$PR/include" "$PR/panel_req.c" "$D/test_panel.c" -o "${OUT}_panel"
cc $CFLAGS -I"$RT/include" "$RT/knob_rotation.c" "$D/test_rotation.c" -o "${OUT}_rotation"
cc $CFLAGS -I"$LS/include" "$LS/link_state.c" "$LS/join_label.c" "$D/test_link.c" -o "${OUT}_link"
cc $CFLAGS -I"$RS/include" "$RS/refr_stats.c" "$D/test_refr.c" -o "${OUT}_refr"
cc $CFLAGS -I"$G/include" "$G/touch_swipe.c" "$D/test_swipe.c" -lm -o "${OUT}_swipe"
cc $CFLAGS -I"$G/include" -I"$V/include" -I"$J" "$G/cfg.c" "$V/improv.c" "$V/cinder_line.c" "$J/cJSON.c" \
    "$D/test_provision.c" -lm -o "${OUT}_provision"
cc $CFLAGS -I"$RT/include" -I"$V/include" -I"$O/include" -I"$G/include" -I"$J" "$V/knob_settings.c" "$V/device_api.c" "$V/coredump_up.c" "$O/ota_policy.c" \
    "$RT/knob_rotation.c" "$J/cJSON.c" "$D/test_device.c" -lm -o "${OUT}_device"
cc $CFLAGS -I"$RT/include" -I"$V/include" -I"$O/include" -I"$J" "$V/device_api.c" "$V/coredump_up.c" "$O/ota_policy.c" "$RT/knob_rotation.c" "$J/cJSON.c" \
    "$D/test_coredump.c" -lm -o "${OUT}_coredump"
cc $CFLAGS -I"$RT/include" -I"$V/include" -I"$O/include" -I"$J" "$V/device_api.c" "$V/coredump_up.c" "$O/ota_policy.c" "$RT/knob_rotation.c" "$J/cJSON.c" \
    "$D/test_ota.c" -lm -o "${OUT}_ota"
cc $CFLAGS "-DLT_RACE_POINT(t)=lt_race_point(t)" -I"$N/include" "$N/fail_streak.c" "$N/http_retry.c" "$N/wifi_backoff.c" "$N/view_wait.c" "$N/legacy_task.c" "$D/test_net.c" -o "${OUT}_net"
K="$D/../../components/knob_view"
NP="$D/../../components/nowplaying"
cc $CFLAGS -I"$K/include" -I"$U/include" -I"$DM/include" -I"$C/include" -I"$P/include" -I"$LS/include" -I"$W/include" -I"$N/include" -I"$NP/include" -I"$H/include" -I"$J" \
    "$K/knob_view.c" "$U/usage.c" "$W/wx_clock.c" "$U/usage_draw.c" "$DM/demo.c" "$K/view_step.c" "$P/pomo.c" "$LS/link_state.c" "$W/weather_face.c" "$N/view_policy.c" "$NP/np.c" "$H/ember_host.c" "$J/cJSON.c" \
    "$D/test_view.c" -lm \
    -o "${OUT}_view"
cc $CFLAGS -I"$K/include" -I"$U/include" -I"$DM/include" -I"$C/include" -I"$P/include" -I"$LS/include" -I"$W/include" -I"$N/include" -I"$NP/include" -I"$J" \
    "$K/knob_view.c" "$U/usage.c" "$W/wx_clock.c" "$U/usage_draw.c" "$DM/demo.c" "$K/view_step.c" "$P/pomo.c" "$LS/link_state.c" "$W/weather_face.c" "$N/view_policy.c" "$NP/np.c" "$NP/np_draw.c" "$NP/np_ctl.c" "$NP/np_cover.c" "$NP/np_vinyl.c" "$J/cJSON.c" \
    "$D/test_demo.c" -lm -o "${OUT}_demo"
cc $CFLAGS -I"$K/include" -I"$U/include" -I"$DM/include" -I"$C/include" -I"$P/include" -I"$LS/include" -I"$W/include" -I"$NP/include" -I"$J" "$K/knob_view.c" "$U/usage.c" "$W/wx_clock.c" "$U/usage_draw.c" "$DM/demo.c" \
    "$P/pomo.c" "$LS/link_state.c" "$W/weather_face.c" "$NP/np.c" "$NP/np_draw.c" "$NP/np_ctl.c" "$NP/np_cover.c" "$NP/np_vinyl.c" "$J/cJSON.c" "$D/test_np.c" -lm -o "${OUT}_np"
cc $CFLAGS -I"$U/include" -I"$W/include" -I"$NP/include" -I"$J" "$U/usage.c" "$W/wx_clock.c" "$U/usage_draw.c" "$NP/np.c" "$J/cJSON.c" "$D/test_usage.c" -lm -o "${OUT}_usage"
cc $CFLAGS -I"$D/view_stubs" -I"$D/idf_stubs" -I"$D/../../main" -I"$U/include" -I"$N/include" -I"$P/include" -I"$LS/include" \
    "$D/../../main/usage_client.c" "$D/test_usage_client.c" -lpthread -o "${OUT}_usage_client"
"${OUT}_usage_client"
"${OUT}_usage"
LV="$D/../../managed_components/lvgl__lvgl"
TITLE_FONT=0
if [ -d "$LV" ]; then
    TITLE_FONT=1
    cc $CFLAGS -DLV_CONF_SKIP -DLV_FONT_MONTSERRAT_36=1 -I"$LV" -I"$D/../../main" "$LV/src/font/lv_font.c" \
        "$LV/src/font/fmt_txt/lv_font_fmt_txt.c" "$LV/src/font/lv_font_montserrat_36.c" "$D/test_title_font.c" \
        -o "${OUT}_title_font"
    cc $CFLAGS -DLV_CONF_SKIP -DLV_FONT_MONTSERRAT_36=1 -I"$LV" -I"$D/../../main" "$LV/src/font/lv_font.c" \
        "$LV/src/font/fmt_txt/lv_font_fmt_txt.c" "$LV/src/font/lv_font_montserrat_36.c" "$D/test_face_fonts.c" \
        -o "${OUT}_face_fonts"
    cc $CFLAGS -DLV_CONF_SKIP -DLV_FONT_MONTSERRAT_24=1 -DLV_FONT_MONTSERRAT_30=1 -I"$LV" -I"$W/include" "$LV/src/font/lv_font.c" \
        "$LV/src/font/fmt_txt/lv_font_fmt_txt.c" "$LV/src/font/lv_font_montserrat_24.c" "$LV/src/font/lv_font_montserrat_30.c" "$LV/src/misc/lv_area.c" "$LV/src/misc/lv_math.c" \
        "$W/weather_face.c" "$W/wx_rain.c" "$W/wx_cast.c" "$D/test_cast_label.c" -lm -o "${OUT}_cast_label"
    cc $CFLAGS -DLV_CONF_SKIP -DLV_FONT_MONTSERRAT_30=1 -I"$LV" -I"$W/include" "$LV/src/font/lv_font.c" \
        "$LV/src/font/fmt_txt/lv_font_fmt_txt.c" "$LV/src/font/lv_font_montserrat_30.c" "$W/weather_face.c" "$W/wx_wind.c" \
        "$D/test_wind_label.c" -lm -o "${OUT}_wind_label"
    cc $CFLAGS -DLV_CONF_SKIP -DLV_FONT_MONTSERRAT_24=1 -DLV_FONT_MONTSERRAT_30=1 -I"$LV" -I"$W/include" -I"$D/../../main" "$LV/src/font/lv_font.c" \
        "$LV/src/font/fmt_txt/lv_font_fmt_txt.c" "$LV/src/font/lv_font_montserrat_24.c" "$LV/src/font/lv_font_montserrat_30.c" \
        "$W/weather_face.c" "$W/wx_temp.c" "$W/wx_shift.c" "$W/wx_rain.c" "$W/wx_cast.c" "$W/wx_wind.c" "$W/wx_days.c" "$W/wx_sun.c" "$W/wx_clock.c" "$W/wx_air.c" "$D/test_days_label.c" -lm -o "${OUT}_days_label"
    cc $CFLAGS -DLV_CONF_SKIP -DLV_FONT_MONTSERRAT_30=1 -DLV_FONT_MONTSERRAT_36=1 -I"$LV" -I"$W/include" -I"$D/../../main" "$LV/src/font/lv_font.c" \
        "$LV/src/font/fmt_txt/lv_font_fmt_txt.c" "$LV/src/font/lv_font_montserrat_30.c" \
        "$W/weather_face.c" "$W/wx_temp.c" "$W/wx_shift.c" "$W/wx_rain.c" "$W/wx_cast.c" "$W/wx_wind.c" "$W/wx_days.c" "$W/wx_sun.c" "$W/wx_clock.c" "$W/wx_air.c" "$D/test_sun_label.c" -lm -o "${OUT}_sun_label"
    cc $CFLAGS -DLV_CONF_SKIP -DLV_FONT_MONTSERRAT_30=1 -DLV_FONT_MONTSERRAT_36=1 -I"$LV" -I"$W/include" -I"$D/../../main" "$LV/src/font/lv_font.c" \
        "$LV/src/font/fmt_txt/lv_font_fmt_txt.c" "$LV/src/font/lv_font_montserrat_30.c" \
        "$W/weather_face.c" "$W/wx_air.c" "$D/test_air_label.c" -lm -o "${OUT}_air_label"
    cc $CFLAGS -DLV_CONF_SKIP -DLV_FONT_MONTSERRAT_30=1 -I"$LV" -I"$W/include" "$LV/src/font/lv_font.c" \
        "$LV/src/font/fmt_txt/lv_font_fmt_txt.c" "$LV/src/font/lv_font_montserrat_30.c" "$W/weather_face.c" "$W/wx_temp.c" "$W/wx_shift.c" "$W/wx_rain.c" \
        "$W/wx_cast.c" "$W/wx_wind.c" "$W/wx_days.c" "$W/wx_sun.c" "$W/wx_clock.c" "$W/wx_air.c" "$W/wx_moment.c" "$D/test_moment_label.c" -lm -o "${OUT}_moment_label"
    LVDEF="-DLV_CONF_SKIP -DLV_FONT_MONTSERRAT_18=1 -DLV_FONT_MONTSERRAT_24=1 -DLV_FONT_MONTSERRAT_30=1 -DLV_FONT_MONTSERRAT_48=1 -DLV_USE_STDLIB_MALLOC=LV_STDLIB_CLIB"
    mkdir -p "$OUT_DIR/lv"
    find "$LV/src" -name '*.c' -print0 | LVDEF="$LVDEF" LV="$LV" LVO="$OUT_DIR/lv" xargs -0 -P 8 -n 1 \
        sh -c 'cc -std=c11 -O1 -w $LVDEF -I"$LV" -c "$1" -o "$LVO/$(printf "%s" "$1" | tr / _).o"' sh
    cc $CFLAGS $LVDEF -I"$D/view_stubs" -I"$LV" -I"$D/../../main" -I"$C/include" -I"$H/include" -I"$U/include" -I"$W/include" -I"$D/idf_stubs" "$D/../../main/bot_view.c" "$D/../../main/moment_view.c" "$U/usage_draw.c" "$W/wx_clock.c" \
        "$W/wx_moment.c" "$W/wx_moment_paint.c" "$W/weather_face.c" "$W/wx_temp.c" "$W/wx_shift.c" "$W/wx_rain.c" "$W/wx_paint.c" "$W/wx_faces.c" \
        "$W/wx_cast.c" "$W/wx_wind.c" "$W/wx_days.c" "$W/wx_sun.c" "$W/wx_air.c" \
        "$C/bot_behavior.c" "$C/bot_shape.c" "$C/bot_raster.c" "$C/ring_glint.c" "$C/arc_text.c" "$C/tool_marks.c" "$C/label_wipe.c" \
        "$C/rim_job.c" "$C/rim_beat.c" "$C/rim_slots.c" "$C/offline_glyph.c" "$H/ember_host.c" "$D/test_eye_view.c" "$OUT_DIR"/lv/*.o -lm -lpthread -o "${OUT}_eye_view"
    cc $CFLAGS $LVDEF -DHOST_HEAP_FAIL -DHOST_HEAP_FREED -DHOST_HEAP_ALLOCED -I"$D/view_stubs" -I"$D/idf_stubs" -I"$LV" -I"$D/../../main" -I"$NP/include" -I"$N/include" -I"$C/include" -I"$G/include" -I"$J" \
        "$C/arc_text.c" "$D/../../main/np_cover_view.c" "$D/../../main/nowplaying_view.c" "$D/../../main/font_digits_96.c" "$NP/np.c" "$NP/np_draw.c" "$NP/np_ctl.c" "$NP/np_cover.c" "$NP/np_vinyl.c" \
        "$G/touch_swipe.c" "$J/cJSON.c" "$D/test_np_view.c" "$OUT_DIR"/lv/*.o -lm -lpthread -o "${OUT}_np_view"
    cc $CFLAGS $LVDEF -DHOST_HEAP_FAIL -I"$D/view_stubs" -I"$D/idf_stubs" -I"$LV" -I"$D/../../main" -I"$C/include" -I"$U/include" -I"$W/include" -I"$N/include" -I"$P/include" -I"$LS/include" \
        -I"$NP/include" -I"$J" "$D/../../main/usage_view.c" "$D/../../main/font_digits_96.c" "$C/tool_marks.c" "$U/usage.c" "$W/wx_clock.c" "$U/usage_draw.c" \
        "$NP/np.c" "$J/cJSON.c" "$D/test_usage_view.c" "$OUT_DIR"/lv/*.o -lm -lpthread -o "${OUT}_usage_view"
    cc $CFLAGS $LVDEF -I"$D/view_stubs" -I"$D/idf_stubs" -I"$LV" -I"$D/../../main" -I"$C/include" "$D/../../main/music_view.c" "$C/music_moment.c" \
        "$D/test_music_view.c" $(ls "$OUT_DIR"/lv/*.o | grep -v 'lv_mem_core_clib\.c\.o$') -lm -lpthread -o "${OUT}_music_view"
fi
"${OUT}_bot"
"${OUT}_pomo"
"${OUT}_weather"
"${OUT}_temp"
"${OUT}_cloud"
"${OUT}_rain"
"${OUT}_cast"
"${OUT}_wind"
"${OUT}_days"
"${OUT}_sun"
"${OUT}_air"
"${OUT}_moment"
"${OUT}_moment_paint"
"${OUT}_music"
"${OUT}_faces"
"${OUT}_umbrella"
"${OUT}_wx_job"
"${OUT}_ember"
"${OUT}_cfg"
"${OUT}_panel"
"${OUT}_rotation"
"${OUT}_link"
"${OUT}_refr"
"${OUT}_swipe"
"${OUT}_provision" "$D/../vectors"
"${OUT}_device"
"${OUT}_coredump"
"${OUT}_ota" "$D/../vectors"
"${OUT}_net"
"${OUT}_view"
"${OUT}_demo" "$D/fixtures/demo" "$D/fixtures/ember/demo"
"${OUT}_np"
cc $CFLAGS -DHOST_HEAP_FAIL -DHOST_HEAP_FREED -I"$D/np_client_stubs" -I"$D/view_stubs" -I"$D/idf_stubs" -I"$D/../../main" -I"$N/include" -I"$G/include" -I"$V/include" \
    -I"$C/include" -I"$DM/include" -I"$H/include" -I"$LS/include" -I"$P/include" -I"$NP/include" -I"$K/include" -I"$U/include" -I"$W/include" -I"$O/include" -I"$RT/include" -I"$J" \
    "$D/../../main/nowplaying_client.c" "$NP/np.c" "$NP/np_draw.c" "$NP/np_ctl.c" "$NP/np_cover.c" "$NP/np_vinyl.c" "$J/cJSON.c" "$C/music_moment.c" "$D/test_np_client.c" \
    -lm -lpthread -o "${OUT}_np_client"
"${OUT}_np_client"
if [ "$TITLE_FONT" = 1 ]; then
    "${OUT}_title_font"
    "${OUT}_face_fonts"
    "${OUT}_cast_label"
    "${OUT}_wind_label"
    "${OUT}_days_label"
    "${OUT}_sun_label"
    "${OUT}_air_label"
    "${OUT}_moment_label"
    "${OUT}_eye_view" ${USAGE_VIEW_SHOTS:+"$USAGE_VIEW_SHOTS"}
    "${OUT}_np_view" ${NP_VIEW_SHOTS:+"$NP_VIEW_SHOTS"}
    "${OUT}_music_view" ${MUSIC_VIEW_SHOTS:+"$MUSIC_VIEW_SHOTS"}
    "${OUT}_usage_view" ${USAGE_VIEW_SHOTS:+"$USAGE_VIEW_SHOTS"}
else
    echo "run.sh: SKIPPED title and face font, forecast, wind, days, sun, air and moment label tests and the LVGL eye, now-playing, music moment and usage view tests: $LV missing; run idf.py reconfigure once" >&2
fi
python3 -m unittest discover -q -s "$D/../tools"
cc $CFLAGS -I"$O/include" -I"$J" "$O/ota_policy.c" "$J/cJSON.c" "$D/test_ota_rec.c" -lm -o "${OUT}_ota_rec"
"${OUT}_ota_rec"
cc $CFLAGS -I"$D/view_stubs" "$D/test_task_stub.c" -lpthread -o "${OUT}_task_stub"
"${OUT}_task_stub"
cc $CFLAGS -I"$K/include" -I"$U/include" -I"$DM/include" -I"$C/include" -I"$P/include" -I"$LS/include" -I"$W/include" -I"$NP/include" -I"$H/include" -I"$G/include" -I"$J" \
    "$K/knob_view.c" "$U/usage.c" "$W/wx_clock.c" "$U/usage_draw.c" "$DM/demo.c" "$K/ember_legacy.c" "$P/pomo.c" "$LS/link_state.c" "$P/pomo_legacy.c" "$W/weather_face.c" "$W/wx_legacy.c" "$NP/np.c" \
    "$H/ember_host.c" "$G/press_route.c" "$J/cJSON.c" "$D/test_legacy.c" -lm -o "${OUT}_legacy"
"${OUT}_legacy"
cc $CFLAGS -I"$RT/include" -I"$K/include" -I"$U/include" -I"$DM/include" -I"$C/include" -I"$P/include" -I"$LS/include" -I"$W/include" -I"$NP/include" -I"$V/include" -I"$O/include" \
    -I"$PG/include" -I"$G/include" -I"$CP/include" -I"$H/include" -I"$J" "$K/knob_view.c" "$U/usage.c" "$U/usage_draw.c" "$DM/demo.c" "$P/pomo.c" "$LS/link_state.c" "$P/pomo_legacy.c" \
    "$W/weather_face.c" "$W/wx_temp.c" "$W/wx_shift.c" "$W/wx_rain.c" "$W/wx_cast.c" "$W/wx_wind.c" "$W/wx_days.c" "$W/wx_sun.c" "$W/wx_clock.c" "$W/wx_air.c" "$NP/np.c" "$V/device_api.c" "$V/knob_settings.c" "$V/coredump_up.c" "$O/ota_policy.c" "$PG/pages.c" \
    "$CP/knob_caps.c" "$H/ember_host.c" "$G/touch_swipe.c" "$D/page_stubs.c" "$RT/knob_rotation.c" "$J/cJSON.c" "$D/test_fixtures.c" -lm -o "${OUT}_fixtures"
"${OUT}_fixtures" "$D/fixtures/ember"
cc $CFLAGS -I"$RT/include" -I"$K/include" -I"$U/include" -I"$DM/include" -I"$C/include" -I"$P/include" -I"$LS/include" -I"$W/include" -I"$NP/include" -I"$V/include" -I"$O/include" \
    -I"$PG/include" -I"$G/include" -I"$CP/include" -I"$J" "$V/device_api.c" "$V/knob_settings.c" "$V/coredump_up.c" \
    "$O/ota_policy.c" "$PG/pages.c" "$CP/knob_caps.c" "$G/touch_swipe.c" "$D/page_stubs.c" "$RT/knob_rotation.c" "$J/cJSON.c" "$D/test_caps.c" -lm \
    -o "${OUT}_caps"
"${OUT}_caps"
cc $CFLAGS -I"$RT/include" -I"$PG/include" -I"$V/include" -I"$G/include" -I"$J" "$PG/pages.c" "$G/touch_swipe.c" "$D/page_stubs.c" \
    "$V/knob_settings.c" "$RT/knob_rotation.c" "$J/cJSON.c" "$D/test_pages.c" -lm -o "${OUT}_pages"
"${OUT}_pages" "$D/fixtures/ember"
cc $CFLAGS -I"$D/idf_stubs" -I"$D/../../main" -I"$N/include" -I"$G/include" -I"$V/include" -I"$O/include" -I"$K/include" -I"$U/include" -I"$DM/include" \
    -I"$C/include" -I"$P/include" -I"$LS/include" -I"$W/include" -I"$NP/include" -I"$H/include" -I"$RT/include" -I"$J" \
    "$D/../../main/http_conn.c" "$N/fail_streak.c" "$N/http_retry.c" "$K/ember_legacy.c" "$K/knob_view.c" "$U/usage.c" "$W/wx_clock.c" "$U/usage_draw.c" "$DM/demo.c" "$P/pomo.c" \
    "$W/weather_face.c" "$NP/np.c" "$H/ember_host.c" "$LS/link_state.c" "$V/device_api.c" "$V/knob_settings.c" "$V/coredump_up.c" \
    "$O/ota_policy.c" "$RT/knob_rotation.c" "$J/cJSON.c" "$D/test_http.c" -lm \
    -o "${OUT}_http"
"${OUT}_http"
