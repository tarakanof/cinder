idf_component_register(SRCS "main.c" "input_route.c" "link_task.c" "stats_task.c" "chase_ctl.c" "moments.c" "scene.c" "page_fx.c" "overlays.c" "pomo_page.c" "controls_page.c" "np_page.c" "panel_check.c" "bot_view.c" "ember_client.c" "dim_ctl.c" "pomo_client.c" "pomo_view.c" "day_client.c" "day_view.c" "controls_client.c" "controls_view.c"
                            "weather_client.c" "temp_face.c" "config_store.c" "provision_usb.c" "http_conn.c" "diag.c"
                            "nowplaying_client.c" "nowplaying_view.c" "np_cover_view.c" "screen_snap.c" "ota_client.c" "ota_face.c" "font_title_bold.c"
                            "font_digits_60.c" "font_digits_78.c" "font_digits_96.c" "font_face_36.c" "moment_view.c" "music_view.c" "usage_client.c" "usage_view.c" "join_view.c" "claim_client.c" "timing.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_tsens page_canvas day controls shown wake_turn usage refr_stats span_stats demo ota_policy pages caps link_state panel_req app_update mbedtls net_policy knob_view nowplaying bot pomo weather ember_host dim cinder_cfg provision bsp_knob esp_driver_usb_serial_jtag esp_app_format esp_wifi esp_netif esp_event nvs_flash esp_http_client esp_timer json espcoredump esp_partition)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    if(NOT CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE OR CONFIG_BOOTLOADER_PROJECT_VER LESS 2)
        message(FATAL_ERROR "sdkconfig has no app rollback (BOOTLOADER_APP_ROLLBACK_ENABLE, BOOTLOADER_PROJECT_VER >= 2): "
                            "it predates OTA. Delete firmware/sdkconfig (docs/workflow.md) to regenerate it.")
    endif()
    if(CONFIG_CINDER_DEV_SEED AND NOT CINDER_SEED_SECRETS)
        message(FATAL_ERROR "sdkconfig has CONFIG_CINDER_DEV_SEED=y without -D CINDER_SEED_SECRETS=1: a stale sdkconfig "
                            "would compile Wi-Fi secrets in. Delete firmware/sdkconfig (docs/workflow.md) to regenerate it.")
    endif()
endif()

set_source_files_properties(temp_face.c PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
