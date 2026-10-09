idf_component_register(SRCS "pomo.c" "pomo_ring.c" "pomo_legacy.c"
                       INCLUDE_DIRS "include"
                       REQUIRES bot link_state
                       PRIV_REQUIRES json)
