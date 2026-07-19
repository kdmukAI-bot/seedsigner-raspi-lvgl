# Pi Zero Hardware Test Guide

Validate that the built native extension runs on real Pi Zero hardware
with display output and joystick/button input.

## Preconditions

- Pi Zero with SeedSigner hat (ST7789 display + joystick + KEY1/KEY2/KEY3)
- Python 3.10 on the Pi (ABI: `cpython-310-arm-linux-gnueabihf`)
- Built `.so` extension artifact from CI or local build

## Setup

### Option A: Clone the repo on Pi

Clone the repo on the Pi and place the built `.so` in `src/`:

```bash
git clone https://github.com/kdmukAI-bot/seedsigner-raspi-lvgl.git
cd seedsigner-raspi-lvgl
# Copy the .so into src/ (from CI artifact download, scp, etc.)
cp /path/to/seedsigner_lvgl_screens.cpython-310-arm-linux-gnueabihf.so src/
```

This gives you the test scripts and the correct directory layout.

### Option B: Copy individual files

Copy the minimum set of files to a directory on the Pi:

```bash
rsync -avz \
  src/seedsigner_lvgl_screens.cpython-310-arm-linux-gnueabihf.so \
  tests/pi_input_hardware_test.py \
  pi@<pi-host>:/home/pi/seedsigner-raspi-lvgl-test/
```

The `.so` must be importable from `PYTHONPATH`. When using Option B,
set `PYTHONPATH=.` from the directory containing the `.so`.

## 1) Verify Python ABI

```bash
python3 -c "import platform, sysconfig; print(platform.machine(), sysconfig.get_config_var('SOABI'))"
```

Expected: `armv6l cpython-310-arm-linux-gnueabihf`

## 2) Verify import

```bash
PYTHONPATH=src python3 -c "import seedsigner_lvgl_screens; print('OK')"
```

If this fails, see the diagnostics section below.

## 3) Hardware input test (display + joystick)

This is the primary end-to-end validation for the hardware input wiring.

```bash
PYTHONPATH=src python3 tests/pi_input_hardware_test.py
```

The test renders screens on the ST7789 display and waits for real input:

1. **Screensaver** (bouncing logo) — press any button to dismiss
2. **Main Menu** (2x2 grid) — verify all 4 joystick directions + center click
3. **"Vertical List"** (Alpha/Beta/Gamma) — UP/DOWN moves focus, PRESS
   selects, UP past the first button reaches Back in the top nav
4. **"Two Items"** — same behavior with fewer buttons
5. **"Single Item"** — edge case
6. **Main-menu loop** — repeats until Ctrl+C

After each interaction, the script prints the event tuple:
- `("button_selected", 0, "Alpha")` — body button selected
- `("button_selected", 1000, "back")` — back pressed via top nav (RET_CODE__BACK_BUTTON sentinel)

### What to verify

- Display renders the screen with title and buttons
- Joystick UP/DOWN visually moves focus highlight between buttons
- UP from the first button moves focus to the top-nav Back control
- DOWN from top-nav returns focus to the body
- Center press (PRESS) activates the focused item and prints the event
- KEY1/KEY2/KEY3 trigger aux key actions
- The correct button label and index appear in the printed event

### If the screen doesn't render

- Check SPI is enabled: `ls /dev/spidev0.0`
- Check GPIO access: `ls /dev/gpiochip0`
- Run as root if permission errors occur: `sudo PYTHONPATH=src python3 tests/pi_input_hardware_test.py`

## 4) Import failure diagnostics

If the extension fails to import, collect:

```bash
python3 -c "import sys, sysconfig, platform; print(sys.version); print(platform.machine()); print(sysconfig.get_config_var('SOABI'))"
file src/seedsigner_lvgl_screens.cpython-310-arm-linux-gnueabihf.so
readelf -A src/seedsigner_lvgl_screens.cpython-310-arm-linux-gnueabihf.so | head -20
ldd src/seedsigner_lvgl_screens.cpython-310-arm-linux-gnueabihf.so || true
```

Common issues:
- **Wrong Python version**: `.so` built for 3.10 but Pi has 3.11
- **Architecture mismatch**: `.so` built as ARMv7 but Pi Zero is ARMv6
- **Missing symbols**: lvgl-screens source file not included in build
