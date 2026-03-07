# seedsigner-raspi-lvgl

Raspberry Pi Zero target project for SeedSigner-style LVGL UI.

## Scope (initial)
- New standalone project under `dev/`.
- Do **not** modify `seedsigner-c-modules` yet.
- Focus on platform layer for Pi Zero + SPI display + joystick input.

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
- Extracted Pi GPIO/timing profile for this project: `docs/hardware-profile.md`

## Next steps
1. Add target architecture doc (`docs/architecture.md`).
2. Define navigation contract (`docs/navigation-contract.md`).
3. Define font strategy for multiple resolutions (`docs/fonts.md`).
4. Capture baseline performance from current Python display path (`docs/perf-baseline.md`).
