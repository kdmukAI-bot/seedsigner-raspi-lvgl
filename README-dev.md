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

## Current M4 status (vertical slice)

- API surface unchanged:
  - `button_list_screen(cfg_dict)`
  - `clear_result_queue()`
  - `poll_for_result()`
- `button_list_screen` now runs an input-driven loop via Pi input backend.
- Supported `button_list` item shapes:
  - strings
  - `(label, value)` tuples
  - dicts containing `label`
- Current result tuples:
  - Select: `("button_selected", index, label)`
  - Back (when `top_nav.show_back_button=True` + `KEY1`): `("back", -1, "back")`
- Current limitations:
  - Rendering is still stubbed/minimal (focus is input->result behavior)
  - Top-nav focus navigation (UP into top-nav / DOWN back) not yet implemented

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
