#!/usr/bin/env python3
"""Pi hardware test: verify joystick/button navigation through native LVGL screens.

Run on Pi Zero with the built .so in src/:
    PYTHONPATH=src python scripts/pi_input_hardware_test.py

Expected:
  - Screen renders on ST7789 display
  - Joystick navigates focus between controls
  - PRESS (center click) selects the focused item
  - KEY1/KEY2/KEY3 trigger aux key behavior
  - Selected item prints as an event tuple, then the next screen loads

Ctrl+C to exit.
"""

from __future__ import annotations

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = REPO_ROOT / "src"
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))

# How long each lvgl_pump cycle runs (ms) before returning to Python.
# Shorter = more responsive to Ctrl+C, but slightly more Python overhead.
PUMP_MS = 500


def pump_until_result(mod):
    """Pump LVGL in a loop until a result event arrives. Returns the event tuple."""
    while True:
        mod.lvgl_pump(duration_ms=PUMP_MS)
        event = mod.poll_for_result()
        if event is not None:
            return event


def show_main_menu(mod):
    """Render main menu and wait for input."""
    mod.clear_result_queue()
    mod.main_menu_screen(wait_timeout_ms=1)
    return pump_until_result(mod)


def show_button_list(mod, title, buttons, *, show_back=True):
    """Render a button list screen and wait for input."""
    mod.clear_result_queue()
    cfg = {
        "top_nav": {
            "title": title,
            "show_back_button": show_back,
            "show_power_button": False,
        },
        "button_list": buttons,
        "wait_timeout_ms": 1,
        "allow_timeout_fallback": False,
    }
    mod.button_list_screen(cfg)
    return pump_until_result(mod)


def format_event(event):
    """Format an event tuple for display."""
    if event is None:
        return "None (no event)"
    kind, index, label = event
    if kind == "topnav_back":
        return "BACK (top nav)"
    if kind == "topnav_power":
        return "POWER (top nav)"
    if kind == "button_selected":
        return f"button #{index}: {label!r}"
    return f"{kind} index={index} label={label!r}"


def main() -> int:
    import seedsigner_lvgl_native as mod

    print("[hw-input-test] Initializing LVGL + native display...")
    mod.lvgl_init(hor_res=240, ver_res=240)
    mod.native_display_init()
    mod.set_flush_mode("native")
    print("[hw-input-test] Display + input ready.")
    print("[hw-input-test] Ctrl+C to exit.\n")

    screens = [
        ("main_menu", None, None),
        ("list", "Vertical List", ["Alpha", "Beta", "Gamma"]),
        ("list", "Two Items", ["First", "Second"]),
        ("list", "Single Item", ["Only Option"]),
    ]

    try:
        for i, (kind, title, buttons) in enumerate(screens, 1):
            if kind == "main_menu":
                print(f"[{i}/{len(screens)}] Screen: Main Menu (2x2 grid)")
                print("    Use joystick in all 4 directions, center click to select.")
                event = show_main_menu(mod)
            else:
                print(f"[{i}/{len(screens)}] Screen: {title}  |  Buttons: {buttons}")
                print("    Use joystick to navigate, center click to select.")
                event = show_button_list(mod, title, buttons)

            print(f"    => {format_event(event)}\n")

        print("All test screens complete. Looping main menu — Ctrl+C to exit.\n")

        round_num = 0
        while True:
            round_num += 1
            print(f"[loop #{round_num}] Main Menu")
            event = show_main_menu(mod)
            print(f"    => {format_event(event)}\n")

    except KeyboardInterrupt:
        print("\n[hw-input-test] Interrupted.")

    mod.lvgl_shutdown()
    mod.native_display_shutdown()
    print("[hw-input-test] Done.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
