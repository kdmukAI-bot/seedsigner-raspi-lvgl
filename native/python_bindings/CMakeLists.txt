cmake_minimum_required(VERSION 3.16)
project(seedsigner_lvgl_native_bindings LANGUAGES C)

option(STAGEC_NO_PYTHON "Build placeholder shared object without CPython dependency" OFF)

if(NOT DEFINED SEEDSIGNER_C_MODULES_DIR OR SEEDSIGNER_C_MODULES_DIR STREQUAL "")
  set(SEEDSIGNER_C_MODULES_DIR "")
endif()

if(NOT DEFINED LVGL_ROOT OR LVGL_ROOT STREQUAL "")
  set(LVGL_ROOT "")
endif()

if(STAGEC_NO_PYTHON)
  add_library(seedsigner_lvgl_native MODULE module_stub.c)
  set_target_properties(seedsigner_lvgl_native PROPERTIES
    PREFIX ""
    OUTPUT_NAME "seedsigner_lvgl_native"
  )
  target_compile_definitions(seedsigner_lvgl_native PRIVATE
    SEEDSIGNER_STAGEC_PLACEHOLDER_BACKEND=1
    SEEDSIGNER_STAGEC_NO_PYTHON=1
  )
else()
  find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)

  add_library(seedsigner_lvgl_native MODULE module.c stage_d_bridge.c)
  set_target_properties(seedsigner_lvgl_native PROPERTIES
    PREFIX ""
    OUTPUT_NAME "seedsigner_lvgl_native"
  )

  target_link_libraries(seedsigner_lvgl_native PRIVATE Python3::Module)
  target_include_directories(seedsigner_lvgl_native PRIVATE ${Python3_INCLUDE_DIRS})

  if(DEFINED PYTHON_CROSS_INCLUDE_DIR AND NOT PYTHON_CROSS_INCLUDE_DIR STREQUAL "")
    target_include_directories(seedsigner_lvgl_native PRIVATE "${PYTHON_CROSS_INCLUDE_DIR}")
  endif()

  target_compile_definitions(seedsigner_lvgl_native PRIVATE
    SEEDSIGNER_STAGEC_PLACEHOLDER_BACKEND=1
  )
endif()

# Stage B wiring-ready placeholders (no direct linkage yet).
if(NOT "${SEEDSIGNER_C_MODULES_DIR}" STREQUAL "")
  target_compile_definitions(seedsigner_lvgl_native PRIVATE
    SEEDSIGNER_C_MODULES_DIR_PATH="${SEEDSIGNER_C_MODULES_DIR}"
  )
endif()

if(NOT "${LVGL_ROOT}" STREQUAL "")
  target_compile_definitions(seedsigner_lvgl_native PRIVATE
    LVGL_ROOT_PATH="${LVGL_ROOT}"
  )
endif()
