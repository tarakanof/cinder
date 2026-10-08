cmake_minimum_required(VERSION 3.16)
# Builds are secret-free. -D CINDER_SEED_SECRETS=1 layers the git-ignored sdkconfig.secrets
# (opt-in dev seed: Wi-Fi and Ember URL, never a token) over the committed defaults.
if(CINDER_SEED_SECRETS AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.secrets")
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.secrets")
endif()
# Firmware version: Improv device info, CINDER1 info/boot, Ember checkin, OTA identity.
# -D CINDER_VERSION=X.Y.Z overrides it for OTA test images (tools/build_release.sh --version).
if(CINDER_VERSION)
    set(PROJECT_VER "${CINDER_VERSION}")
else()
    set(PROJECT_VER "0.9.35")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# LVGL joins only overlapping invalid areas and falls back to a full-screen redraw past
# LV_INV_BUF_SIZE (default 32). Set for every component: it sizes a struct in
# lv_display_private.h that the display adapter also includes (#20).
idf_build_set_property(COMPILE_DEFINITIONS "LV_INV_BUF_SIZE=64" APPEND)
# Bounds cJSON's parse recursion (default 1000 levels); Ember's deepest document is a few levels.
idf_build_set_property(COMPILE_DEFINITIONS "CJSON_NESTING_LIMIT=32" APPEND)
project(cinder)
