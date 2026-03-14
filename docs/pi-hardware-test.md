# Pi Zero Hardware Test Guide (Compiled Extension)

Goal: validate that the built extension runs on real Pi Zero hardware.

## Preconditions

- Pi runtime Python ABI expected:
  - `cpython-310-arm-linux-gnueabihf`
- Built extension file exists locally:
  - `src/seedsigner_lvgl_native.cpython-310-arm-linux-gnueabihf.so`

## 1) Copy files to Pi

At minimum, copy:
- `src/seedsigner_lvgl_native.cpython-310-arm-linux-gnueabihf.so`
- `tests/pi_runtime_smoke.py`
- `tests/pi_button_list_flow_smoke.py`

Example with `rsync` from repo root:

```bash
rsync -avz src/seedsigner_lvgl_native.cpython-310-arm-linux-gnueabihf.so \
  tests/pi_runtime_smoke.py tests/pi_button_list_flow_smoke.py \
  pi@<pi-host>:/home/pi/seedsigner-raspi-lvgl-test/
```

## 2) Verify Python ABI on Pi

On Pi:

```bash
python - <<'PY'
import platform,sysconfig
print(platform.machine())
print(sysconfig.get_config_var('SOABI'))
print(sysconfig.get_config_var('EXT_SUFFIX'))
PY
```

Expected:
- machine includes `armv6`
- soabi includes `cpython-310-arm-linux-gnueabihf`

## 3) Run runtime smoke test

On Pi in test directory:

```bash
PYTHONPATH=. python pi_runtime_smoke.py
```

Expected:
- import succeeds
- queue API works
- `button_list_screen(...)` returns an event tuple

## 4) Run flow smoke test

```bash
PYTHONPATH=. python pi_button_list_flow_smoke.py
```

Expected:
- script prints elapsed time + event tuple
- exits with `OK`

## 5) If import fails

Collect diagnostics:

```bash
python - <<'PY'
import sys,sysconfig,platform
print(sys.version)
print(platform.machine())
print(sysconfig.get_config_var('SOABI'))
PY

file seedsigner_lvgl_native.cpython-310-arm-linux-gnueabihf.so
readelf -A seedsigner_lvgl_native.cpython-310-arm-linux-gnueabihf.so | sed -n '1,120p'
ldd seedsigner_lvgl_native.cpython-310-arm-linux-gnueabihf.so || true
```

Share outputs for triage.
