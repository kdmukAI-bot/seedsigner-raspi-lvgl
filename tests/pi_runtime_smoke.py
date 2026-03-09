#!/usr/bin/env python3
"""Pi runtime smoke test for compiled seedsigner_lvgl_native extension."""

from __future__ import annotations

import importlib
import json
import platform
import sys
import sysconfig


def main() -> int:
    print("[pi-runtime-smoke] python:", sys.version)
    print("[pi-runtime-smoke] machine:", platform.machine())
    print("[pi-runtime-smoke] soabi:", sysconfig.get_config_var("SOABI"))

    try:
        mod = importlib.import_module("seedsigner_lvgl_native")
    except Exception as e:
        print("[pi-runtime-smoke] ERROR import failed:", repr(e))
        return 2

    print("[pi-runtime-smoke] import OK")

    try:
        mod.clear_result_queue()
        polled = mod.poll_for_result()
        print("[pi-runtime-smoke] initial poll:", polled)
    except Exception as e:
        print("[pi-runtime-smoke] ERROR queue API failed:", repr(e))
        return 3

    cfg = {
        "top_nav": {"title": "Pi Runtime Smoke", "show_back_button": True, "show_power_button": False},
        "button_list": ["Alpha", "Beta", "Gamma"],
    }

    try:
        mod.lvgl_init(hor_res=240, ver_res=240)
        mod.button_list_screen(cfg)
        event = mod.poll_for_result()
    except Exception as e:
        print("[pi-runtime-smoke] ERROR button_list_screen failed:", repr(e))
        return 4

    print("[pi-runtime-smoke] event:", json.dumps(event))
    print("[pi-runtime-smoke] OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
