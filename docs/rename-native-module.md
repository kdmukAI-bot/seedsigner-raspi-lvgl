# TODO: build the Pi extension as `seedsigner_lvgl_screens`

**Status:** pending — flagged 2026-06-20 from the seedsigner Foundation A work.

The seedsigner Python app now does **`import seedsigner_lvgl_screens`**
(previously the Pi side was `seedsigner_lvgl_native`, referred to in docs as
`seedsigner_lvgl`). The compiled Pi extension this repo produces must be
**importable under that name**.

- **What to change:** the extension/package name in `setup.py` / the native build
  and the `.pth` deploy pattern, so `import seedsigner_lvgl_screens` resolves on
  the Pi. The underlying `.so` / C-extension name can remain an implementation
  detail (e.g. ship a thin `seedsigner_lvgl_screens` Python package wrapping it),
  but the import must work.
- **Why:** a single, content-descriptive public import name across Pi + ESP32,
  matching the `seedsigner-lvgl-screens` source repo. Replaces the old Pi-only
  `seedsigner_lvgl_native`.
- **Consumer:** seedsigner `src/seedsigner/gui/lvgl_screen_runner.py` does
  `import seedsigner_lvgl_screens as lv`.
- **Full rationale + the unified host-API contract:** see the seedsigner repo's
  `docs/architecture/lvgl-host-api-unification.md`.
