idf_component_register(
    SRCS "bsp_knob_15_md50et.c"
    INCLUDE_DIRS "include"
    REQUIRES
        driver
        esp_lcd
        esp_timer
        esp_lcd_co5300
        esp_lvgl_adapter
        lvgl
        knob
        button
        esp_lcd_touch
        # Managed: viewesmart/esp_lcd_touch_cst820
        viewesmart__esp_lcd_touch_cst820
)
