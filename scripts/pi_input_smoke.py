#!/usr/bin/env python3
import sys
import time
from pathlib import Path

# Allow running directly from repo checkout without package install.
REPO_ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = REPO_ROOT / "src"
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))

from seedsigner_lvgl.platform.pi_zero import PiButtonsBackend


def main() -> int:
    try:
        backend = PiButtonsBackend()
        backend.init_gpio()
    except Exception as e:
        print(f"ERROR: This script must run on Pi with RPi.GPIO installed ({e})")
        return 2

    watch_keys = ["UP", "DOWN", "LEFT", "RIGHT", "PRESS", "KEY1", "KEY2", "KEY3"]
    print("Pi input smoke running. Press D-pad/center or KEY1/2/3. Ctrl+C to exit.")

    try:
        while True:
            event = backend.poll_event(watch_keys)
            if event is not None:
                key, event_type, ts_ms = event
                print(f"[{ts_ms}] {event_type:<6} {key}")
            time.sleep(0.01)
    except KeyboardInterrupt:
        print("\nExiting input smoke.")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
