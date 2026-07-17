# The Pi runs two independent GPIO input readers — LVGL input fixes don't reach PIL screens

## The trap

During the LVGL migration, the Pi runs **two unrelated readers on the same GPIO pins**:

1. **This repo's LVGL keypad indev** — `native_input_read_cb` in
   `native/python_bindings/gpio_input.cpp`, reading via gpiochip ioctl line handles.
   Feeds LVGL-rendered screens through the group/indev machinery and the result queue.
2. **The app's Python `HardwareButtons`** (`seedsigner.hardware.buttons`) — RPi.GPIO via
   `/dev/gpiomem` (memory-mapped registers, not the character device, so it coexists with
   our line handles without `EBUSY`). Feeds every screen still rendered via PIL.

Which reader a screen uses is decided by the app's per-view routing (`IS_MICROPYTHON`
gates and the `run_screen` dispatch seam in the `seedsigner` repo), **not** by anything
in this repo. Nothing crashes or warns when you fix input behavior in one layer while the
symptomatic screen reads from the other.

## How this bit us (2026-07)

Symptom: activating an Address Explorer row with a still-held key opened the address QR,
which instantly self-dismissed (~500 ms). Two correct fixes shipped at the LVGL layer —
the screens repo's `lv_indev_wait_release()` attach latch (dd34e62) and this repo's
GPIO-source held-key gate (`gpio_input.cpp`) — and **neither changed on-device behavior**,
because the Pi's QR display was still the PIL `QRDisplayScreen`: its input came from
`HardwareButtons.wait_for()`, which by design re-reports a key held past its 225/250 ms
thresholds as new input and has no release requirement between screens. Each fix was
verified "correct yet ineffective" before the routing split was identified.

## The rule

Before touching input handling for a Pi-side symptom, first determine which reader the
symptomatic screen actually uses: find the app's route for that view (PIL class vs native
screen name at the `run_screen` seam / the view's `IS_MICROPYTHON` gate). Held-key or
debounce behavior must be fixed in the layer the screen reads from — or the screen must
be routed native (the chosen direction; `HardwareButtons` is being retired rather than
patched — see the app repo's `docs/_integration/pi-pil-input-cutover-todo.md`).

The hazard class disappears when the cutover completes and this repo's `.so` becomes the
single input owner.
