# seedsigner-raspi-lvgl

Raspberry Pi Zero target project for SeedSigner-style LVGL UI.

## Scope (reworked)
- New standalone project under `dev/`.
- Primary path: compile and run existing SeedSigner C/C++ LVGL screens on Pi Zero.
- Expose compiled screens through Python bindings with MicroPython-parity signatures.

## Input model (current decision)
- No touchscreen.
- No dedicated hardware BACK button.
- Directional joystick + SELECT.
- `UP` at top-of-content moves focus to top nav (Back/Power).
- Back/exit behavior handled via top-nav focus and selection.

## Display model (current decision)
- SPI display target on Raspberry Pi Zero.
- Target display resolution: **240 x 240**.
- For Pi Zero builds, set `PX_MULTIPLIER=100` (no scaling vs original SeedSigner pixels).
- LVGL UI logic should be portable, with Pi-specific platform integration.

## Implementation plan
- See `TODO.md` for phased implementation steps, metrics, and immediate next tasks.

## Durable references
- Local C modules root: `/home/keith/.openclaw/workspace-mp-project-lead/dev/seedsigner-micropython-builder/sources/seedsigner-c-modules`
- MicroPython bindings: `/home/keith/.openclaw/workspace-mp-project-lead/dev/seedsigner-micropython-builder/sources/seedsigner-c-modules/bindings/modseedsigner_bindings.c`
- C screen implementation: `/home/keith/.openclaw/workspace-mp-project-lead/dev/seedsigner-micropython-builder/sources/seedsigner-c-modules/components/seedsigner/seedsigner.cpp`
- `button_list_screen` parity sample: `/home/keith/.openclaw/workspace-mp-project-lead/dev/seedsigner-micropython-builder/sources/seedsigner-c-modules/tests/repl_test_button_list_flow.py`
- Scenario reference: `/home/keith/.openclaw/workspace-mp-project-lead/dev/seedsigner-micropython-builder/sources/seedsigner-c-modules/tests/screenshot_generator/scenarios.json`
- Upstream ST7789 driver: https://github.com/SeedSigner/seedsigner/blob/dev/src/seedsigner/hardware/displays/ST7789.py
- Upstream buttons input model: https://github.com/SeedSigner/seedsigner/blob/dev/src/seedsigner/hardware/buttons.py
- Production Pi OS baseline (Buildroot): https://github.com/SeedSigner/seedsigner-os
- Extracted Pi GPIO/timing profile for this project: `docs/hardware-profile.md`

## Next steps (re-prioritized)
1. Build `docs/porting-boundary.md` to map portable C/C++ screen core vs ESP32-specific code.
2. Add Docker Pi cross-build skeleton and matching GitHub Actions entrypoint.
3. Link compiled screen core into a minimal CPython binding (`seedsigner_lvgl`) for one screen.
4. Demonstrate `button_list_screen` PoC on real Pi Zero hardware via Python binding + poll queue.
