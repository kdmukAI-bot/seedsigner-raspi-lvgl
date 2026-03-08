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

## Current M1 status

- API parity stubs in `seedsigner_lvgl`:
  - `button_list_screen(cfg_dict)`
  - `clear_result_queue()`
  - `poll_for_result()`
- Deterministic placeholder event behavior implemented.

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
