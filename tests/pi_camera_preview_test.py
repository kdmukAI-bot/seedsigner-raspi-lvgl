#!/usr/bin/env python3
"""Camera-preview pixel-path hardware check (Stage 1, no camera required).

Run on the Pi Zero with the rebuilt .so:
    cd /home/pi/seedsigner-raspi-lvgl && python3 tests/pi_camera_preview_test.py

Proves the piece the headless/desktop tests cannot: that a raw RGB565 frame pushed
with camera_preview_set_frame() reaches the ST7789 with correct COLOR + ORIENTATION,
and that the portable overlay chrome composites on top. A synthetic color-bar frame
stands in for live camera pixels, so a channel/byte-order error is visually obvious.

Watch the display and confirm:
  1. Eight vertical bars L->R: RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA, WHITE, BLACK.
     If red<->blue are swapped (or the bars are byte-garbled), the frame byte order is
     wrong — fix the conversion, NOT this expectation (frames are LVGL-native RGB565).
  2. The overlay instruction line sits at the bottom over the bars.
  3. Every ~1s the status bar advances (10%..100%) with the green/gray status dot,
     then wraps — the overlay updating over a static frame.
  4. Joystick LEFT (or Ctrl+C) exits.

FLUSH MODES: this drives NATIVE flush (the C display_st7789 driver owns the panel).
That validates the frame's byte order end-to-end through the native path. The app
currently runs LVGL in PYTHON flush (blended display); that path is covered for free
because camera pixels ride the exact same RGB565->flush path as every other LVGL
widget — so if normal LVGL screens render correct colors in the app (they do), a
frame fed as LVGL-native RGB565 renders correctly there too. The one rule that keeps
this true: NEVER pre-swap the frame for the panel — hand LVGL-native RGB565 and let
the active flush driver own byte order/BGR.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = REPO_ROOT / "src"
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))

import seedsigner_lvgl_screens as lv

W = H = 240
PUMP_MS = 100

# 8 color bars, left->right. (R, G, B) 8-bit; channel-order errors are obvious.
BARS = [
    (0xFF, 0x00, 0x00),  # red
    (0x00, 0xFF, 0x00),  # green
    (0x00, 0x00, 0xFF),  # blue
    (0xFF, 0xFF, 0x00),  # yellow
    (0x00, 0xFF, 0xFF),  # cyan
    (0xFF, 0x00, 0xFF),  # magenta
    (0xFF, 0xFF, 0xFF),  # white
    (0x00, 0x00, 0x00),  # black
]


def _rgb565_le(r: int, g: int, b: int) -> bytes:
    """One pixel as LVGL-native RGB565, little-endian (what LVGL stores; the native
    flush byte-swaps to the panel). NOT pre-swapped for the panel."""
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return struct.pack("<H", v)


def _make_color_bar_frame() -> bytes:
    bar_w = W // len(BARS)
    row = bytearray()
    for x in range(W):
        idx = min(x // bar_w, len(BARS) - 1)
        row += _rgb565_le(*BARS[idx])
    return bytes(row) * H  # every row identical -> vertical bars


def main() -> int:
    print("[cam-preview] init LVGL + native ST7789...")
    lv.lvgl_init(hor_res=W, ver_res=H)
    lv.native_display_init()
    lv.set_flush_mode("native")
    lv.set_screensaver_timeout(0)  # belt-and-suspenders; the screen also opts out

    frame = _make_color_bar_frame()
    assert len(frame) == W * H * 2, len(frame)

    lv.clear_result_queue()
    lv.camera_preview_screen({"instructions_text": "< back  |  Scan a QR code"})
    lv.camera_preview_set_frame(frame)
    lv.lvgl_pump(duration_ms=PUMP_MS)
    print("[cam-preview] color bars pushed. Expect R G B Y C M W K, left->right.")
    print("[cam-preview] >>> watch the overlay progress bar cycle; joystick LEFT or Ctrl+C to exit.")

    pct = 0
    ticks = 0
    try:
        while True:
            lv.camera_preview_set_frame(frame)  # re-push each loop (simulates a live feed)
            lv.lvgl_pump(duration_ms=PUMP_MS)

            ticks += 1
            if ticks % 10 == 0:  # ~1s at PUMP_MS=100
                pct = (pct + 10) % 110
                status = 1 if (pct // 10) % 2 else 2  # alternate added(green)/repeated(gray)
                lv.camera_preview_set_progress(pct, status)

            ev = lv.poll_for_result()
            if ev is not None:
                print(f"[cam-preview]   result: {ev}")
                kind, _index, _label = ev
                if kind in ("topnav_back",):
                    print("[cam-preview] back received — PASS.")
                    return 0
    finally:
        lv.camera_preview_close()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\n[cam-preview] interrupted.")
    finally:
        try:
            lv.native_display_shutdown()
            lv.lvgl_shutdown()
        except Exception:
            pass
