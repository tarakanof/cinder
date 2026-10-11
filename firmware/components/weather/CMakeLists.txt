idf_component_register(SRCS "weather_face.c" "weather_scene.c" "wx_legacy.c" "wx_temp.c" "wx_paint.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES json)
