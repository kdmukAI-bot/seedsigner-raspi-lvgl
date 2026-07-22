# seedsigner-raspi-lvgl

Run SeedSigner's compiled C/C++ LVGL screens on Raspberry Pi Zero (ARMv6).

## What this is

This project compiles the screen implementations from
[seedsigner-lvgl-screens](https://github.com/kdmukAI-bot/seedsigner-lvgl-screens)
into a CPython extension (`seedsigner_lvgl_screens`) that runs on the Pi Zero
hardware that production SeedSigner targets.

Python View/business logic calls screen functions directly — the same call
shape as the MicroPython bindings — while the compiled C++ code owns rendering,
navigation, and focus management via LVGL.

## Architecture

```
Python View layer
    │  button_list_screen(cfg_dict)
    │  lvgl_pump() + poll_for_result() → ("button_selected", index, label)
    ▼
CPython bindings + Pi platform backend (native/python_bindings/)
    │  JSON marshal, LVGL runtime + pump, result queue,
    │  ST7789 SPI flush, GPIO input polling
    ▼
Portable screen core (sources/seedsigner-lvgl-screens)
    │  screens/*_screen.cpp, components.cpp, navigation.cpp
    ▼
Hardware: 240x240 ST7789 + joystick + KEY1/KEY2/KEY3
```

See `docs/architecture.md` for layer boundaries, the binding-file map, and the
hardware profile.

## Quick start

```bash
git clone --recursive https://github.com/kdmukAI-bot/seedsigner-raspi-lvgl.git
cd seedsigner-raspi-lvgl
python -m venv .venv && source .venv/bin/activate
pip install -U pip pytest
```

See `README-dev.md` for build commands, CI details, and Pi hardware testing.

## Local dev workflow

The loop is **build the extension → deploy it (with the app) to a dev Pi → iterate**.

**1. Build the `.so`** — Docker QEMU ARMv6 cross-compile (~13 min cold, ~1 min with ccache):

```bash
./run_build.sh
# -> src/seedsigner_lvgl_screens*.so   (+ uUR*.so, the native BC-UR decoder)
```

To build against an in-progress `seedsigner-lvgl-screens` tree instead of the pinned
submodule, see `docs/knowledge/build-against-alternate-screens-tree.md`.

**2. Deploy to a dev Pi** — `scripts/deploy-dev.sh` stops the app, syncs the app code plus
the freshly built `.so`(s), (re)generates `version.json`, and restarts:

```bash
scripts/deploy-dev.sh 192.168.1.50           # bare host/IP -> root@ prepended
scripts/deploy-dev.sh root@seedsigner.local  # or a full ssh target
scripts/deploy-dev.sh -a /path/to/seedsigner # deploy a specific app checkout
scripts/deploy-dev.sh --help                 # all flags
```

Config precedence is **CLI args > environment > `.env`** (copy `.env.example`):

| Key | Set by | Meaning |
|-----|--------|---------|
| `SS_DEVICE` | positional arg / `-d` / env | ssh target of the dev Pi |
| `SS_APP_DIR` | `-a` / env | the **seedsigner app** checkout to deploy |

The current dev image is SeedSigner OS (root-only). Two things follow:

- The `.so`(s) co-locate **inside the app dir** (the app's working dir at launch), so a bare
  `import seedsigner_lvgl_screens` resolves with no `PYTHONPATH`.
- SeedSigner OS reads version data **only** from `src/seedsigner/version.json` (never `git` at
  runtime), and the sync strips `.git`. So the script regenerates that file from the app
  checkout's git state the way a release image does (`tools/write_versionfile.py`, git commit
  time). `SS_APP_DIR` must therefore be a **git checkout**; skip with `SS_SKIP_VERSIONFILE=1`.

## Hardware target

- Raspberry Pi Zero (ARMv6, single supported profile)
- 240x240 ST7789 SPI display
- 5-way joystick + 3 side buttons (KEY1/KEY2/KEY3)
- No touchscreen

## Documentation

| Document | Purpose |
|----------|---------|
| `README-dev.md` | Build commands, CI, base-image publishing |
| `docs/architecture.md` | Layer boundaries, binding-file map, hardware profile |
| `docs/interface-contract.md` | Python API contract: methods, cfg shapes, result tuples, input model |
| `docs/language-support.md` | Locale/font-pack loading |
| `docs/pi-hardware-test.md` | On-device validation guide |
| `docs/dev-device-deployment.md` | Deploying builds to the dev Pi |
| `docs/python-abi-targets.md` | Target Python ABI + production-parity policy |
| `docs/knowledge/` | Debug journals and non-obvious constraints |

## Upstream references

- SeedSigner main repo: https://github.com/SeedSigner/seedsigner
- SeedSigner OS (Buildroot): https://github.com/SeedSigner/seedsigner-os
