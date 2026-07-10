# Architecture

## Objective

Run the SeedSigner C/C++ LVGL screens on Pi Zero (ARMv6), exposed to Python
through bindings that match the ESP32/MicroPython usage. Python orchestrates
Views/business flow; compiled C/C++ owns screen behavior and rendering.

## Core constraints

1. Reuse `seedsigner-lvgl-screens` screen code as source of truth — never fork
   screen logic here.
2. CPython binding signatures must match the MicroPython equivalents (same
   function names, same cfg JSON).
3. Direct function-call model (`button_list_screen(cfg_dict)`, not RPC).
4. Build flow is Docker-first, identical in local and GitHub Actions.
5. Single hardware target: 240x240 ST7789, joystick + KEY1/2/3,
   PX_MULTIPLIER=100 (a 320x240 landscape profile exists for bench work via
   `set_resolution`).

## Layer breakdown

### 1) Portable screen core (from the `sources/seedsigner-lvgl-screens` submodule)

Compiled in directly (see the `seedsigner_sources` glob in `setup.py`):

- `screens/*_screen.cpp` — one file per screen (construction + JSON cfg parsing)
- `components.cpp`, `screen_helpers`/`screen_scaffold`, `qr_core` — shared
  widgets, scaffold, result emission
- `navigation.cpp` — keypad sink, focus management, zone transitions, aux keys
- `input_profile.cpp` — runtime input mode (touch vs hardware)
- `locale_loader.cpp`, `locale_picker.cpp`, `font_registry.cpp`,
  `glyph_runs.cpp` — i18n font-pack loading
- `fonts/*.c`, `images/*.c` — baked fonts and logo assets (height-profile
  gated; see setup.py)

Only `components/seedsigner/` is used; the submodule's ESP32 hardware drivers
are excluded.

### 2) Pi platform backend + binding layer (`native/python_bindings/`)

One subsystem per file; `module_internal.h` declares the cross-unit API:

| File | Owns |
|------|------|
| `module.cpp` | `methods[]` table + module init — the public API index |
| `lvgl_runtime.cpp` | LVGL lifecycle, tick, host-driven pump, flush dispatch, resolution switch, save/restore/clear screen |
| `display_st7789.cpp` | ST7789 SPI driver (`/dev/spidev0.0`), GPIO output lines, flush-path flags |
| `gpio_input.cpp` | Joystick/key input lines + LVGL keypad indev callback |
| `gpio_lines.cpp` | Generic gpiochip-ioctl / sysfs GPIO helpers |
| `result_queue.cpp` | Result queue + the `seedsigner_lvgl_on_*` callback overrides |
| `screens.cpp` | Python wrappers for every screen (shared builders + per-screen macro) |
| `locale_packs.cpp` | Language-pack discovery/loading |

### 3) Python View layer

The SeedSigner app imports `seedsigner_lvgl_screens`, builds a screen, then
pumps + polls (`docs/interface-contract.md` has the full API and result
contract). There is no Python facade package.

## Hardware profile (Pi Zero, BCM numbering)

Matches upstream SeedSigner's Python drivers
([ST7789.py](https://github.com/SeedSigner/seedsigner/blob/dev/src/seedsigner/hardware/displays/ST7789.py),
[buttons.py](https://github.com/SeedSigner/seedsigner/blob/dev/src/seedsigner/hardware/buttons.py)):

- Display: ST7789 240x240, SPI `/dev/spidev0.0`; DC=25, RST=27, BL=24;
  BGR-wired panel (MADCTL BGR bit set by default). `native_display_init`
  defaults to 62.5 MHz SPI (upstream's Python driver runs 40 MHz; the panel
  tolerates the higher clock — pass `spi_speed_hz=40000000` to match upstream
  exactly).
- Input: joystick UP=6, DOWN=19, LEFT=5, RIGHT=26, PRESS=13; KEY1=21, KEY2=20,
  KEY3=16. Active-low with pull-up, via `/dev/gpiochip0` ioctls (sysfs
  fallback).
- Hold-repeat timing lives in the shared navigation/input code, mirroring
  upstream `buttons.py` (first repeat 225 ms, next 250 ms).

## Key boundary: navigation ownership

The lvgl-screens navigation system (`nav_bind()`) owns all focus management:
per-screen LVGL groups, keypad indev assignment, directional movement, zone
transitions, aux-key dispatch. The binding layer registers the keypad indev and
sets hardware input mode — it never creates groups or manages focus.

## Build model

- Canonical build: Docker + QEMU ARMv6 (`./run_build.sh` locally and in CI).
- Pinned dependencies in `versions.lock.toml`; ABI cross-validated against
  `docs/abi/dev-pi-abi.json`; ARMv6 codegen verified via `readelf -A`.
- Output: `src/seedsigner_lvgl_screens.cpython-310-arm-linux-gnueabihf.so`.
