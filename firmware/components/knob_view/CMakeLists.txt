idf_component_register(SRCS "knob_view.c" "ember_legacy.c" "view_step.c"
                       INCLUDE_DIRS "include"
                       REQUIRES json bot pomo weather nowplaying ember_host net_policy)
