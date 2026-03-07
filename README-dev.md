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
