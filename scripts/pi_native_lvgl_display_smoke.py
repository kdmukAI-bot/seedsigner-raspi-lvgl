#!/usr/bin/env python3
"""Pi hardware smoke test for native LVGL flush -> ST7789 path."""

from __future__ import annotations

import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = REPO_ROOT / "src"
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))


def main() -> int:
    try:
        import RPi.GPIO as GPIO  # type: ignore
        import spidev  # type: ignore
    except Exception as e:
        print(f"ERROR: must run on Pi with RPi.GPIO + spidev ({e})")
        return 2

    import seedsigner_lvgl as lv
    from seedsigner_lvgl.platform.pi_zero import ST7789Display, ST7789Config

    cfg = ST7789Config(width=240, height=240)
    spi = spidev.SpiDev(0, 0)
    display = ST7789Display(gpio=GPIO, spi=spi, cfg=cfg)
    display.init()

    lv.bind_display(display)
    lv.lvgl_init(hor_res=cfg.width, ver_res=cfg.height)
    lv.clear_result_queue()

    screen_cfg = {
        "top_nav": {"title": "Native LVGL", "show_back_button": True, "show_power_button": True},
        "button_list": ["Alpha", "Beta", "Gamma"],
    }

    print("[native-lvgl-smoke] rendering button_list_screen (press a button)")
    lv.button_list_screen(screen_cfg)

    deadline = time.time() + 10.0
    event = None
    while time.time() < deadline:
        event = lv.poll_for_result()
        if event is not None:
            break
        time.sleep(0.05)

    print("[native-lvgl-smoke] event:", event)
    lv.lvgl_shutdown()
    return 0 if event is not None else 3


if __name__ == "__main__":
    raise SystemExit(main())
