#!/usr/bin/env python3
"""Interactive-ish flow smoke for button_list_screen on Pi.

This currently validates call/return path and queue event shape.
"""

from __future__ import annotations

import importlib
import time


def main() -> int:
    mod = importlib.import_module("seedsigner_lvgl_native")

    mod.clear_result_queue()
    cfg = {
        "top_nav": {"title": "Select Option", "show_back_button": True, "show_power_button": False},
        "button_list": ["One", "Two", "Three"],
    }

    start = time.time()
    mod.button_list_screen(cfg)
    elapsed = time.time() - start

    ev = mod.poll_for_result()
    print("[pi-button-list-smoke] elapsed_s=", round(elapsed, 3))
    print("[pi-button-list-smoke] event=", ev)

    if ev is None:
        print("[pi-button-list-smoke] ERROR: no event returned")
        return 2
    if not isinstance(ev, tuple) or len(ev) != 3:
        print("[pi-button-list-smoke] ERROR: unexpected event shape")
        return 3

    print("[pi-button-list-smoke] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
