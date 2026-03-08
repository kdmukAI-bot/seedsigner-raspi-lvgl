#!/usr/bin/env python3
"""Pi hardware smoke test for ST7789 backend.

Runs on Raspberry Pi target hardware only.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

# Allow running directly from repo checkout without package install.
REPO_ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = REPO_ROOT / "src"
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="ST7789 Pi hardware smoke test")
    p.add_argument("--hold-seconds", type=float, default=1.5, help="seconds to keep each test frame visible")
    return p.parse_args()


def main() -> int:
    args = parse_args()

    try:
        import RPi.GPIO as GPIO  # type: ignore
        import spidev  # type: ignore
    except Exception as e:
        print(f"ERROR: This script must run on Pi with RPi.GPIO + spidev installed ({e})")
        return 2

    from seedsigner_lvgl.platform.pi_zero import ST7789Display

    spi = spidev.SpiDev(0, 0)
    display = ST7789Display(gpio=GPIO, spi=spi)

    print("[1/4] Initializing display...")
    display.init()

    w = display.cfg.width
    h = display.cfg.height
    frame_len = w * h * 2

    print("[2/4] White frame")
    display.show_rgb565_frame(bytes([0xFF]) * frame_len)
    time.sleep(args.hold_seconds)

    print("[3/4] Black frame")
    display.show_rgb565_frame(bytes([0x00]) * frame_len)
    time.sleep(args.hold_seconds)

    print("[4/4] Checkerboard frame")
    data = bytearray(frame_len)
    for y in range(h):
        for x in range(w):
            i = (y * w + x) * 2
            white = ((x // 16) + (y // 16)) % 2 == 0
            if white:
                data[i] = 0xFF
                data[i + 1] = 0xFF
            else:
                data[i] = 0x00
                data[i + 1] = 0x00
    display.show_rgb565_frame(bytes(data))
    time.sleep(args.hold_seconds)

    print("Smoke test complete.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
