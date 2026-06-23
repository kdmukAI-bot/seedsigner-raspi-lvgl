#!/usr/bin/env python3
"""Native screensaver idle-watch hardware check (Phase 2 overlay manager).

Run on the Pi Zero with the rebuilt .so:
    cd /home/pi/seedsigner-raspi-lvgl && python3 tests/pi_screensaver_idle_test.py

This is the one piece of the overlay manager that the headless/desktop tests
cannot prove: that the native idle-watch actually drives the ST7789 with no
Python loop touching the screensaver. Watch the display and confirm:

  1. A button-list screen renders.
  2. After ~3 s of NO input, the native overlay manager swaps in the
     bouncing-logo screensaver automatically — Python only pumps lv_timer_handler;
     it never calls a screensaver function.
  3. Pressing any button dismisses the screensaver and restores the button list
     in place (the wake press is swallowed, not acted on).
  4. Navigating + selecting on the restored screen still works: pick a body
     button to end the test (its result prints).

Ctrl+C also exits.
"""
from __future__ import annotations

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = REPO_ROOT / "src"
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))

import seedsigner_lvgl_screens as lv

TIMEOUT_MS = 3000   # short, so the saver fires quickly during the test
PUMP_MS = 100       # lv_timer_handler slice; the dispatcher itself runs every 200ms


def main() -> int:
    print("[ss-idle] init LVGL + native ST7789...")
    lv.lvgl_init(hor_res=240, ver_res=240)
    lv.native_display_init()
    lv.set_flush_mode("native")

    # The Phase 2 export: the Python runtime sets this once at init. The overlay
    # manager owns activation/dismiss entirely in C from here on.
    lv.set_screensaver_timeout(TIMEOUT_MS)
    print(f"[ss-idle] set_screensaver_timeout({TIMEOUT_MS}) ok")

    lv.clear_result_queue()
    lv.button_list_screen({
        "top_nav": {"title": "Idle Test", "show_back_button": True, "show_power_button": False},
        "button_list": ["Alpha", "Beta", "Gamma"],
    })
    print("[ss-idle] button list built (pure builder; returned immediately).")
    print(f"[ss-idle] >>> DO NOT TOUCH for ~{TIMEOUT_MS // 1000}s — watch for the bouncing logo.")
    print("[ss-idle] >>> then press any button to dismiss, then click a list item to finish.")

    # Pump LVGL forever. The overlay dispatcher (a 200ms lv_timer) fires inside
    # lv_timer_handler() and owns the screensaver; this loop never touches it.
    while True:
        lv.lvgl_pump(duration_ms=PUMP_MS)
        ev = lv.poll_for_result()
        if ev is not None:
            print(f"[ss-idle]   result: {ev}")
            kind, index, _label = ev
            if kind == "button_selected" and index >= 0:
                print("[ss-idle] list selection received on the restored screen — PASS.")
                return 0

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\n[ss-idle] interrupted.")
    finally:
        try:
            lv.native_display_shutdown()
            lv.lvgl_shutdown()
        except Exception:
            pass
