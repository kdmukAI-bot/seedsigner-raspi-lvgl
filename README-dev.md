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

## Current direction status

- Temporary Python `button_list_screen` behavior shim was retired.
- Target implementation path is compiled C/C++ LVGL screens exposed through Python bindings.
- Until compiled bindings are wired, calling `button_list_screen` from Python package raises `NotImplementedError` by design.

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
