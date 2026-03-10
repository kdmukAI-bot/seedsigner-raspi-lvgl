#!/usr/bin/env python3
"""Interactive GPIO mapping diagnostic for Pi input controls.

Purpose:
- confirm which physical inputs are active in BOARD numbering
- confirm corresponding BCM line mapping used by native gpiochip path

Run on Pi with controls attached.
"""

from __future__ import annotations

import time

try:
    import RPi.GPIO as GPIO  # type: ignore
except Exception as e:  # pragma: no cover
    raise SystemExit(f"ERROR: RPi.GPIO unavailable: {e}")

BOARD_MAP = {
    "UP": 31,
    "DOWN": 35,
    "LEFT": 29,
    "RIGHT": 37,
    "PRESS": 33,
    "KEY1": 40,
    "KEY2": 38,
    "KEY3": 36,
}

# Native module.cpp gpiochip offsets (BCM numbering)
BCM_MAP = {
    "UP": 6,
    "DOWN": 19,
    "LEFT": 5,
    "RIGHT": 26,
    "PRESS": 13,
    "KEY1": 21,
    "KEY2": 20,
    "KEY3": 16,
}

EXPECTED_BOARD_TO_BCM = {
    29: 5,
    31: 6,
    33: 13,
    35: 19,
    36: 16,
    37: 26,
    38: 20,
    40: 21,
}


def setup(mode: int, pins: list[int]) -> None:
    GPIO.cleanup()
    GPIO.setwarnings(False)
    GPIO.setmode(mode)
    for p in pins:
        GPIO.setup(p, GPIO.IN, pull_up_down=GPIO.PUD_UP)


def wait_for_press(pin: int, timeout_s: float = 4.0) -> bool:
    end = time.time() + timeout_s
    while time.time() < end:
        if GPIO.input(pin) == 0:  # active-low
            # debounce confirm
            time.sleep(0.02)
            if GPIO.input(pin) == 0:
                return True
        time.sleep(0.005)
    return False


def run_phase(label: str, mode: int, mapping: dict[str, int]) -> dict[str, bool]:
    print(f"\n=== {label} ===")
    setup(mode, list(mapping.values()))
    detected: dict[str, bool] = {}

    for name, pin in mapping.items():
        input(f"Press and hold {name} (pin {pin}) then hit Enter...")
        ok = wait_for_press(pin)
        detected[name] = ok
        print(f"  {name:<6} pin={pin:<2} detected={'YES' if ok else 'NO'}")
    return detected


def main() -> int:
    print("Pi GPIO Mapping Diagnostic")
    print("Active-low expected: pressed == 0")

    print("\nExpected BOARD->BCM conversion:")
    for k, bpin in BOARD_MAP.items():
        print(f"  {k:<6} BOARD {bpin:>2} -> BCM {EXPECTED_BOARD_TO_BCM[bpin]:>2} (native uses {BCM_MAP[k]:>2})")

    board_detect = run_phase("BOARD mode (RPi.GPIO)", GPIO.BOARD, BOARD_MAP)
    bcm_detect = run_phase("BCM mode (native-equivalent lines)", GPIO.BCM, BCM_MAP)

    GPIO.cleanup()

    print("\n=== Summary ===")
    bad = False
    for k in BOARD_MAP:
        b = board_detect[k]
        c = bcm_detect[k]
        status = "OK" if (b and c) else "MISMATCH"
        if status != "OK":
            bad = True
        print(f"  {k:<6} BOARD={'YES' if b else 'NO '}  BCM={'YES' if c else 'NO '}  => {status}")

    if bad:
        print("\nResult: mapping inconsistency detected. Do not patch blindly; use this table to drive pin remap.")
        return 2

    print("\nResult: BOARD and BCM detections align for all controls.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
