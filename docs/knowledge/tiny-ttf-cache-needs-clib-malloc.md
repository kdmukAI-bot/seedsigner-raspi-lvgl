# Pi Zero: LVGL must use CLIB malloc (the tiny_ttf glyph cache is on by default)

**Status: APPLIED.** `("LV_USE_STDLIB_MALLOC", "LV_STDLIB_CLIB")` is in the `define_macros` of
[`setup.py`](../../setup.py). This is **required**, not optional — do not remove it.

## Why

The shared font code in c-modules (`components/seedsigner/font_registry.cpp`, `gui_constants.cpp`) creates
its tiny_ttf fonts with the glyph/draw cache **enabled by default** (`SEEDSIGNER_TTF_CACHE_SIZE = 256` in
`gui_constants.h`). The cache retains rasterized glyph bitmaps for redraw/scroll speed.

Without the allocator override, the `seedsigner_lvgl_screens` extension compiles under `LV_CONF_SKIP=1`,
whose default is `LV_USE_STDLIB_MALLOC = LV_STDLIB_BUILTIN` with a **fixed 64 KB pool** (`LV_MEM_SIZE`).
LVGL would then see only 64 KB — **the board's 512 MB never reaches it.** The cache's retained CJK bitmaps
exhaust 64 KB, the next `lv_malloc` returns NULL, and LVGL's default `LV_ASSERT_MALLOC` →
`LV_ASSERT_HANDLER` = `while(1);` busy-loops → **CPU-bound hang.** Root cause (with repro) in the c-modules
submodule:
[`sources/seedsigner-c-modules/docs/knowledge/tiny-ttf-cache-spin-root-cause.md`](../../sources/seedsigner-c-modules/docs/knowledge/tiny-ttf-cache-spin-root-cause.md).

`LV_USE_STDLIB_MALLOC = LV_STDLIB_CLIB` routes `lv_malloc` to glibc `malloc`, so the full system heap is
available, the cache fits, and the spin cannot occur. (Alternatively `LV_STDLIB_BUILTIN` with
`LV_MEM_SIZE` raised to a few MB — CLIB is simpler and matches the c-modules desktop tools.)

## Verification

Reproduce/verify with the standalone harness referenced in the root-cause doc: a 64 KB builtin pool +
cache enabled spins; CLIB malloc (or a multi-MB pool) renders the full CJK corpus with the cache on and no
spin.

## Note

This must move **in lockstep** with the c-modules submodule pin: any commit that bumps c-modules carries
the cache-on-by-default font code, so this override must already be present (it is). If a future
memory-tight variant ever needs the cache off, build c-modules with `-DSEEDSIGNER_TTF_CACHE_SIZE=0` rather
than reverting this allocator change.
