# Custom large-icon status screen: glyph must be in the baked hero-icon PUA range

## What "custom large icon" is
`large_icon_status_screen` (screens repo `seedsigner.cpp`) accepts a `status_type`:
`"success" | "warning" | "dire_warning" | "error"` select a baked icon + color, and
`"custom"` lets the **caller** supply the hero glyph and color at call time via
`cfg["icon"]` (a raw glyph string, same convention as button/top-nav icons) and
`cfg["icon_color"]` (hex). One screen then renders any large-icon prompt — it is what
powers PSBTFinalize's SIGN screen and the microSD notification without a bespoke entry
point.

## It needs no new Pi binding
It is a *mode* of an already-bound screen, not a new screen. The CPython binding
`py_large_icon_status_screen` (`native/python_bindings/module.cpp`) is a **pure
pass-through**: it converts the caller's cfg dict to JSON and calls
`large_icon_status_screen`, injecting nothing. So `status_type:"custom"` + `icon` +
`icon_color` already reach the native screen through the existing binding — no C change,
no `setup.py` change, no LVGL flag.

## The one real constraint — the glyph must be in the baked hero font
On the Pi the hero icon renders in the compiled **48px** seedsigner icon font
(`components/seedsigner/fonts/seedsigner_icons_48_4bpp.c`; the 240 display profile maps
both `icon_large` and `icon_primary_screen` to it in `gui_constants.cpp`). Every baked
size of that font is generated with `--range 0xE900-0xE923` — the SeedSigner custom PUA
icon set (`SeedSignerIconConstants` in `gui_constants.h`, U+E900..U+E923). A caller glyph
**outside** 0xE900-0xE923 renders as tofu.

The two glyphs the custom mode is designed for are both in range, so they render:
- `SIGN` = U+E921 (PSBTFinalize)
- `MICROSD` = U+E91F (microSD notification)

This is the mirror image of the dice-keyboard issue
(`docs/knowledge/dice-keyboard-requires-icon-font.md`): there the glyphs are FontAwesome
codepoints (0xF522-F528) that live only in the **smaller** keyboard-font sizes, so the fix
was font *selection*. Here the hero font is fixed (48px); the constraint is that the custom
glyph be a SeedSigner PUA icon (0xE900-0xE923), which the whole `SeedSignerIconConstants`
set satisfies. A future custom prompt that wants a glyph outside that range would need the
range widened when the icon `.c` files are regenerated in the screens repo — the Pi build
just compiles whatever those files contain.

## Verifying
`tests/test_native_smoke.py::test_large_icon_custom_status_renders` builds the custom
screen headlessly with both SIGN and MICROSD and asserts the compiled path renders, so a
regression (e.g. a range shrink, or the binding gaining validation that strips the custom
fields) is caught in the ARMv6 build's pytest step.
