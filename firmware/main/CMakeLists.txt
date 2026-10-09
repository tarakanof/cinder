idf_component_register(SRCS "main.c" "panel_check.c" "bot_view.c" "ember_client.c" "pomo_client.c" "pomo_view.c"
                            "weather_client.c" "weather_view.c" "config_store.c" "provision_usb.c" "http_conn.c" "diag.c"
                            "nowplaying_client.c" "nowplaying_view.c" "screen_snap.c" "ota_client.c" "ota_face.c" "font_title_bold.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_tsens ota_policy pages panel_req app_update mbedtls net_policy knob_view nowplaying bot pomo weather ember_host dim cinder_cfg provision bsp_knob esp_driver_usb_serial_jtag esp_app_format esp_wifi esp_netif esp_event nvs_flash esp_http_client esp_timer json espcoredump esp_partition)

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
