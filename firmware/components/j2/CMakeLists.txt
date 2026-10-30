idf_component_register(SRCS "j2_pins.c" "j2_i2c.c" "j2_dev.c" "j2_room.c" "j2_amp.c" "j2_radar.c"
                       INCLUDE_DIRS "include"
                       REQUIRES sens esp_driver_gpio
                       PRIV_REQUIRES tone esp_driver_i2c esp_driver_i2s esp_driver_uart esp_timer)
