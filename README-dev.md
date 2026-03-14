# Developer Guide

## Local setup

```bash
git submodule update --init --recursive   # pulls seedsigner-c-modules + LVGL
python -m venv .venv
source .venv/bin/activate
pip install -U pip pytest
```

> The repo includes `seedsigner-c-modules` as a git submodule under
> `sources/seedsigner-c-modules`, which itself contains LVGL as a nested
> submodule (`third_party/lvgl`). The `--recursive` flag is required.

## Run tests

```bash
python -m pytest
```

## Build paths

### Host build (native extension for local arch)

```bash
./scripts/stagec_build.sh
```

Docker variant:
```bash
TARGET_ARCH=host ./scripts/build_stagec_in_docker.sh
```

### ARMv6 build (Pi Zero target)

Using GHCR base image (preferred — produces ARMv6-compatible artifacts):
```bash
./scripts/build_stagef_with_local_base.sh
```

Override image source:
```bash
IMAGE_TAG=seedsigner-raspi-lvgl/python-armv6:py310-dev-local ./scripts/build_stagef_with_local_base.sh
```

> **ARMv6 requirement**: use `build_stagef_with_local_base.sh` for Pi Zero
> artifacts. The `build_stagef_emu_in_docker.sh` path runs `linux/arm/v7`
> emulation and can produce artifacts that fail ARMv6 compatibility.

Alternative (emulated ARM container, slower):
```bash
./scripts/build_stagef_emu_in_docker.sh
```

### Build ARMv6 Python base image

```bash
PYTHON_VERSION=3.10.10 PY_SERIES=py310 ./scripts/build_python_armv6_base_in_docker.sh
```

## Build logs

All builds write timestamped logs under `logs/`:
- `logs/stageb/` — Docker build skeleton
- `logs/staged/` — native binding builds
- `logs/stagef/` — ARMv6 CPython extension builds
- `logs/base-image/` — Python base image builds

## Environment variables

| Variable | Purpose | Default |
|----------|---------|---------|
| `SEEDSIGNER_C_MODULES_DIR` | Path to c-modules source | `sources/seedsigner-c-modules` |
| `LVGL_ROOT` | Path to LVGL source | `sources/seedsigner-c-modules/third_party/lvgl` |
| `WS_ROOT` | Workspace root for Docker mounts | auto-detected |
| `IMAGE_TAG` | GHCR base image for ARMv6 builds | `py310-dev` tag |
| `TARGET_ARCH` | Build target (`host`, `armv6`) | varies by script |
| `LOCK_FILE` | Version lock file | `versions.lock.toml` |
| `ABI_JSON` | ABI reference file | `docs/abi/dev-pi-abi.json` |

## Pi hardware testing

Full guide: `docs/pi-hardware-test.md`

### Display smoke test

```bash
python scripts/pi_display_smoke.py --hold-seconds 1.5
```

Expected: white frame, black frame, checkerboard frame.

### Input smoke test (GPIO polling only)

```bash
python scripts/pi_input_smoke.py
```

Expected: prints timestamped press/repeat events. Exit with Ctrl+C.

### Hardware input test (display + navigation, end-to-end)

```bash
PYTHONPATH=src python scripts/pi_input_hardware_test.py
```

Renders screens on the display, waits for joystick/button input, prints
the resulting event. Verifies the full path: GPIO polling → LVGL indev →
c-modules navigation → screen result → Python event tuple.

## Input behavior spec

See `docs/input-button-behavior.md` for the canonical navigation and input
contract (focus zones, directional rules, KEY1/2/3 policy).
