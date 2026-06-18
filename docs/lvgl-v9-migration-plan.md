# Plan: Integrate LVGL v9 Migration from seedsigner-c-modules PR #12

## Context

PR [kdmukAI-bot/seedsigner-c-modules#12](https://github.com/kdmukAI-bot/seedsigner-c-modules/pull/12) migrates the shared LVGL screen code from v8.4.0 to v9.5.0. That PR covers:
- Platform-agnostic screens (`components/seedsigner/*.cpp`)
- Desktop tools (screen_runner, screenshot_generator)
- Image assets (v9 `lv_image_dsc_t` format)
- LVGL submodule pointer (v8.4.0 → v9.5.0)

This project (`seedsigner-raspi-lvgl`) has its **own** LVGL display driver, input driver, and flush callback in `module.cpp` that use v8 APIs. Those must be updated to v9 to compile against the new submodule. The screen code changes are already handled by the c-modules PR — we only need to update the **platform backend** in this repo.

## Files to Modify

1. **`native/python_bindings/module.cpp`** — Display init, input init, flush callback, shutdown (bulk of work)
2. **`setup.py`** — LVGL source exclusion patterns and compile defines for v9

## Step 1: Update submodule to LVGL v9 branch

Point `sources/seedsigner-c-modules` to the PR #12 branch commit (`db445d08` on `feature/lvgl-v9-migration`), which includes the updated LVGL v9.5.0 nested submodule.

```bash
cd sources/seedsigner-c-modules
git fetch origin feature/lvgl-v9-migration
git checkout db445d08
git submodule update --init --recursive
```

## Step 2: `module.cpp` — Global variables (lines 44-46)

**Remove:**
```cpp
static lv_disp_draw_buf_t s_draw_buf;          // line 45
static lv_color_t s_buf1[240 * 240];           // line 46
```

**Replace with:**
```cpp
static uint8_t s_buf1[240 * 240 * 2];          // RGB565: 2 bytes/pixel, full-screen buffer
```

Rationale: In v9, `lv_color_t` is always 3 bytes (RGB888) regardless of display format. The draw buffer must be raw bytes sized for the actual pixel format (RGB565 = 2 bytes/pixel). The `lv_disp_draw_buf_t` struct no longer exists in v9.

## Step 3: `module.cpp` — Input read callback signature (line 350)

**Change:**
```cpp
static void native_input_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
```
**To:**
```cpp
static void native_input_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
```

The body stays the same — `LV_KEY_*` constants and `LV_INDEV_STATE_*` constants are unchanged in v9. Just update `(void)drv` → `(void)indev`.

## Step 4: `module.cpp` — Flush callback (lines 514-565)

**Change signature:**
```cpp
// v8:
static void flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
// v9:
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
```

**Inside the function:**
- Replace `sizeof(lv_color_t)` with `2` (hardcoded RGB565 bytes-per-pixel) on line 517
- The pixel data in `px_map` is already RGB565 since we'll set `LV_COLOR_FORMAT_RGB565` on the display
- For the native flush path: `const uint8_t *src = reinterpret_cast<const uint8_t *>(color_p)` → `const uint8_t *src = px_map`
- For the Python callback path: `PyBytes_FromStringAndSize(reinterpret_cast<const char *>(color_p), ...)` → `PyBytes_FromStringAndSize(reinterpret_cast<const char *>(px_map), ...)`
- Replace `lv_disp_flush_ready(disp_drv)` → `lv_display_flush_ready(disp)` (line 565)

## Step 5: `module.cpp` — Display init in `ensure_lvgl_runtime()` (lines 597-624)

**Remove v8 pattern (lines 604-612):**
```cpp
lv_disp_draw_buf_init(&s_draw_buf, s_buf1, NULL, (sizeof(s_buf1) / sizeof(s_buf1[0])));
static lv_disp_drv_t disp_drv;
lv_disp_drv_init(&disp_drv);
disp_drv.hor_res = s_hor_res;
disp_drv.ver_res = s_ver_res;
disp_drv.flush_cb = flush_cb;
disp_drv.draw_buf = &s_draw_buf;
lv_disp_drv_register(&disp_drv);
```

**Replace with v9 pattern:**
```cpp
lv_display_t *disp = lv_display_create(s_hor_res, s_ver_res);
lv_display_set_flush_cb(disp, flush_cb);
lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
lv_display_set_buffers(disp, s_buf1, NULL, sizeof(s_buf1),
                       LV_DISPLAY_RENDER_MODE_FULL);
```

**Remove v8 input init (lines 614-618):**
```cpp
static lv_indev_drv_t indev_drv;
lv_indev_drv_init(&indev_drv);
indev_drv.type = LV_INDEV_TYPE_KEYPAD;
indev_drv.read_cb = native_input_read_cb;
s_input_indev = lv_indev_drv_register(&indev_drv);
```

**Replace with v9 pattern:**
```cpp
s_input_indev = lv_indev_create();
lv_indev_set_type(s_input_indev, LV_INDEV_TYPE_KEYPAD);
lv_indev_set_read_cb(s_input_indev, native_input_read_cb);
```

## Step 6: `module.cpp` — `lv_layer_sys()` in shutdown (line 883)

Check if `lv_layer_sys()` exists in v9 headers. If not, remove the call:
```cpp
lv_obj_clean(lv_layer_sys());   // remove this line
```
The subsequent `lv_scr_load(scr)` already replaces the active screen, making the sys layer cleanup unnecessary.

## Step 7: `module.cpp` — Version guard cleanup (lines 640-642)

Update the deinit version guard (optional cleanup):
```cpp
// v8 guard — can simplify since we now target v9+
#if defined(LVGL_VERSION_MAJOR) && (LVGL_VERSION_MAJOR >= 8)
    lv_deinit();
#endif
```
`lv_deinit()` still exists in v9. The guard can stay as-is or be simplified to `>= 9`.

## Step 8: `setup.py` — Update LVGL source exclusions (lines 33-40)

The v8 draw backend directories have changed significantly in v9. Replace the exclude list:

**v8 excludes (remove):**
```python
EXCLUDE_SUBSTRINGS = [
    "/src/draw/arm2d/",
    "/src/draw/nxp/",
    "/src/draw/renesas/",
    "/src/draw/swm341_dma2d/",
    "/src/draw/stm32_dma2d/",
    "/src/draw/sdl/",
]
```

**v9 excludes (replace with):**
```python
EXCLUDE_SUBSTRINGS = [
    # Hardware-specific draw backends (not needed for Pi Zero SW rendering)
    "/src/draw/dma2d/",
    "/src/draw/espressif/",
    "/src/draw/eve/",
    "/src/draw/nanovg/",
    "/src/draw/nema_gfx/",
    "/src/draw/nxp/",
    "/src/draw/opengles/",
    "/src/draw/renesas/",
    "/src/draw/sdl/",
    "/src/draw/vg_lite/",
    # Architecture-specific SW blend backends (ARMv7+, RISC-V, etc.)
    "/src/draw/sw/blend/neon/",
    "/src/draw/sw/blend/helium/",
    "/src/draw/sw/blend/arm2d/",
    "/src/draw/sw/blend/riscv_v/",
    "/src/draw/sw/arm2d/",
]
```

## Step 9: `setup.py` — Add NEON assembly disable define (line 118)

Add `LV_USE_DRAW_SW_ASM=LV_DRAW_SW_ASM_NONE` to `define_macros` to prevent LVGL v9 from enabling ARM NEON assembly at compile time (not available on ARMv6):

```python
define_macros=[
    ("LV_CONF_SKIP", "1"),
    ("LV_USE_DRAW_SW_ASM", "LV_DRAW_SW_ASM_NONE"),
    ...
],
```

## Verification

1. **Compile check** (Docker QEMU build):
   ```bash
   bash run_build.sh
   ```
   This runs the full ARMv6 cross-compilation, pytest smoke tests, and ELF/ABI validation.

2. **Key things to verify:**
   - No v8 API symbols remain (grep for `lv_disp_drv`, `lv_indev_drv`, `lv_disp_draw_buf`)
   - `sizeof(lv_color_t)` is not used for buffer sizing (should be hardcoded `2` for RGB565)
   - Flush callback byte-swap logic still works correctly with `uint8_t *px_map`
   - Smoke tests pass (import, init/shutdown cycle)

3. **On-device test** (Pi Zero with ST7789):
   - `native_display_init()` → display initializes
   - `button_list_screen()` → renders correctly
   - Input (joystick/buttons) responds
   - `native_display_shutdown()` → clean black screen
