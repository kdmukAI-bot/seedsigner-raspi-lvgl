cmake_minimum_required(VERSION 3.16)
project(screen_core_sanity LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED SEEDSIGNER_C_MODULES_DIR OR SEEDSIGNER_C_MODULES_DIR STREQUAL "")
  message(FATAL_ERROR "SEEDSIGNER_C_MODULES_DIR is required")
endif()

if(NOT DEFINED LVGL_ROOT OR LVGL_ROOT STREQUAL "")
  set(LVGL_ROOT "${SEEDSIGNER_C_MODULES_DIR}/../micropython/ports/esp32/managed_components/lvgl__lvgl")
endif()

set(SEEDSIGNER_DIR "${SEEDSIGNER_C_MODULES_DIR}/components/seedsigner")
set(NLOHMANN_JSON_INCLUDE_DIR "${SEEDSIGNER_C_MODULES_DIR}/components/nlohmann_json/include")

if(NOT EXISTS "${SEEDSIGNER_DIR}/seedsigner.cpp")
  message(FATAL_ERROR "seedsigner.cpp not found under ${SEEDSIGNER_DIR}")
endif()

if(NOT EXISTS "${LVGL_ROOT}/lvgl.h")
  message(FATAL_ERROR "LVGL root not found at ${LVGL_ROOT}. Pass -DLVGL_ROOT=<path>")
endif()

file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS
  "${LVGL_ROOT}/src/*.c"
)

set(SEEDSIGNER_FONT_SOURCES
  "${SEEDSIGNER_DIR}/fonts/opensans_regular_17_4bpp.c"
  "${SEEDSIGNER_DIR}/fonts/opensans_semibold_18_4bpp.c"
  "${SEEDSIGNER_DIR}/fonts/opensans_semibold_20_4bpp.c"
  "${SEEDSIGNER_DIR}/fonts/seedsigner_icons_24_4bpp.c"
  "${SEEDSIGNER_DIR}/fonts/opensans_regular_17_4bpp_125x.c"
  "${SEEDSIGNER_DIR}/fonts/opensans_semibold_18_4bpp_125x.c"
  "${SEEDSIGNER_DIR}/fonts/opensans_semibold_20_4bpp_125x.c"
  "${SEEDSIGNER_DIR}/fonts/seedsigner_icons_24_4bpp_125x.c"
  "${SEEDSIGNER_DIR}/fonts/opensans_regular_17_4bpp_150x.c"
  "${SEEDSIGNER_DIR}/fonts/opensans_semibold_18_4bpp_150x.c"
  "${SEEDSIGNER_DIR}/fonts/opensans_semibold_20_4bpp_150x.c"
  "${SEEDSIGNER_DIR}/fonts/seedsigner_icons_24_4bpp_150x.c"
  "${SEEDSIGNER_DIR}/fonts/seedsigner_icons_36_4bpp.c"
  "${SEEDSIGNER_DIR}/fonts/seedsigner_icons_36_4bpp_125x.c"
  "${SEEDSIGNER_DIR}/fonts/seedsigner_icons_36_4bpp_150x.c"
)

set(SEEDSIGNER_CORE_SOURCES
  "${SEEDSIGNER_DIR}/components.cpp"
  "${SEEDSIGNER_DIR}/seedsigner.cpp"
)

add_executable(screen_core_sanity
  ${LVGL_SOURCES}
  ${SEEDSIGNER_FONT_SOURCES}
  ${SEEDSIGNER_CORE_SOURCES}
  "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
)

target_compile_definitions(screen_core_sanity PRIVATE LV_CONF_SKIP)

target_include_directories(screen_core_sanity PRIVATE
  "${LVGL_ROOT}"
  "${SEEDSIGNER_DIR}"
  "${NLOHMANN_JSON_INCLUDE_DIR}"
)
