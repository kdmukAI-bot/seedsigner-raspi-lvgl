# Porting Boundary Map (SeedSigner C/C++ LVGL -> Pi Zero)

This document defines what to reuse as-is from `seedsigner-c-modules`, what to replace with Pi shims, and what is out-of-scope for the PoC.

## Source roots

- C modules root:
  `/home/keith/.openclaw/workspace-mp-project-lead/dev/seedsigner-micropython-builder/sources/seedsigner-c-modules`
- Screen core candidate:
  - `components/seedsigner/seedsigner.cpp`
  - `components/seedsigner/components.cpp`
  - `components/seedsigner/gui_constants.h`
  - `components/seedsigner/components.h`
  - `components/seedsigner/fonts/*.c`
- Existing MicroPython binding reference:
  - `bindings/modseedsigner_bindings.c`
- Existing non-IDF native build reference:
  - `tests/screenshot_generator/CMakeLists.txt`

## Key observation

`components/seedsigner/*` screen logic is already mostly platform-agnostic LVGL + JSON/C++ logic.
The primary ESP32 coupling sits in `components/display_manager/*`, `components/esp_bsp/*`, and `components/esp_lv_port/*`.

This is favorable for Pi porting.

---

## Boundary classification

## 1) Reuse directly (PoC path)

- `components/seedsigner/seedsigner.cpp`
  - screen construction (`button_list_screen`, `main_menu_screen`, etc.)
  - JSON config parsing/validation
- `components/seedsigner/components.cpp`
  - reusable LVGL widgets (`top_nav`, `button_list`, button callbacks)
  - emits `seedsigner_lvgl_on_button_selected(...)`
- `components/seedsigner/gui_constants.h`
- `components/seedsigner/fonts/*.c` (target-specific subset selectable)
- `components/nlohmann_json/include/*`

Rationale: these files contain the screen behavior we want to preserve exactly.

## 2) Replace with Pi shims

### Replace ESP display/runtime wrapper
- Current ESP-specific:
  - `components/display_manager/display_manager.cpp`
  - depends on `bsp_*`, `esp_lv_port`, `esp_io_expander`, FreeRTOS/IDF
- Pi replacement needed:
  - `platform/pi_zero/runtime` (new)
  - responsibilities:
    - LVGL init/tick/task handling
    - ST7789 flush integration
    - input device registration (joystick/key backend)
    - screen callback execution wrapper (`run_screen` equivalent)

### Replace binding host layer
- Current host layer:
  - `bindings/modseedsigner_bindings.c` (MicroPython C module)
- Pi replacement needed:
  - CPython extension module exposing equivalent API surface:
    - `button_list_screen(cfg_dict)`
    - `clear_result_queue()`
    - `poll_for_result()`
  - can reuse result queue/event callback pattern from MicroPython binding logic.

## 3) Exclude from PoC

- `components/esp_bsp/*`
- `components/esp_lv_port/*`
- `components/esp32-camera/*`
- power/camera/touch-specific ESP components

Rationale: not required for first Pi `button_list_screen` compiled-screen PoC.

---

## Interface boundary to preserve

From Python caller perspective (View layer), keep parity with MicroPython usage:
- direct function call model (no RPC envelope)
- dict-based screen config
- poll queue result flow

Minimum PoC result tuple compatibility target:
- `("button_selected", index, label)`

Back/top-nav event tuple parity should be validated/locked after first PoC path is running.

---

## Build-system boundary

## Reuse
- `tests/screenshot_generator/CMakeLists.txt` proves non-IDF CMake build of:
  - LVGL sources
  - `components/seedsigner/*.cpp`
  - selected font sources

## New for Pi
- Dockerized build entrypoint for armv6l-targetable artifact generation
- CPython extension build wiring (e.g., setuptools + CMake/meson, or pure CMake + wheel step)
- same Docker entrypoint for local + GitHub Actions

---

## PoC extraction set (recommended minimal)

1. LVGL core sources
2. `components/seedsigner/components.cpp`
3. `components/seedsigner/seedsigner.cpp`
4. required `gui_constants.h`/headers
5. minimal font set sufficient for `button_list_screen`
6. nlohmann json headers
7. Pi runtime shim (`run_screen` equivalent)
8. CPython binding bridge + queue callback glue

---

## Risks / watch items

1. **LVGL runtime threading model differences** (ESP task model vs Linux loop)
2. **Font footprint and selection** for Pi parity vs memory/perf
3. **Event semantics drift** between MicroPython binding and CPython binding
4. **Top-nav behavior parity** (back/power) needs explicit confirmation in PoC tests

---

## Immediate implementation checklist

1. Create `platform/pi_zero/runtime` shim with `run_screen` equivalent and LVGL loop hooks.
2. Build minimal native executable linking `seedsigner.cpp + components.cpp` on host (sanity gate).
3. Build CPython extension exposing `button_list_screen`, `poll_for_result`, `clear_result_queue`.
4. Run on Pi Zero with real input/display backend and validate tuple output parity.
