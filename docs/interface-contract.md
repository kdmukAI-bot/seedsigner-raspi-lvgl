# Interface Contract (Python View ↔ Compiled Screens)

Defines the compatibility contract for invoking compiled LVGL screens from
Python View code on Pi Zero.

## Binding model

- Python calls screen functions directly via the native extension (no RPC
  envelope, no router, no server layer).
- Config dict shape and result behavior must match MicroPython binding behavior.
- Platform-specific internals (threading, SPI, GPIO) do not leak into the API.

## Call signature: button_list_screen

```python
import seedsigner_lvgl_native as mod

mod.lvgl_init(hor_res=240, ver_res=240)
mod.native_display_init()  # on Pi hardware

mod.button_list_screen({
    "top_nav": {
        "title": "Select Network",
        "show_back_button": True,
        "show_power_button": False,
    },
    "button_list": [
        ("Mainnet", "mainnet"),
        ("Testnet", "testnet"),
    ],
    # Optional overrides (default: wait forever for hardware input):
    # "wait_timeout_ms": 0,
    # "allow_timeout_fallback": False,
})

event = mod.poll_for_result()
# → ("button_selected", 0, "Mainnet")
# → ("topnav_back", -1, "topnav_back")
# → ("topnav_power", -1, "topnav_power")
```

### cfg_dict shape

- `top_nav` (required dict): `title` (string, required), `show_back_button`
  (bool), `show_power_button` (bool)
- `button_list` (required list): strings or `(label, value)` tuples
- `wait_timeout_ms` (optional int): milliseconds to wait for input; `0` = forever (default)
- `allow_timeout_fallback` (optional bool): if true and timeout expires,
  auto-select first button (default: false)

## Result semantics

Results are polled via `clear_result_queue()` + `poll_for_result()`:

| Event | Tuple shape |
|-------|-------------|
| Body button selected | `("button_selected", index, label)` |
| Top-nav back | `("topnav_back", -1, "topnav_back")` |
| Top-nav power | `("topnav_power", -1, "topnav_power")` |
| No result yet | `None` |

## Error behavior

Invalid config raises `RuntimeError` with a descriptive message:
- Missing `top_nav` or `top_nav.title`
- Missing or invalid `button_list`
- LVGL runtime not initialized

## Determinism

Given the same config dict and input sequence, output events are deterministic.
Result ordering and naming are stable.
