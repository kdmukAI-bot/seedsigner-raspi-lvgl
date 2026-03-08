# README-dev

## Local setup

```bash
python -m venv .venv
source .venv/bin/activate
pip install -U pip pytest
```

## Run tests

```bash
python -m pytest
```

## Stage B Docker build skeleton

Build the portable screen-core sanity target through Docker (same entrypoint local + CI):

```bash
./scripts/build_in_docker.sh
```

Build logs are written to a consistent directory:
- `logs/stageb/`
- filename format starts with UTC timestamp and includes target:
  - `<YYYYmmdd-HHMMSS>_stageb_docker_driver_<target>.log`
  - `<YYYYmmdd-HHMMSS>_stageb_docker_<target>.log`
  - `<YYYYmmdd-HHMMSS>_stageb_host_<target>.log`

Notes:
- Expects sibling repo at `../seedsigner-micropython-builder` by default.
- Override paths via env vars if needed:
  - `WS_ROOT`
  - `SEEDSIGNER_C_MODULES_DIR`
  - `LVGL_ROOT`
  - `RUN_TS` (optional fixed run timestamp for grouped logs)
  - `TARGET_ARCH` (`host` default, `armv6` for Pi Zero cross build)

Examples:
```bash
# host build in docker
TARGET_ARCH=host ./scripts/build_in_docker.sh

# Pi Zero architecture cross-build in docker
TARGET_ARCH=armv6 ./scripts/build_in_docker.sh
```

## Stage C CPython binding scaffold

Build native CPython extension (host):

```bash
./scripts/stagec_build.sh
```

Build native CPython extension through Docker:

```bash
TARGET_ARCH=host ./scripts/stagec_build_in_docker.sh
TARGET_ARCH=armv6 ./scripts/stagec_build_in_docker.sh
```

Stage C logs are written under:
- `logs/stagec/`
- filename format starts with UTC timestamp, e.g.
  - `<YYYYmmdd-HHMMSS>_stagec_docker_driver_<target>.log`
  - `<YYYYmmdd-HHMMSS>_stagec_docker_<target>.log`
  - `<YYYYmmdd-HHMMSS>_stagec_host_<target>.log`

Import/test command (after host build):

```bash
PYTHONPATH=build/stagec-native-host-host python -m pytest -q tests/test_native_module_smoke.py
```

## Current direction status

- Temporary Python `button_list_screen` behavior shim was retired.
- Stage C adds a minimal native module scaffold (`seedsigner_lvgl_native`) with deterministic placeholder queue behavior.
- Full runtime parity is not claimed yet; backend linkage remains placeholder wiring until deeper integration.

## Pi hardware smoke test (display)

Run this on Pi target hardware (with `RPi.GPIO` and `spidev` installed):

```bash
python scripts/pi_display_smoke.py --hold-seconds 1.5
```

(If you prefer explicit pathing: `PYTHONPATH=src python scripts/pi_display_smoke.py`)

Expected visual sequence:
1. White frame
2. Black frame
3. Checkerboard frame

## Pi hardware smoke test (input)

Run this on Pi target hardware (with `RPi.GPIO` installed):

```bash
python scripts/pi_input_smoke.py
```

(If you prefer explicit pathing: `PYTHONPATH=src python scripts/pi_input_smoke.py`)

Expected behavior:
- Prints timestamped `press` and `repeat` events for UP/DOWN/LEFT/RIGHT/PRESS/KEY1/KEY2/KEY3
- Repeat cadence scaffold: first repeat ~225ms, then every ~250ms while held
- Exit with `Ctrl+C`
