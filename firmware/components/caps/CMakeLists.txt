idf_component_register(SRCS "knob_caps.c"
                       INCLUDE_DIRS "include"
                       REQUIRES pages provision knob_view cinder_cfg rotation)

if(CONFIG_CINDER_ROOM)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_CINDER_ROOM=1)
endif()
