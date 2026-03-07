# Hardware Profile (Pi Zero target)

Extracted from upstream SeedSigner Python hardware implementation.

## Sources
- Display driver: https://github.com/SeedSigner/seedsigner/blob/dev/src/seedsigner/hardware/displays/ST7789.py
- Buttons driver: https://github.com/SeedSigner/seedsigner/blob/dev/src/seedsigner/hardware/buttons.py

## Display (ST7789) GPIO + SPI

Display resolution:
- **240 x 240** pixels

From `ST7789.py` (GPIO BOARD numbering):
- `DC` = **22**
- `RST` = **13**
- `BL` (backlight) = **18**

SPI configuration:
- Bus/device: `spidev.SpiDev(0, 0)`
- `max_speed_hz` = **40_000_000**

## Buttons / Joystick GPIO (target mapping)

For modern Raspberry Pi 40-pin header path (`GPIO.RPI_INFO['P1_REVISION'] == 3`, BOARD numbering):
- `KEY_UP` = **31**
- `KEY_DOWN` = **35**
- `KEY_LEFT` = **29**
- `KEY_RIGHT` = **37**
- `KEY_PRESS` (joystick center click) = **33**
- `KEY1` = **40**
- `KEY2` = **38**
- `KEY3` = **36**

## Input timing behavior (hold-repeat)

From `buttons.py`:
- `first_repeat_threshold` = **225 ms**
- `next_repeat_threshold` = **250 ms**
- polling sleep interval in wait loop = **10 ms**

## UI scaling / pixel parity

- `PX_MULTIPLIER` must be set to **100** for Pi Zero builds.
- `PX_MULTIPLIER=100` means no scaling relative to original SeedSigner pixel values.
- This preserves baseline pixel-parity behavior for the 240x240 target.

## Project decision for `seedsigner-raspi-lvgl`

- We are targeting one specific Pi hardware profile.
- Hard-code the 40-pin BOARD mapping above in the Pi binding layer.
- Preserve hold-repeat timing semantics compatible with `buttons.py`.
- Compile Pi Zero target with `PX_MULTIPLIER=100`.
