#!/usr/bin/env python3
"""Pi smoke test for native LVGL + native ST7789 driver path (no Python flush relay)."""

from __future__ import annotations

import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = REPO_ROOT / "src"
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))


def main() -> int:
    import seedsigner_lvgl as lv

    # Native backend owns SPI+GPIO directly.
    lv.native_display_init(width=240, height=240, dc_pin=25, rst_pin=27, bl_pin=24, spi_path="/dev/spidev0.0", spi_speed_hz=40_000_000, bgr=False, lvgl_swap_bytes=True)
    lv.native_debug_config(enabled=True, flush_log_limit=20)
    lv.set_flush_mode("native")

    lv.lvgl_init(hor_res=240, ver_res=240)
    lv.clear_result_queue()

    cfg = {
        "top_nav": {"title": "Native Driver", "show_back_button": True, "show_power_button": True},
        "button_list": ["Alpha", "Beta", "Gamma"],
        "wait_timeout_ms": 10000,
        "allow_timeout_fallback": False,
    }

    print("[native-driver-smoke] render + wait for event")
    lv.button_list_screen(cfg)

    deadline = time.time() + 10.0
    event = None
    while time.time() < deadline:
        event = lv.poll_for_result()
        if event is not None:
            break
        time.sleep(0.05)

    print("[native-driver-smoke] event:", event)

    lv.lvgl_shutdown()
    lv.native_display_shutdown()
    return 0 if event is not None else 3


if __name__ == "__main__":
    raise SystemExit(main())
