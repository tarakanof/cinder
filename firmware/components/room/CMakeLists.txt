idf_component_register(SRCS "room_report.c" "room_face.c" "room_moment.c"
                       INCLUDE_DIRS "include"
                       REQUIRES sens provision weather)

target_compile_options(${COMPONENT_LIB} PRIVATE -ffp-contract=off)
