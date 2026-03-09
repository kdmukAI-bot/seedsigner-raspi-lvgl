"""seedsigner_lvgl package facade.

Stage C: prefer native CPython extension when available.
"""

from ._queue import clear_result_queue as _py_clear_result_queue
from ._queue import poll_for_result as _py_poll_for_result

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


def set_flush_callback(cb=None):
    if _native is None:
        if cb is None:
            return None
        raise NotImplementedError("Native binding not available.")
    return _native.set_flush_callback(cb)


def native_display_init(
    width=240,
    height=240,
    dc_pin=22,
    rst_pin=13,
    bl_pin=18,
    spi_path="/dev/spidev0.0",
    spi_speed_hz=40_000_000,
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
    )


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


def button_list_screen(cfg_dict):
    if _native is None:
        raise NotImplementedError(
            "Native binding not available yet. Build/install Stage C extension first."
        )
    return _native.button_list_screen(cfg_dict)


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
    "set_flush_callback",
    "native_display_init",
    "native_display_shutdown",
    "native_display_test_pattern",
    "native_debug_config",
    "set_flush_mode",
    "bind_display",
    "button_list_screen",
    "clear_result_queue",
    "poll_for_result",
]
