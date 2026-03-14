# seedsigner-raspi-lvgl

Run SeedSigner's compiled C/C++ LVGL screens on Raspberry Pi Zero (ARMv6).

## What this is

This project compiles the screen implementations from
[seedsigner-c-modules](https://github.com/kdmukAI-bot/seedsigner-c-modules)
into a CPython extension (`seedsigner_lvgl_native`) that runs on the Pi Zero
hardware that production SeedSigner targets.

Python View/business logic calls screen functions directly — the same call
shape as the MicroPython bindings — while the compiled C++ code owns rendering,
navigation, and focus management via LVGL.

## Architecture

```
Python View layer
    │  button_list_screen(cfg_dict)
    │  poll_for_result() → ("button_selected", index, label)
    ▼
CPython binding (module.cpp)
    │  JSON marshal, LVGL runtime, result queue
    ▼
Portable screen core (seedsigner-c-modules)
    │  seedsigner.cpp, components.cpp, navigation.cpp
    ▼
Pi platform backend (module.cpp)
    │  ST7789 SPI flush, GPIO input polling, LVGL tick
    ▼
Hardware: 240x240 ST7789 + joystick + KEY1/KEY2/KEY3
```

See `docs/architecture.md` for design decisions and boundaries.

## Quick start

```bash
git clone --recursive https://github.com/kdmukAI-bot/seedsigner-raspi-lvgl.git
cd seedsigner-raspi-lvgl
python -m venv .venv && source .venv/bin/activate
pip install -U pip pytest
```

See `README-dev.md` for build commands, CI details, and Pi hardware testing.

## Hardware target

- Raspberry Pi Zero (ARMv6, single supported profile)
- 240x240 ST7789 SPI display
- 5-way joystick + 3 side buttons (KEY1/KEY2/KEY3)
- No touchscreen

Pin mappings and timing: `docs/hardware-profile.md`

## Documentation

| Document | Purpose |
|----------|---------|
| `README-dev.md` | Build commands, CI, testing |
| `docs/architecture.md` | Design decisions and layer boundaries |
| `docs/hardware-profile.md` | GPIO pins, SPI config, timing |
| `docs/input-button-behavior.md` | Navigation model and input contract |
| `docs/interface-contract.md` | Python binding API contract |
| `docs/pi-hardware-test.md` | On-device validation guide |
| `docs/python-abi-targets.md` | Target Python ABI decisions |
| `docs/production-parity-lock.md` | Version pinning policy |

## Upstream references

- SeedSigner main repo: https://github.com/SeedSigner/seedsigner
- SeedSigner OS (Buildroot): https://github.com/SeedSigner/seedsigner-os
