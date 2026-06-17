"""seedsigner_lvgl package facade.

Prefer native CPython extension when available.
"""

from pathlib import Path

from ._queue import clear_result_queue as _py_clear_result_queue
from ._queue import poll_for_result as _py_poll_for_result

# On-device language packs ship next to this package (rsynced alongside the .so).
# Layout mirrors the screens repo: <DEFAULT_FONT_DIR>/<locale>/{<locale>.ttf,
# runs.bin, ...}. Callers may override font_dir to point elsewhere (e.g. tests).
DEFAULT_FONT_DIR = Path(__file__).resolve().parent / "lang-packs"

try:
    import seedsigner_lvgl_native as _native  # type: ignore
except Exception:  # pragma: no cover
    _native = None


def lvgl_init(hor_res=240, ver_res=240):
    if _native is None:
        raise NotImplementedError("Native binding not available.")
    return _native.lvgl_init(hor_res=hor_res, ver_res=ver_res)


def lvgl_shutdown():
    if _native is None:
        return None
    return _native.lvgl_shutdown()


def set_resolution(width, height):
    """Switch LVGL display resolution (e.g. 240x240 to 320x240)."""
    if _native is None:
        raise NotImplementedError("Native binding not available.")
    return _native.set_resolution(width=width, height=height)


def set_flush_callback(cb=None):
    if _native is None:
        if cb is None:
            return None
        raise NotImplementedError("Native binding not available.")
    return _native.set_flush_callback(cb)


def native_display_init(
    width=240,
    height=240,
    dc_pin=25,
    rst_pin=27,
    bl_pin=24,
    spi_path="/dev/spidev0.0",
    spi_speed_hz=62_500_000,
    # The SeedSigner Pi Zero ST7789 panel is BGR-wired — its own driver uses BGR
    # color order (st7789_mpy color_order=BGR; the PIL path feeds "BGR;16"). The
    # native LVGL path emits RGB565, so without the MADCTL BGR bit red/blue swap
    # and the orange active highlight renders light blue. Default bgr=True so all
    # Pi 0 screens get correct color without passing it on every call.
    bgr=True,
    lvgl_swap_bytes=True,
):
    if _native is None:
        raise NotImplementedError("Native binding not available.")
    return _native.native_display_init(
        width=width,
        height=height,
        dc_pin=dc_pin,
        rst_pin=rst_pin,
        bl_pin=bl_pin,
        spi_path=spi_path,
        spi_speed_hz=spi_speed_hz,
        bgr=bgr,
        lvgl_swap_bytes=lvgl_swap_bytes,
    )


def clear_screen():
    if _native is None:
        raise NotImplementedError("Native binding not available.")
    return _native.clear_screen()


def native_input_init():
    """Initialize GPIO input only (no display). For use when display is
    owned by an external driver."""
    if _native is None:
        raise NotImplementedError("Native binding not available.")
    return _native.native_input_init()


def save_screen():
    """Save the active LVGL screen and indev group for later restore."""
    if _native is None:
        raise NotImplementedError("Native binding not available.")
    return _native.save_screen()


def restore_screen():
    """Restore previously saved LVGL screen and indev group."""
    if _native is None:
        raise NotImplementedError("Native binding not available.")
    return _native.restore_screen()


def native_display_shutdown():
    if _native is None:
        return None
    return _native.native_display_shutdown()


def native_display_test_pattern():
    if _native is None:
        raise NotImplementedError("Native binding not available.")
    return _native.native_display_test_pattern()


def native_debug_config(enabled=True, flush_log_limit=20):
    if _native is None:
        raise NotImplementedError("Native binding not available.")
    return _native.native_debug_config(enabled=enabled, flush_log_limit=flush_log_limit)


def set_flush_mode(mode):
    if _native is None:
        raise NotImplementedError("Native binding not available.")
    return _native.set_flush_mode(mode)


def bind_display(display):
    """Bind an initialized display backend that provides blit_rgb565()."""

    def _flush(x1, y1, x2, y2, buf):
        display.blit_rgb565(x1, y1, x2, y2, buf)

    return set_flush_callback(_flush)


def lvgl_pump(duration_ms=10, sleep_ms=1):
    if _native is None:
        raise NotImplementedError("Native binding not available.")
    return _native.lvgl_pump(duration_ms=duration_ms, sleep_ms=sleep_ms)


def set_locale(locale, font_dir=None):
    """Switch the active locale, loading its font packs from <font_dir>/<locale>/.

    font_dir defaults to the language packs shipped beside this package
    (DEFAULT_FONT_DIR). Returns True on success; returns False if a required
    pack file is missing, in which case the loader has restored the baked
    Western (English) floor. Must be called after lvgl_init() — font
    registration rasterizes glyphs via tiny_ttf and needs a live LVGL runtime.
    """
    if _native is None:
        raise NotImplementedError("Native binding not available.")
    if font_dir is None:
        font_dir = DEFAULT_FONT_DIR
    return _native.set_locale(str(locale), str(font_dir))


def unload_locale():
    """Clear loaded locale packs and restore the baked Western floor."""
    if _native is None:
        return None
    return _native.unload_locale()


def button_list_screen(cfg_dict):
    if _native is None:
        raise NotImplementedError("Native binding not available.")
    return _native.button_list_screen(cfg_dict)


def seed_add_passphrase_screen(cfg_dict=None):
    """Render the BIP39 passphrase entry screen.

    cfg_dict (optional) accepts: top_nav (title/show_back_button), initial_text,
    max_length, and an input-mode override. Poll for results afterward: a
    ("text_entered", -1, passphrase) tuple on confirm, or ("topnav_back", -1, ...)
    if the user backs out.
    """
    if _native is None:
        raise NotImplementedError("Native binding not available.")
    if cfg_dict is None:
        return _native.seed_add_passphrase_screen()
    return _native.seed_add_passphrase_screen(cfg_dict)


def main_menu_screen(wait_timeout_ms=0, allow_timeout_fallback=False):
    if _native is None:
        raise NotImplementedError("Native binding not available.")
    return _native.main_menu_screen(
        wait_timeout_ms=wait_timeout_ms,
        allow_timeout_fallback=allow_timeout_fallback,
    )


def screensaver_screen(wait_timeout_ms=0, allow_timeout_fallback=False):
    if _native is None:
        raise NotImplementedError("Native binding not available.")
    return _native.screensaver_screen(
        wait_timeout_ms=wait_timeout_ms,
        allow_timeout_fallback=allow_timeout_fallback,
    )


def clear_result_queue():
    if _native is not None:
        return _native.clear_result_queue()
    return _py_clear_result_queue()


def poll_for_result():
    if _native is not None:
        return _native.poll_for_result()
    return _py_poll_for_result()


__all__ = [
    "lvgl_init",
    "lvgl_shutdown",
    "set_resolution",
    "set_flush_callback",
    "clear_screen",
    "native_display_init",
    "native_input_init",
    "save_screen",
    "restore_screen",
    "native_display_shutdown",
    "native_display_test_pattern",
    "native_debug_config",
    "set_flush_mode",
    "bind_display",
    "lvgl_pump",
    "set_locale",
    "unload_locale",
    "button_list_screen",
    "seed_add_passphrase_screen",
    "main_menu_screen",
    "screensaver_screen",
    "clear_result_queue",
    "poll_for_result",
]
