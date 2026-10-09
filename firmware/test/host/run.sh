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
IDF="${IDF_PATH:?source the ESP-IDF export script first (IDF_PATH)}"
J="$IDF/components/json/cJSON"
OUT_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cinder_test.XXXXXX")"
trap 'rm -rf "$OUT_DIR"' EXIT
OUT="$OUT_DIR/t"
CFLAGS="-std=c11 -D_DEFAULT_SOURCE -DCJSON_NESTING_LIMIT=32 -Wall -Wextra -Werror -O1"
cc $CFLAGS -I"$C/include" "$C/bot_behavior.c" "$C/bot_shape.c" "$C/bot_raster.c" "$C/ring_glint.c" "$C/arc_text.c" "$C/glint_chase.c" "$C/tool_marks.c" "$C/label_wipe.c" "$C/orbit_table.c" "$D/test_bot.c" -lm -o "${OUT}_bot"
cc $CFLAGS -I"$C/include" -I"$P/include" "$C/bot_raster.c" "$P/pomo.c" "$P/pomo_ring.c" "$D/test_pomo.c" -lm -o "${OUT}_pomo"
cc $CFLAGS -I"$C/include" -I"$W/include" "$W/weather_face.c" "$W/weather_scene.c" "$C/bot_raster.c" \
    "$D/test_weather.c" -lm -o "${OUT}_weather"
cc $CFLAGS -I"$H/include" -I"$M/include" "$H/ember_host.c" "$M/dim.c" "$D/test_ember.c" -o "${OUT}_ember"
cc $CFLAGS -I"$G/include" "$G/cfg.c" "$G/reset_gesture.c" "$D/test_cfg.c" -o "${OUT}_cfg"
cc $CFLAGS -I"$PR/include" "$PR/panel_req.c" "$D/test_panel.c" -o "${OUT}_panel"
cc $CFLAGS -I"$G/include" "$G/touch_swipe.c" "$D/test_swipe.c" -lm -o "${OUT}_swipe"
cc $CFLAGS -I"$G/include" -I"$V/include" -I"$J" "$G/cfg.c" "$V/improv.c" "$V/cinder_line.c" "$J/cJSON.c" \
    "$D/test_provision.c" -lm -o "${OUT}_provision"
cc $CFLAGS -I"$V/include" -I"$O/include" -I"$J" "$V/knob_settings.c" "$V/device_api.c" "$V/coredump_up.c" "$O/ota_policy.c" \
    "$J/cJSON.c" "$D/test_device.c" -lm -o "${OUT}_device"
cc $CFLAGS -I"$V/include" -I"$O/include" -I"$J" "$V/device_api.c" "$V/coredump_up.c" "$O/ota_policy.c" "$J/cJSON.c" \
    "$D/test_coredump.c" -lm -o "${OUT}_coredump"
cc $CFLAGS -I"$V/include" -I"$O/include" -I"$J" "$V/device_api.c" "$V/coredump_up.c" "$O/ota_policy.c" "$J/cJSON.c" \
    "$D/test_ota.c" -lm -o "${OUT}_ota"
cc $CFLAGS "-DLT_RACE_POINT(t)=lt_race_point(t)" -I"$N/include" "$N/fail_streak.c" "$N/http_retry.c" "$N/wifi_backoff.c" "$N/view_wait.c" "$N/legacy_task.c" "$D/test_net.c" -o "${OUT}_net"
K="$D/../../components/knob_view"
NP="$D/../../components/nowplaying"
cc $CFLAGS -I"$K/include" -I"$C/include" -I"$P/include" -I"$W/include" -I"$N/include" -I"$NP/include" -I"$J" \
    "$K/knob_view.c" "$P/pomo.c" "$W/weather_face.c" "$N/view_policy.c" "$NP/np.c" "$J/cJSON.c" "$D/test_view.c" -lm \
    -o "${OUT}_view"
cc $CFLAGS -I"$K/include" -I"$C/include" -I"$P/include" -I"$W/include" -I"$NP/include" -I"$J" "$K/knob_view.c" \
    "$P/pomo.c" "$W/weather_face.c" "$NP/np.c" "$NP/np_draw.c" "$NP/np_ctl.c" "$J/cJSON.c" "$D/test_np.c" -lm -o "${OUT}_np"
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
"${OUT}_ember"
"${OUT}_cfg"
"${OUT}_panel"
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
cc $CFLAGS -I"$K/include" -I"$C/include" -I"$P/include" -I"$W/include" -I"$NP/include" -I"$H/include" -I"$G/include" -I"$J" \
    "$K/knob_view.c" "$K/ember_legacy.c" "$P/pomo.c" "$P/pomo_legacy.c" "$W/weather_face.c" "$W/wx_legacy.c" "$NP/np.c" \
    "$H/ember_host.c" "$G/press_route.c" "$J/cJSON.c" "$D/test_legacy.c" -lm -o "${OUT}_legacy"
"${OUT}_legacy"
cc $CFLAGS -I"$K/include" -I"$C/include" -I"$P/include" -I"$W/include" -I"$NP/include" -I"$V/include" -I"$O/include" \
    -I"$J" "$K/knob_view.c" "$P/pomo.c" "$P/pomo_legacy.c" "$W/weather_face.c" "$NP/np.c" "$V/device_api.c" \
    "$V/knob_settings.c" "$V/coredump_up.c" "$O/ota_policy.c" "$J/cJSON.c" "$D/test_fixtures.c" -lm -o "${OUT}_fixtures"
"${OUT}_fixtures" "$D/fixtures/ember"
