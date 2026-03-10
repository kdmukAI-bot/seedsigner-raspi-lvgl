#!/usr/bin/env python3
"""Sweep native ST7789 color order (BGR vs RGB) on Pi."""

from __future__ import annotations

import time

import seedsigner_lvgl as lv


def run_once(bgr: bool) -> None:
    print(f"\n=== TEST bgr={bgr} ===")
    lv.native_display_init(
        width=240,
        height=240,
        dc_pin=25,
        rst_pin=27,
        bl_pin=24,
        spi_path="/dev/spidev0.0",
        spi_speed_hz=40_000_000,
        bgr=bgr,
    )
    lv.native_display_test_pattern()
    print("Displayed color bars for 2s")
    time.sleep(2)
    lv.native_display_shutdown()


def main() -> int:
    for bgr in (True, False):
        try:
            run_once(bgr)
        except Exception as e:
            print(f"ERROR bgr={bgr}: {e}")
            try:
                lv.native_display_shutdown()
            except Exception:
                pass
    print("\nDone. Pick the visually correct mode and we will lock it as default.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
