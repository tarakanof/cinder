idf_component_register(SRCS "hwtest.c" "hwt_line.c" "hwt_ui.c"
                       INCLUDE_DIRS "."
                       REQUIRES j2 sens tone bsp_knob json esp_driver_usb_serial_jtag esp_driver_gpio esp_timer esp_app_format)
