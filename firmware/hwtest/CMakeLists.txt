cmake_minimum_required(VERSION 3.16)
# Hardware test image for the J2 header (docs/hardware-test.md). Never published or released:
# build it into its own directory with idf.py -C hwtest -B build-hwtest.
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/../sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../components/bsp_knob"
    "${CMAKE_CURRENT_LIST_DIR}/../components/rotation"
    "${CMAKE_CURRENT_LIST_DIR}/../components/sens"
    "${CMAKE_CURRENT_LIST_DIR}/../components/tone"
    "${CMAKE_CURRENT_LIST_DIR}/../components/j2")
set(PROJECT_VER "hwtest-1")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(COMPILE_DEFINITIONS "LV_INV_BUF_SIZE=64" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "CJSON_NESTING_LIMIT=32" APPEND)
project(cinder_hwtest)
