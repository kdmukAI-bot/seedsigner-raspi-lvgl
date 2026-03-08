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

## Stage C/E native binding path

Host build + native extension smoke test:

```bash
./scripts/stagec_build.sh
```

Docker entrypoint:

```bash
TARGET_ARCH=host ./scripts/build_stagec_in_docker.sh
TARGET_ARCH=armv6 ./scripts/build_stagec_in_docker.sh
```

Stage C/E logs:
- `logs/staged/`
- `<YYYYmmdd-HHMMSS>_staged_<mode>_<target>.log`
- `<YYYYmmdd-HHMMSS>_staged_docker_driver_<target>.log`

## ARMv6 Python base image (versioned)

Initial priority is Python 3.10 for dev hardware.

Build local ARMv6 Python base image (source-built Python):

```bash
PYTHON_VERSION=3.10.10 PY_SERIES=py310 ./scripts/build_python_armv6_base_in_docker.sh
```

Logs:
- `logs/base-image/`
- `<YYYYmmdd-HHMMSS>_python-armv6-base_<pyseries>.log`

This path is parameterized for future versions (e.g., py311/py312).

## Stage F armv6-cpython wiring

Run Stage F host path:

```bash
TARGET_ARCH=host ./scripts/stagef_build.sh
```

Run Stage F Docker armv6-cpython wiring:

```bash
TARGET_ARCH=armv6-cpython ./scripts/build_stagef_in_docker.sh
```

Stage F logs:
- `logs/stagef/`
- `<YYYYmmdd-HHMMSS>_stagef_<mode>_<target>.log`
- `<YYYYmmdd-HHMMSS>_stagef_docker_driver_<target>.log`

Stage F environment knobs:
- `LOCK_FILE` (default `versions.lock.toml`, consumed by scripts)
- `ABI_JSON` (default `docs/abi/dev-pi-abi.json`)
- `PYTHON_TARGET_INCLUDE` (required if target Python headers are not present in container)
- `PYTHON_TARGET_LIBDIR` (optional)
- `PYTHON_TARGET_LDLIBRARY` (optional)

Alternative (emulated ARM container) when cross headers/sysroot are unavailable:

```bash
./scripts/build_stagef_emu_in_docker.sh
```

Notes:
- Uses QEMU/binfmt + `--platform linux/arm/v7` with Python 3.10 bullseye base.
- Build step now forces ARMv6 codegen flags (`-march=armv6zk -mtune=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard`).
- Script verifies `readelf -A` includes ARMv6 CPU arch attribute and fails otherwise.
- Slower than native/cross builds; still validate final artifacts on real Pi.

Current limitations:
- armv6-cpython build wiring is in place, but successful cross-build still depends on providing target Python headers/sysroot paths.
- Stage E runtime loop is minimal; full joystick->LVGL indev parity wiring is still pending.
- If no callback event is emitted before timeout, deterministic fallback queue event is used.
- Compiled `button_list_screen` currently expects `button_list` entries as strings or arrays/tuples with string at index 0.

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
