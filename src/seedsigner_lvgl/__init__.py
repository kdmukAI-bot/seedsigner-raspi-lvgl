"""seedsigner_lvgl package (Pi project).

Direction update:
- Screen behavior should come from compiled C/C++ LVGL core via Python bindings.
- Python reimplementation stubs were intentionally removed to avoid API confusion.

Current Python-only exports are limited to queue helpers used by interim tests/tools.
"""

from ._queue import clear_result_queue, poll_for_result


def button_list_screen(_cfg_dict):
    raise NotImplementedError(
        "button_list_screen is now expected from compiled bindings; "
        "Python shim implementation has been retired."
    )


__all__ = [
    "button_list_screen",
    "clear_result_queue",
    "poll_for_result",
]
