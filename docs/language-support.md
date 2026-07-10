# Language support (Pi Zero)

How multi-language rendering works in the CPython extension, and how this repo relates to
the language-pack producer. Deployment mechanics (getting packs onto the device) live in
[`dev-device-deployment.md`](dev-device-deployment.md).

## Two independent layers

Language support is split into two things that do **not** depend on each other:

| Layer | Owner | Source of truth |
|---|---|---|
| **Fonts / script rendering** | this repo's `.so` | language packs (per-locale subset fonts + pre-shaped glyph runs) |
| **Translated text** | the app's gettext | catalogs at `get_catalog_root()` |

On CPython/Pi, `get_catalog_root()` returns the app's bundled `resources/seedsigner-translations/l10n`
— so translated *text* is independent of this repo and the packs. (The packs also carry a
`LC_MESSAGES/messages.mo`, but that copy is the **ESP32** text source, where there is no bundled
submodule.) Text is looked up the same way regardless of which screen (LVGL or legacy PIL)
consumes it, so it flows through both unchanged.

## Fonts are manifest-driven (no baked table)

The screens layer bakes only the **English/Latin floor**; every non-English locale is fully
self-described by its pack's `manifest.json`. There is no compiled locale→font table. So:

1. `discover_locale_packs(font_dir="lang-packs")` scans `<font_dir>/<locale>/manifest.json` and
   registers each via `ss_register_pack_manifest` (defensive: skips desktop-OS junk, tolerates
   half-copied/malformed packs, never crashes). The app calls this at runtime init and before
   the picker.
2. `set_locale(locale, font_dir="lang-packs")` loads that locale's fonts from its registered
   manifest + pack files. It **fails closed** (returns `False`, restores the English floor) if a
   pack font can't be loaded.
3. A locale with no registered pack renders on the **baked English (Latin) floor** — so a
   non-Latin locale with no pack shows Latin/tofu, never a crash.

`set_locale`'s boolean is coarse (it also returns `True` for an unregistered locale that falls to
the floor); the meaningful signal is that a *registered* locale returning `True` loaded its script
fonts.

## The picker

`settings_locale_picker_screen(cfg)` renders each language's name in its own script: Latin names as live
text (the baked floor covers them), non-Latin names as pre-rendered **endonym images** fetched
from the pack (`endonym_<height>.bin`) — so the picker never needs every script's font resident.
The app builds the rows from `list_available_locales()` (the font packs present) unioned with the
locales it has catalogs for; a selection returns `button_selected(index)`.

## Relationship to `seedsigner-language-packs`

Packs are produced by the dedicated
[`seedsigner-language-packs`](https://github.com/kdmukAI-bot/seedsigner-language-packs) repo
(format + `locales.h` policy + fonts + reproducible builder). **This repo does not submodule or
build that repo.** Packs are materialized into the **app's** `src/lang-packs` (by running the pack
builder with `--out-dir $SS_APP_DIR/src/lang-packs`), and this repo's `deploy-dev.sh` copies that
payload to the device. Packs are purely additive: **no packs = a valid English-only device.**

A pack directory is one self-contained unit per locale:

```
<locale>/
    manifest.json           # self-describing: chain, rtl, shaping, endonym, endonym_images, ...
    <locale>.ttf            # subset font (CJK/shaping) | <locale>_{regular,semibold}.ttf (Latin block-range)
    runs.bin                # pre-shaped glyph runs (complex scripts: hi/th/ur)
    endonym_240/320/480.bin # native-name images per display height
    LC_MESSAGES/messages.mo # translation catalog (ESP32 text source; unused for text on Pi)
```

The render layer consumes only `chain` / `rtl` / `shaping` (+ per-role sizes derived from `chain`)
and the endonym images from the manifest; the other fields are builder-only.

## Where it lives

- Bindings: `native/python_bindings/locale_packs.cpp` (`discover_locale_packs`, `list_available_locales`,
  `set_locale`, `unload_locale`; the `fs_pack_provider` filesystem seam) and
  `native/python_bindings/screens.cpp` (`settings_locale_picker_screen`).
- Screen implementations + the manifest loader: the `sources/seedsigner-lvgl-screens` submodule.
- Deploying packs to a device: [`dev-device-deployment.md`](dev-device-deployment.md).
</content>
