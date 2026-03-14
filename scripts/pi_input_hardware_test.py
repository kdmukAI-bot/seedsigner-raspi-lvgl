#!/usr/bin/env python3
"""Pi hardware test: verify joystick/button navigation through native LVGL screens.

Run on Pi Zero with the built .so in src/:
    PYTHONPATH=src python scripts/pi_input_hardware_test.py

Expected:
  - Screen renders on ST7789 display
  - Joystick UP/DOWN moves focus between buttons and top-nav
  - PRESS (center click) selects the focused item
  - KEY1/KEY2/KEY3 trigger aux key behavior
  - Selected item prints as an event tuple, then the next screen loads

Ctrl+C to exit at any time.
"""

from __future__ import annotations

import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = REPO_ROOT / "src"
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))


def run_screen(mod, title, buttons, *, show_back=True):
    """Render a screen and wait for real hardware input. Returns the event tuple."""
    mod.clear_result_queue()
    cfg = {
        "top_nav": {
            "title": title,
            "show_back_button": show_back,
            "show_power_button": False,
        },
        "button_list": buttons,
    }

    print(f"\n--- Screen: {title} ---")
    print(f"    Buttons: {buttons}")
    print("    Navigate with joystick, press center to select.")
    if show_back:
        print("    UP past first item reaches Back in top nav.")

    mod.button_list_screen(cfg)

    # button_list_screen blocks until a nav event fires (timeout=0 default).
    event = mod.poll_for_result()
    return event


def main() -> int:
    import seedsigner_lvgl_native as mod

    print("[hw-input-test] Initializing LVGL + native display...")
    mod.lvgl_init(hor_res=240, ver_res=240)
    mod.native_display_init()
    mod.set_flush_mode("native")
    print("[hw-input-test] Display + input ready.\n")

    screens = [
        ("Navigation Test", ["Alpha", "Beta", "Gamma"]),
        ("Two Items", ["First", "Second"]),
        ("Single Item", ["Only Option"]),
    ]

    try:
        for title, buttons in screens:
            event = run_screen(mod, title, buttons)
            print(f"  => Event: {event}")

            if event is None:
                print("  => ERROR: no event received")
                continue

            kind, index, label = event
            if kind == "topnav_back":
                print("  => Back pressed (top nav)")
            elif kind == "button_selected":
                print(f"  => Selected button #{index}: {label!r}")
            else:
                print(f"  => Other event: {kind}")

        print("\n[hw-input-test] All screens complete. Running one more (press Ctrl+C to exit)...")

        while True:
            event = run_screen(mod, "Repeat Test", ["Keep Going", "Still Here"])
            print(f"  => Event: {event}")

    except KeyboardInterrupt:
        print("\n[hw-input-test] Interrupted.")

    mod.lvgl_shutdown()
    mod.native_display_shutdown()
    print("[hw-input-test] Shutdown complete.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
