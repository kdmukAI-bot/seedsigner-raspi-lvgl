#!/usr/bin/env python3
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

    lv.native_display_init(width=240, height=240, dc_pin=25, rst_pin=27, bl_pin=24, spi_path="/dev/spidev0.0", spi_speed_hz=40_000_000, bgr=False)
    lv.native_display_test_pattern()
    print("[native-test-pattern] rendered RGB565 bands")
    time.sleep(2)
    lv.native_display_shutdown()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
