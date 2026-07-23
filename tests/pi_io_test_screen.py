#!/usr/bin/env python3
"""Pi hardware test: exercise the native io_test_screen (RASPI-2 gaps 1+2).

Run on the Pi Zero with the built .so in src/:
    PYTHONPATH=src python tests/pi_io_test_screen.py
or, stopping/restarting the running app around it (dev image #114):
    bash tests/run_pi_io_test.sh

Verify by hand (the point of a hardware self-test):
  - The "I/O Test" screen renders on the ST7789: a D-pad pictogram (left),
    KEY1 camera glyph / KEY2 (blank) / KEY3 "Exit" (right).
  - Joystick up/down/left/right + center CLICK each FLASH the matching D-pad
    control. The screen reads the keypad itself and emits no event for these.
  - KEY1 / KEY2 / KEY3 each flash their button AND print an ('aux_key', 0,
    'KEYn') event here. That is gap 2 -- the strong seedsigner_lvgl_on_aux_key
    override routing aux keys into the result queue.
  - KEY1 simulates a camera grab: the "Capturing image..." band appears for ~1s,
    then KEY2's label becomes "Clear". KEY2 then clears it (band hidden, label
    blank). That is gap 1 -- io_test_set_capture_state CAPTURING -> CAPTURED -> IDLE.
  - KEY3 exits the test.

NOT covered here (gap 3, deferred to SCREENS-9): there is no live camera square
behind the chrome. The Pi has no board adapter and io_test_screen owns no pixel
plane, so the center stays dark. That is expected until the screens-repo change.

Ctrl+C also exits (the app is restarted by run_pi_io_test.sh on exit).
"""

from __future__ import annotations

import os
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = REPO_ROOT / "src"
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))

import seedsigner_lvgl_screens as lv

# Short pump cycles: joystick/key flashes animate live and Ctrl+C stays responsive.
PUMP_MS = 100
# Safety: auto-exit (and let the app restart) after this many seconds, so an
# unattended run never strands the display. 0/unset = no limit (manual use).
MAX_SECONDS = float(os.environ.get("IO_TEST_MAX_SECONDS", "0") or "0")
# How many pump cycles to hold the "Capturing image..." band so it is clearly seen.
CAPTURING_HOLD_CYCLES = 10  # ~1.0s at PUMP_MS

# io_test_set_capture_state states (mirror seedsigner.h io_test_capture_state_t).
CAPTURE_IDLE = 0
CAPTURE_CAPTURING = 1
CAPTURE_CAPTURED = 2

IO_TEST_CFG = {
    "top_nav": {"title": "I/O Test"},
    "capturing_text": "Capturing image...",
    "clear_label": "Clear",
    "exit_label": "Exit",
}


def pump(cycles: int = 1) -> None:
    for _ in range(cycles):
        lv.lvgl_pump(duration_ms=PUMP_MS)


def simulate_capture() -> None:
    """KEY1 = grab a still via the native still-grab primitive (RASPI-2 gap 3).

    Mirrors the APP-6 KEY1 seam contract: CAPTURING band up, io_test_camera_start()
    to feed the io_test plane from the native engine, pump the hold so frames flow in
    (camera_engine_pump_consume runs each lvgl_pump), io_test_camera_stop() to freeze
    the last frame, then CAPTURED. If the camera can't start, leave the square dark.
    """
    print("    KEY1 -> capturing: 'Capturing image...' band shown")
    lv.io_test_set_capture_state(CAPTURE_CAPTURING)
    grabbing = False
    try:
        lv.io_test_camera_start()
        grabbing = True
        print("    KEY1 -> camera grab started (live frames into the plane)")
    except OSError as e:
        print(f"    KEY1 -> camera unavailable ({e}); square stays dark")
    try:
        pump(CAPTURING_HOLD_CYCLES)
    finally:
        if grabbing:
            lv.io_test_camera_stop()
            print("    KEY1 -> camera grab stopped (last frame frozen in the plane)")
    lv.io_test_set_capture_state(CAPTURE_CAPTURED)
    print("    KEY1 -> captured: KEY2 label is now 'Clear'")


def main() -> int:
    print("[io-test] Initializing LVGL + native display + input...")
    lv.lvgl_init(hor_res=240, ver_res=240)
    lv.native_display_init()  # also brings up the keypad input (native_input_init)
    lv.set_flush_mode("native")

    lv.clear_result_queue()
    lv.io_test_screen(IO_TEST_CFG)
    print("[io-test] I/O Test screen up.")
    print("  - Joystick + click: each D-pad control should flash (no event).")
    print("  - KEY1 grab / KEY2 clear / KEY3 exit -- each prints an aux_key event.")
    print("  - NOTE: no live camera square (gap 3 / SCREENS-9) -- center stays dark.")
    print("  Ctrl+C to exit.\n")

    captured = False
    start = time.monotonic()
    try:
        while True:
            pump()
            if MAX_SECONDS and (time.monotonic() - start) > MAX_SECONDS:
                print(f"\n[io-test] Max runtime {MAX_SECONDS:.0f}s reached; exiting.")
                break
            event = lv.poll_for_result()
            if event is None:
                continue
            kind, index, label = event
            if kind != "aux_key":
                print(f"    (unexpected event: {event})")
                continue
            print(f"    => aux_key: {label}")
            if label == "KEY1":
                simulate_capture()
                captured = True
            elif label == "KEY2":
                if captured:
                    lv.io_test_set_capture_state(CAPTURE_IDLE)
                    captured = False
                    print("    KEY2 -> cleared: band hidden, KEY2 label blank (IDLE)")
                else:
                    print("    KEY2 -> (nothing captured yet)")
            elif label == "KEY3":
                print("    KEY3 -> exit")
                break
    except KeyboardInterrupt:
        print("\n[io-test] Interrupted.")
    finally:
        lv.native_display_shutdown()
        lv.lvgl_shutdown()
        print("[io-test] Done.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
