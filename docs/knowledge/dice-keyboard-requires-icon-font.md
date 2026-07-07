# Dice-roll keyboard must request the icon font (`keyboard_font: "icon"`)

**Status: open coordination item.** The same fix is needed on the ESP32/MicroPython side
(`seedsigner-micropython-builder`) — a matching note lives there; keep the two in sync. This
was first observed on the ESP32-P4 build; the Pi Zero binding has the same pass-through shape,
so it will hit the same issue when the dice-roll flow is wired.

## Symptom
The dice-roll entry keyboard renders blank / tofu keys instead of the six FontAwesome dice
faces — "the FontAwesome dice glyphs aren't in the native keyboard font."

## Root cause — font *selection*, not a missing font
The FontAwesome dice glyphs (U+F522–F528) **are** baked into the shared seedsigner icon font at
every size. In the pinned `seedsigner-lvgl-screens` submodule (`sources/seedsigner-lvgl-screens`),
`components/seedsigner/fonts/seedsigner_icons_24_4bpp*.c` are generated with
`--range 0xf030,0xf11c,0xf522-0xf528`. So whichever size the Pi Zero display profile selects for
`ICON_FONT__SEEDSIGNER` contains the dice glyphs. The desktop screenshot generator renders the
dice keyboard correctly, proving the font content is present.

The problem is which font the keyboard selects. `keyboard_screen` (screens repo `seedsigner.cpp`)
draws the keys in the default `KEYBOARD_FONT` (Inconsolata — **no** dice glyphs) **unless** the
cfg sets `keyboard_font: "icon"` (or `"fontawesome"`), which switches the key glyph font to
`ICON_FONT__SEEDSIGNER` (the baked icon font that has the dice glyphs). Without the flag the keys
fall back to Inconsolata and render blank.

The CPython binding `py_keyboard_screen` (`native/python_bindings/module.cpp`) is a **pure
pass-through**: it converts the caller's cfg dict to JSON and calls `keyboard_screen`, injecting
nothing. So the **caller** that builds the dice-roll keyboard cfg is responsible for the font.

## What to do
The cfg dict passed to the Pi Zero `keyboard_screen(cfg_dict)` for the dice-roll flow must include:
- `"keyboard_font": "icon"`,
- the dice glyph strings (U+F523–F528) as the `keys`, and
- `"keys_to_values"` mapping each glyph → its numeric value (`"1"`..`"6"`).

Reference: the screens repo `tools/scenarios/scenarios.json`, `keyboard_screen` → `dice`
variation (renders correctly on desktop).

## Cross-platform
Ideally the shared Python business logic sets `keyboard_font: "icon"` once for the dice flow so
both the CPython (Pi Zero) and MicroPython (ESP32) bindings inherit it. See the twin note in
`seedsigner-micropython-builder/docs/knowledge/dice-keyboard-requires-icon-font.md`.
