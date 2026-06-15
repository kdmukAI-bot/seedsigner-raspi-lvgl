# Integration Strategy: seedsigner-raspi-lvgl into SeedSigner

## Context

seedsigner-raspi-lvgl produces a compiled C extension (`seedsigner_lvgl_native.so`)
that renders LVGL-based screens on Raspberry Pi Zero hardware. The SeedSigner Python
application currently uses pure PIL for all screen rendering. We need a strategy for
how the compiled module gets installed into and used by the SeedSigner Python codebase,
enabling incremental migration of screens from PIL to LVGL.

The fork of the SeedSigner Python app lives at `/home/keith/dev/seedsigner`.

## Key Integration Points

**Dispatch point** — `View.run_screen()` at `src/seedsigner/views/view.py:112-118`:
```python
def run_screen(self, Screen_cls, **kwargs) -> int | str:
    self.screen = Screen_cls(**kwargs)
    return self.screen.display()
```
All Views call this. It returns an int (button index) or `RET_CODE__BACK_BUTTON` (1000) /
`RET_CODE__POWER_BUTTON` (1001). LVGL adapter screens must honor this same contract.

**Display hardware** — Both use ST7789 on the same SPI/GPIO pins (DC=BCM25, RST=BCM27,
BL=BCM24), but via different kernel interfaces (RPi.GPIO+spidev vs gpiochip+fd ioctl).
They cannot share the hardware simultaneously.

---

## Part 1: Packaging & Distribution

**Approach: Pre-built wheel, installed via pip**

1. Extend CI in seedsigner-raspi-lvgl to produce a `.whl` file (not just the bare `.so`)
   - The existing `setup.py` + `pyproject.toml` already define the extension; `pip wheel
     --no-deps .` inside the ARM Docker build will produce a platform wheel containing both
     `seedsigner_lvgl/` (Python facade) and the native `.so`
   - Publish as a GitHub Release asset on the seedsigner-raspi-lvgl repo

2. SeedSigner installs via pip URL or local path:
   ```
   pip install seedsigner_raspi_lvgl-0.1.0-cp310-cp310-linux_armv6l.whl
   ```
   Add to an optional `requirements-lvgl.txt` in the SeedSigner repo.

3. The `seedsigner_lvgl` Python package is then importable from SeedSigner code like any
   other dependency.

**SeedSigner OS integration:** We control the OS build pipeline. The wheel can be added as
a build step: download the release wheel, pip install it into the OS image alongside other
dependencies. Same mechanism as existing requirements.txt packages, just from a GitHub
Release URL instead of PyPI.

**Why not alternatives:**
- Submodule: requires building from source on Pi Zero (impractical)
- Vendored .so: 6.9MB binary in git, poor version tracking
- PyPI: overkill for R&D between two coordinated repos

### Local dev workflow

The Pi Zero is too slow for compiling. Developers build on a development machine
(Docker + QEMU emulation) and then deploy the results to Pi Zero hardware.

**On the dev machine:**
The build script (`run_build.sh` → `build_steps.sh`) runs `setup.py build_ext --inplace`,
placing the compiled `.so` directly into the source tree under `src/`.

**On the Pi Zero:**
rsync the repo (or just `src/seedsigner_lvgl/`) from the dev machine to the Pi, then
use an editable install so Python resolves `import seedsigner_lvgl` from the synced tree:

```bash
# One-time setup on the Pi
pip install -e /path/to/seedsigner-raspi-lvgl
```

Iteration loop:
1. Change C++ or Python facade on dev machine
2. `./run_build.sh` (Docker, on dev machine)
3. `rsync -a src/seedsigner_lvgl/ pi-zero:/path/to/seedsigner-raspi-lvgl/src/seedsigner_lvgl/`
4. Restart SeedSigner on the Pi

### Changes needed (seedsigner-raspi-lvgl repo)
- `.github/workflows/build.yml`: add wheel-building step after .so compilation
- Verify `pyproject.toml` metadata is complete for wheel generation

---

## Part 2: Python Adapter Layer

**Approach: Adapter screens that implement BaseScreen's `display() -> int` contract**

Create LVGL-backed Screen classes that Views can use as drop-in replacements for PIL
screens. Each adapter:
- Translates SeedSigner kwargs (ButtonOption lists, titles, etc.) into LVGL `cfg_dict`
- Calls the LVGL screen function (which blocks while handling input internally)
- Translates the LVGL result tuple back into SeedSigner return codes

Example for ButtonListScreen:
```python
class LvglButtonListScreen:
    def __init__(self, title, button_data, show_back_button=True, ...):
        self.cfg = {
            "top_nav": {"title": title, "show_back": show_back_button},
            "button_list": [opt.button_label for opt in button_data],
        }

    def display(self) -> int:
        # Uses Python flush + input bridges (see Part 3 for details)
        result = lvgl_bridge.run_screen(
            seedsigner_lvgl.button_list_screen, self.cfg
        )
        return self._translate_result(result)

    def _translate_result(self, result) -> int:
        if result is None:
            return 0
        kind, index, label = result
        if kind == "topnav_back":
            return RET_CODE__BACK_BUTTON
        elif kind == "topnav_power":
            return RET_CODE__POWER_BUTTON
        return index
```

**Screen substitution** happens at `View.run_screen()`:
```python
def run_screen(self, Screen_cls, **kwargs) -> int | str:
    if _lvgl_enabled and Screen_cls in _LVGL_SCREEN_MAP:
        Screen_cls = _LVGL_SCREEN_MAP[Screen_cls]
    self.screen = Screen_cls(**kwargs)
    return self.screen.display()
```

Views are completely unaware of LVGL — the substitution is transparent.

### Changes needed (seedsigner repo)
- New: `src/seedsigner/gui/screens/lvgl_screens.py` — adapter classes
- Modified: `src/seedsigner/views/view.py` — `run_screen()` dispatch logic
- Modified: `src/seedsigner/controller.py` — LVGL init/shutdown at app lifecycle

---

## Part 3: Display & Input Coordination

**Approach: SeedSigner owns all hardware; LVGL renders through Python callbacks**

SeedSigner keeps exclusive ownership of SPI (display) and RPi.GPIO (buttons) at all times.
No hardware handoff, no resource contention. LVGL communicates through two Python bridges:

### Display bridge (already exists)
LVGL's `set_flush_mode("python")` + `set_flush_callback(cb)` routes rendered pixels
through a Python callback. The callback writes RGB565 data through SeedSigner's existing
ST7789 SPI driver. Performance impact is minimal — LVGL only flushes on UI changes (not
continuous 60fps), so the per-frame Python callback overhead is negligible for menu screens.

### Input bridge (needs new C API in seedsigner-raspi-lvgl)
Currently, LVGL input is ALWAYS native GPIO via gpiochip — no Python injection API exists.
However, the screen_runner desktop tool already proves the pattern works at the C level:
it uses global state (`g_pending_key`, `g_key_ready`) set by SDL events and read by LVGL's
input callback.

**Required new API in seedsigner-raspi-lvgl (~50-80 lines of C):**

1. Add `INPUT_MODE_PYTHON` to `input_profile.h` (alongside existing TOUCH/HARDWARE)
2. Add global key injection state in `module.cpp`:
   ```cpp
   static uint32_t g_py_key = 0;
   static bool g_py_key_pressed = false;
   ```
3. Modify `native_input_read_cb()` to check input mode — if PYTHON, read from globals
   instead of GPIO ioctl
4. Expose two new Python functions:
   - `set_input_mode("native"|"python")` — switch between GPIO and injected input
   - `inject_key(key_code)` — set the pending key for LVGL to read on next pump

**Adapter input loop (SeedSigner side):**
```python
def display(self) -> int:
    seedsigner_lvgl.set_flush_mode("python")
    seedsigner_lvgl.set_flush_callback(self._flush_to_st7789)
    seedsigner_lvgl.set_input_mode("python")
    seedsigner_lvgl.button_list_screen(self.cfg)  # non-blocking with short timeout

    while True:
        seedsigner_lvgl.lvgl_pump(duration_ms=10)
        # Poll SeedSigner's HardwareButtons
        key = self.hw_inputs.check_for_low()
        if key:
            seedsigner_lvgl.inject_key(self._map_key(key))
        result = seedsigner_lvgl.poll_for_result()
        if result:
            return self._translate_result(result)
```

**Benefits of this approach:**
- No hardware handoff — zero latency switching between PIL and LVGL screens
- SeedSigner's existing debounce/repeat logic stays in control
- No GPIO conflict between RPi.GPIO and gpiochip (gpiochip never opens button pins)
- `native_display_init()` is never called — no gpiochip init at all
- Clean separation: LVGL is a pure rendering/UI engine, SeedSigner owns all I/O

### Changes needed (seedsigner-raspi-lvgl repo)
- `sources/seedsigner-lvgl-screens/components/seedsigner/input_profile.h`: add INPUT_MODE_PYTHON
- `native/python_bindings/module.cpp`: add inject_key() + set_input_mode() functions,
  modify native_input_read_cb() to support python mode
- `src/seedsigner_lvgl/__init__.py`: expose new functions in facade

### Changes needed (seedsigner repo)
- New: `src/seedsigner/hardware/lvgl_bridge.py` — flush callback that routes through
  ST7789 driver, key mapping from HardwareButtons codes to LVGL key codes
- The ST7789 driver does NOT need a cleanup() method (SeedSigner keeps ownership always)

---

## Part 4: Feature Gating

Gate LVGL behind a runtime setting so it can be toggled without redeployment:
- New setting: `RENDERING_ENGINE` with values `"pil"` (default) / `"lvgl"`
- LVGL init only happens when setting is `"lvgl"` and the native module is importable
- Graceful fallback: if LVGL screen adapter fails, fall back to PIL screen

### Changes needed (seedsigner repo)
- Modified: `src/seedsigner/models/settings_definition.py` — add rendering engine setting

---

## Part 5: Migration Roadmap (phased across production releases)

**Phase 1: Foundation** — wheel packaging, adapter base class, hardware bridge,
feature flag, renderer ownership tracking. No user-visible change; LVGL disabled by default.

**Phase 2: First screens** — ButtonListScreen adapter (most common screen type).
MainMenuScreen + ScreensaverScreen adapters (already have LVGL implementations).
Ship as opt-in (`RENDERING_ENGINE=lvgl`) behind the feature flag.

**Phase 3: Expand coverage** — As new LVGL screen types are added to seedsigner-lvgl-screens,
add corresponding adapters. Each release migrates a batch of screens. The dispatch map
grows; PIL screens that haven't been migrated yet continue to work unchanged.

**Phase 4: LVGL default** — Once coverage is high enough, flip the default to `lvgl` with
PIL as the fallback. Users can still force `pil` mode if needed.

**Phase 5: PIL deprecation** — If/when all screens have LVGL equivalents (or the remaining
PIL holdouts are acceptable), remove the PIL rendering path. If a small handful of screens
must remain PIL-based permanently, the handoff bridge stays but is exercised rarely.

---

## Part 6: Screenshot Generator Compatibility

SeedSigner's test suite includes a screenshot generator (`tests/screenshot_generator/`) that
produces reference PNGs for every screen. This is critical for visual regression testing.
LVGL screens use a fundamentally different rendering pipeline, so the existing generator
cannot capture them without modification.

### How the current generator works

The generator replaces the `Renderer` singleton with a `ScreenshotRenderer` that intercepts
`show_image()`:

```
Components → PIL ImageDraw → PIL Image canvas → renderer.show_image() → save PNG
```

When a View calls `run_screen()`, the screen's `_render()` method draws onto a shared PIL
Image canvas, then calls `renderer.show_image()`. The mock renderer saves that PIL Image
to disk and raises `ScreenshotComplete` to exit the flow.

### Why it doesn't work for LVGL screens

LVGL screens never touch the Python `Renderer`. They render internally through LVGL's
own compositor and emit raw RGB565 pixel regions via `flush_cb()`. There is no PIL Image
and no `show_image()` call to intercept.

```
C widgets → LVGL renderer → flush_cb(x1, y1, x2, y2, rgb565_bytes) → display or callback
```

### Available mechanisms for LVGL screenshots

Two capture mechanisms already exist:

1. **C++ screenshot tool** (`sources/seedsigner-lvgl-screens/tools/screenshot_generator/`) —
   hooks the LVGL flush callback to accumulate pixel regions into a framebuffer, converts
   to RGB24, and writes PNG via libpng. Works today for standalone C screen testing.

2. **Python flush callback** (`set_flush_callback(callable)`) — the native module routes
   every dirty region through a Python callable: `callback(x1, y1, x2, y2, bytes_buf)`.
   This is the same mechanism used for the display bridge (Part 3).

### Recommended approach: flush callback → PIL Image capture utility

Add a screenshot capture utility to the `seedsigner_lvgl` Python package that:

1. Registers a flush callback that accumulates RGB565 regions into a PIL Image
2. Runs the LVGL screen with a short timeout or simulated input
3. Returns the final PIL Image (same type the existing generator already saves)

```python
# seedsigner_lvgl/screenshot.py
from PIL import Image
import struct
import seedsigner_lvgl

def capture_screen(screen_fn, cfg, width=240, height=240):
    """Run an LVGL screen and return a PIL Image of the rendered result."""
    fb = Image.new("RGB", (width, height), "black")
    pixels = fb.load()

    def _flush(x1, y1, x2, y2, buf):
        i = 0
        for y in range(y1, y2 + 1):
            for x in range(x1, x2 + 1):
                rgb565 = struct.unpack_from('<H', buf, i)[0]
                r = (rgb565 >> 11) << 3
                g = ((rgb565 >> 5) & 0x3F) << 2
                b = (rgb565 & 0x1F) << 3
                pixels[x, y] = (r, g, b)
                i += 2

    seedsigner_lvgl.set_flush_callback(_flush)
    screen_fn(cfg)  # create screen objects
    seedsigner_lvgl.lvgl_pump(duration_ms=50)  # render initial frame
    return fb
```

### Hybrid period: two capture paths

During migration, the screenshot generator must handle both screen types:

- **PIL screens** — existing `ScreenshotRenderer` mock (no change needed)
- **LVGL screens** — flush callback capture via the utility above

Both paths produce PIL Images, so downstream comparison and storage infrastructure is
unchanged. The `ScreenshotConfig` dataclass (or a new LVGL equivalent) needs to know
which capture path to use for each screen. This can follow the same dispatch pattern
as `_LVGL_SCREEN_MAP` in the adapter layer (Part 2):

```python
if Screen_cls in _LVGL_SCREEN_MAP:
    image = seedsigner_lvgl.screenshot.capture_screen(...)
else:
    image = existing_pil_screenshot_flow(...)
```

### Color fidelity note

PIL screens render in 24-bit RGB. LVGL screens render in RGB565 (16-bit), so captured
screenshots will have reduced color depth (5-6-5 bits per channel). For visual regression
comparison, this is acceptable — the actual hardware display is RGB565 anyway, so LVGL
screenshots are more representative of what users see. However, pixel-exact comparison
between a PIL reference screenshot and an LVGL screenshot of the "same" screen will not
match due to both color depth differences and rendering differences (font rasterization,
anti-aliasing, layout).

New LVGL screens should have their own reference screenshots, not be compared against
the PIL originals.

### Changes needed

**seedsigner-raspi-lvgl repo:**
- New: `src/seedsigner_lvgl/screenshot.py` — flush callback capture utility

**seedsigner repo:**
- Modified: `tests/screenshot_generator/generator.py` — add LVGL capture path
- Modified: `tests/screenshot_generator/utils.py` — extend `ScreenshotConfig` for LVGL screens
- New reference screenshots for migrated LVGL screens (separate from PIL references)

---

## Resolved Questions

**Migration direction:** Full LVGL replacement, phased across multiple production releases.
Architecture must tolerate a permanent PIL fallback for a small handful of screens.

**Display mode:** Python flush callback — SeedSigner owns all hardware, LVGL renders
through callbacks. No hardware handoff needed.

**Deploy target:** Build with SeedSigner OS integration in mind from the start. We control
the OS build pipeline.

**Input model:** With the Python input bridge, SeedSigner's HardwareButtons stays in
control of debounce/repeat — no behavioral divergence concern.

---

## Verification

**seedsigner-raspi-lvgl (input bridge):**
- Extend existing smoke tests to verify inject_key() + set_input_mode("python") work
- Test that button_list_screen responds to injected keys (select, back, scroll)
- Build wheel in CI, verify it installs cleanly:
  `pip install *.whl && python -c "import seedsigner_lvgl"`

**SeedSigner integration (on Pi Zero hardware):**
- Run SeedSigner with `RENDERING_ENGINE=lvgl`, navigate ButtonListScreen menus
- Verify PIL screens still display correctly (no hardware state corruption)
- Toggle back to `RENDERING_ENGINE=pil`, confirm PIL-only mode still works
- Run existing SeedSigner test suite with LVGL disabled (no regressions)
