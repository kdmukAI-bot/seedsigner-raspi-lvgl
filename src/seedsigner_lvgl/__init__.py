"""seedsigner_lvgl package facade.

Stage C: prefer native CPython extension when available.
"""

from ._queue import clear_result_queue as _py_clear_result_queue
from ._queue import poll_for_result as _py_poll_for_result

try:
    import seedsigner_lvgl_native as _native  # type: ignore
except Exception:  # pragma: no cover
    _native = None


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


__all__ = ["button_list_screen", "clear_result_queue", "poll_for_result"]
