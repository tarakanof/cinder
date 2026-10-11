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
body() { awk -v f="$2" '$0 ~ "^(static )?[a-z_][a-z_0-9 ]*[ *]" f "\\(" {on=1} on {print} on && /^}/ {exit}' "$1"; }
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
WXC="$D/../../main/weather_client.c"
body "$WXC" weather_client_get | awk '/xSemaphoreTake\(s_lock/ {lock=1} /xSemaphoreGive\(s_lock/ {lock=0} /esp_timer_get_time\(/ {t++; if (!lock) bad=1} END {exit !(t == 1 && !bad)}' ||
    WIRING="$WIRING weather_client_get-reads-the-timer-under-the-lock"
if [ -n "$WIRING" ]; then
    echo "run.sh: FAIL weather wiring:$WIRING" >&2
    exit 1
fi
echo "weather wiring: weather_client_get reads the timer under the lock"
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
body "$TFC" temp_face_update | grep -qF 'if (wx_shift_tick(&s_shift, dt)) shift_root();' || WIRING="$WIRING update-steps-the-burn-in-shift"
body "$TFC" shift_root | grep -qF 'lv_obj_set_style_translate_y(s_root, dy, 0);' || WIRING="$WIRING shift-moves-the-root"
[ "$(grep -c 'wx_bake(' "$TFC")" -eq 1 ] && body "$TFC" bake_task | grep -qF 'wx_bake(t, &s_buf, &s_io.out);' ||
    WIRING="$WIRING wx_bake-only-in-bake_task"
grep -qE 'wx_paint_(clear|ring|rays|stars|thermo|hero|clouds)\(' "$TFC" && WIRING="$WIRING no-bake-pass-on-the-LVGL-task"
body "$TFC" bake_task | grep -qF 'const wx_temp_t *t = wx_job_take(&s_job);' || WIRING="$WIRING bake_task-takes-through-wx_job"
body "$TFC" bake_task | grep -qF 'if (!t) continue;' || WIRING="$WIRING bake_task-skips-an-empty-take"
body "$TFC" bake_task | grep -qF 'wx_job_done(&s_job, ' || WIRING="$WIRING bake_task-publishes-through-wx_job"
grep -qE 'atomic_|JOB_(IDLE|BUSY|DONE)' "$TFC" && WIRING="$WIRING job-state-only-in-wx_job"
grep -qF 'static wx_job_t s_job;' "$TFC" || WIRING="$WIRING job-state-in-internal-RAM"
body "$TFC" temp_face_show | grep -qF 'act(wx_job_show(&s_job, on));' || WIRING="$WIRING show-through-wx_job"
body "$TFC" temp_face_hold | grep -qF 'act(wx_job_hold(&s_job, hold));' || WIRING="$WIRING hold-through-wx_job"
body "$TFC" temp_face_update | grep -qF 'int a = wx_job_step(&s_job, &t, dt);' || WIRING="$WIRING update-steps-wx_job"
body "$TFC" temp_face_update | grep -qF 'if (!(a & WX_DO_PAINT)) return;' || WIRING="$WIRING update-paints-only-on-WX_DO_PAINT"
for ACT in 'if (a & WX_DO_RELEASE) release();' 'wx_job_acquired(&s_job, ok);' 'if (a & WX_DO_DETACH) detach();' \
    'if (a & WX_DO_BAKE) xTaskNotifyGive(s_task);' 'if (a & WX_DO_APPLY) apply(&s_io.key, &s_io.out);'; do
    body "$TFC" act | grep -qF "$ACT" || WIRING="$WIRING act:$ACT"
done
[ "$(grep -c 'xTaskNotifyGive(' "$TFC")" -eq 1 ] || WIRING="$WIRING notify-only-in-act"
body "$TFC" detach | grep -qF 'show_obj(s_root, false);' || WIRING="$WIRING detach-hides-the-face"
for CV in s_bg s_hero s_still s_drift; do
    body "$TFC" detach | grep -qF "lv_canvas_set_buffer($CV, s_none, " || WIRING="$WIRING detach-$CV"
done
body "$TFC" release | grep -qF 'detach();' && body "$TFC" release | grep -qF 'wx_job_released(&s_job);' ||
    WIRING="$WIRING release-detaches-and-tells-wx_job"
grep -qF 'lv_canvas_set_buffer(s_bg, s_buf.bg, ' "$TFC" && [ "$(grep -c 'lv_canvas_set_buffer(s_bg, s_buf.bg, ' "$TFC")" -eq 1 ] &&
    body "$TFC" apply | grep -qF 'lv_canvas_set_buffer(s_bg, s_buf.bg, ' || WIRING="$WIRING bg-attached-only-on-apply"
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
echo "temp face wiring: burn-in shift on the root, bake and job state through wx_job, detach on re-bake, OTA hold"
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
body "$BVC" rim_task | grep -q 'if (!rim_job_take(&s_rim, .*)) continue;' || WIRING="$WIRING rim_task-skips-a-held-take"
body "$BVC" rim_task | grep -qF 'if (!rim_job_live(&s_rim, gen)) break;' || WIRING="$WIRING rim_task-stops-a-dead-job"
if [ -n "$WIRING" ]; then
    echo "run.sh: FAIL rim wiring:$WIRING" >&2
    exit 1
fi
echo "rim wiring: rim_task skips a held take and stops a dead job"
CFLAGS="-std=c11 -D_DEFAULT_SOURCE -DCJSON_NESTING_LIMIT=32 -Wall -Wextra -Werror -O1"
cc $CFLAGS "-DRJ_RACE_POINT(j,at)=rj_race_point(j,at)" -I"$C/include" "$C/bot_behavior.c" "$C/bot_shape.c" "$C/bot_raster.c" "$C/ring_glint.c" "$C/arc_text.c" "$C/glint_chase.c" "$C/tool_marks.c" "$C/label_wipe.c" "$C/orbit_table.c" "$C/rim_job.c" "$D/test_bot.c" -lm -o "${OUT}_bot"
cc $CFLAGS -I"$C/include" -I"$P/include" -I"$LS/include" "$C/bot_raster.c" "$P/pomo.c" "$LS/link_state.c" "$P/pomo_ring.c" "$D/test_pomo.c" -lm -o "${OUT}_pomo"
cc $CFLAGS -I"$C/include" -I"$W/include" "$W/weather_face.c" "$W/weather_scene.c" "$C/bot_raster.c" \
    "$D/test_weather.c" -lm -o "${OUT}_weather"
cc $CFLAGS -I"$W/include" "$W/weather_face.c" "$W/wx_temp.c" "$W/wx_paint.c" "$D/test_temp.c" -lm -o "${OUT}_temp"
cc $CFLAGS "-DWJ_RACE_POINT(j,at)=wj_race_point(j,at)" -I"$W/include" "$W/weather_face.c" "$W/wx_temp.c" "$W/wx_job.c" \
    "$D/test_wx_job.c" -lm -o "${OUT}_wx_job"
cc $CFLAGS -I"$H/include" -I"$M/include" "$H/ember_host.c" "$M/dim.c" "$D/test_ember.c" -o "${OUT}_ember"
cc $CFLAGS -I"$G/include" "$G/cfg.c" "$G/reset_gesture.c" "$D/test_cfg.c" -o "${OUT}_cfg"
cc $CFLAGS -I"$PR/include" "$PR/panel_req.c" "$D/test_panel.c" -o "${OUT}_panel"
cc $CFLAGS -I"$RT/include" "$RT/knob_rotation.c" "$D/test_rotation.c" -o "${OUT}_rotation"
cc $CFLAGS -I"$LS/include" "$LS/link_state.c" "$D/test_link.c" -o "${OUT}_link"
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
cc $CFLAGS -I"$K/include" -I"$C/include" -I"$P/include" -I"$LS/include" -I"$W/include" -I"$N/include" -I"$NP/include" -I"$J" \
    "$K/knob_view.c" "$K/view_step.c" "$P/pomo.c" "$LS/link_state.c" "$W/weather_face.c" "$N/view_policy.c" "$NP/np.c" "$J/cJSON.c" \
    "$D/test_view.c" -lm \
    -o "${OUT}_view"
cc $CFLAGS -I"$K/include" -I"$C/include" -I"$P/include" -I"$LS/include" -I"$W/include" -I"$NP/include" -I"$J" "$K/knob_view.c" \
    "$P/pomo.c" "$LS/link_state.c" "$W/weather_face.c" "$NP/np.c" "$NP/np_draw.c" "$NP/np_ctl.c" "$J/cJSON.c" "$D/test_np.c" -lm -o "${OUT}_np"
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
fi
"${OUT}_bot"
"${OUT}_pomo"
"${OUT}_weather"
"${OUT}_temp"
"${OUT}_wx_job"
"${OUT}_ember"
"${OUT}_cfg"
"${OUT}_panel"
"${OUT}_rotation"
"${OUT}_link"
"${OUT}_swipe"
"${OUT}_provision" "$D/../vectors"
"${OUT}_device"
"${OUT}_coredump"
"${OUT}_ota" "$D/../vectors"
"${OUT}_net"
"${OUT}_view"
"${OUT}_np"
if [ "$TITLE_FONT" = 1 ]; then
    "${OUT}_title_font"
    "${OUT}_face_fonts"
else
    echo "run.sh: SKIPPED title and face font tests: $LV missing; run idf.py reconfigure once" >&2
fi
python3 -m unittest discover -q -s "$D/../tools"
cc $CFLAGS -I"$O/include" -I"$J" "$O/ota_policy.c" "$J/cJSON.c" "$D/test_ota_rec.c" -lm -o "${OUT}_ota_rec"
"${OUT}_ota_rec"
cc $CFLAGS -I"$K/include" -I"$C/include" -I"$P/include" -I"$LS/include" -I"$W/include" -I"$NP/include" -I"$H/include" -I"$G/include" -I"$J" \
    "$K/knob_view.c" "$K/ember_legacy.c" "$P/pomo.c" "$LS/link_state.c" "$P/pomo_legacy.c" "$W/weather_face.c" "$W/wx_legacy.c" "$NP/np.c" \
    "$H/ember_host.c" "$G/press_route.c" "$J/cJSON.c" "$D/test_legacy.c" -lm -o "${OUT}_legacy"
"${OUT}_legacy"
cc $CFLAGS -I"$RT/include" -I"$K/include" -I"$C/include" -I"$P/include" -I"$LS/include" -I"$W/include" -I"$NP/include" -I"$V/include" -I"$O/include" \
    -I"$PG/include" -I"$G/include" -I"$CP/include" -I"$J" "$K/knob_view.c" "$P/pomo.c" "$LS/link_state.c" "$P/pomo_legacy.c" \
    "$W/weather_face.c" "$W/wx_temp.c" "$NP/np.c" "$V/device_api.c" "$V/knob_settings.c" "$V/coredump_up.c" "$O/ota_policy.c" "$PG/pages.c" \
    "$CP/knob_caps.c" "$G/touch_swipe.c" "$D/page_stubs.c" "$RT/knob_rotation.c" "$J/cJSON.c" "$D/test_fixtures.c" -lm -o "${OUT}_fixtures"
"${OUT}_fixtures" "$D/fixtures/ember"
cc $CFLAGS -I"$RT/include" -I"$K/include" -I"$C/include" -I"$P/include" -I"$LS/include" -I"$W/include" -I"$NP/include" -I"$V/include" -I"$O/include" \
    -I"$PG/include" -I"$G/include" -I"$CP/include" -I"$J" "$V/device_api.c" "$V/knob_settings.c" "$V/coredump_up.c" \
    "$O/ota_policy.c" "$PG/pages.c" "$CP/knob_caps.c" "$G/touch_swipe.c" "$D/page_stubs.c" "$RT/knob_rotation.c" "$J/cJSON.c" "$D/test_caps.c" -lm \
    -o "${OUT}_caps"
"${OUT}_caps"
cc $CFLAGS -I"$RT/include" -I"$PG/include" -I"$V/include" -I"$G/include" -I"$J" "$PG/pages.c" "$G/touch_swipe.c" "$D/page_stubs.c" \
    "$V/knob_settings.c" "$RT/knob_rotation.c" "$J/cJSON.c" "$D/test_pages.c" -lm -o "${OUT}_pages"
"${OUT}_pages" "$D/fixtures/ember"
cc $CFLAGS -I"$D/idf_stubs" -I"$D/../../main" -I"$N/include" -I"$G/include" -I"$V/include" -I"$O/include" -I"$K/include" \
    -I"$C/include" -I"$P/include" -I"$LS/include" -I"$W/include" -I"$NP/include" -I"$H/include" -I"$RT/include" -I"$J" \
    "$D/../../main/http_conn.c" "$N/fail_streak.c" "$N/http_retry.c" "$K/ember_legacy.c" "$K/knob_view.c" "$P/pomo.c" \
    "$W/weather_face.c" "$NP/np.c" "$H/ember_host.c" "$LS/link_state.c" "$V/device_api.c" "$V/knob_settings.c" "$V/coredump_up.c" \
    "$O/ota_policy.c" "$RT/knob_rotation.c" "$J/cJSON.c" "$D/test_http.c" -lm \
    -o "${OUT}_http"
"${OUT}_http"
