# Architecture

## Objective

Run the existing SeedSigner C/C++ LVGL screen implementation on Pi Zero
(ARMv6), exposed to Python through bindings that match MicroPython usage.

## Non-goal

Rewriting screen logic in Python. Python orchestrates Views/business flow;
compiled C/C++ owns screen behavior and rendering.

## Core constraints

1. Reuse `seedsigner-c-modules` screen code as source of truth.
2. CPython binding signatures must match MicroPython equivalents.
3. Direct function-call model (`button_list_screen(cfg_dict)`, not RPC).
4. Build flow is Docker-first, identical in local and GitHub Actions.
5. Single hardware target: 240x240 ST7789, joystick + KEY1/2/3, PX_MULTIPLIER=100.

## Layer breakdown

### 1) Portable screen core (C/C++ — from seedsigner-c-modules)

Reused directly:
- `seedsigner.cpp` — screen construction, JSON config parsing
- `components.cpp` — LVGL widgets, button callbacks, result emission
- `navigation.cpp` — keypad sink, focus management, zone transitions, aux keys
- `input_profile.cpp` — runtime input mode (touch vs hardware)
- `fonts/*.c` — pre-rendered font bitmaps

These files are platform-agnostic. ESP32 coupling lives in separate components
(`display_manager`, `esp_bsp`, `esp_lv_port`) that are not used here.

### 2) Pi platform backend (module.cpp)

`native/python_bindings/module.cpp` is both the platform backend and the
Python binding layer:
- ST7789 SPI display flush (direct `/dev/spidev0.0` writes)
- GPIO input polling via `/dev/gpiochip0` (active-low, joystick + 3 buttons)
- LVGL runtime management (init, tick, timer handler)
- `input_profile_set_mode(INPUT_MODE_HARDWARE)` — activates the c-modules
  navigation system (keypad sink, per-screen focus groups, aux key handling)
- Result queue bridging `seedsigner_lvgl_on_button_selected()` → Python tuples

### 3) CPython binding surface

Native extension module `seedsigner_lvgl_native` exposes:
- `lvgl_init(hor_res, ver_res)` / `lvgl_shutdown()`
- `native_display_init(...)` / `native_display_shutdown()`
- `button_list_screen(cfg_dict)` — renders screen, runs LVGL loop until result
- `clear_result_queue()` / `poll_for_result()` — result tuple access
- `set_flush_callback(cb)` / `set_flush_mode("native"|"python")`

### 4) Python View layer

Existing business logic calls screen functions and polls for results, unchanged
from MicroPython usage.

## Build model

- Canonical build: Docker + QEMU ARMv6 emulation using GHCR base image
- Same `build_stagef_with_local_base.sh` script for local and CI
- Pinned dependencies in `versions.lock.toml`
- ARMv6 codegen enforced and verified via `readelf -A`

## Key boundary: navigation ownership

The c-modules navigation system (`nav_bind()`) owns all focus management:
- Creates per-screen LVGL groups
- Assigns keypad indevs to groups
- Handles directional movement, zone transitions, aux key dispatch

`module.cpp` registers the keypad indev and sets hardware input mode.
It does **not** create groups or manage focus — that's the c-modules' job.
