idf_component_register(SRCS "improv.c" "cinder_line.c" "knob_settings.c" "device_api.c" "coredump_up.c"
                       INCLUDE_DIRS "include"
                       REQUIRES json cinder_cfg ota_policy rotation)
