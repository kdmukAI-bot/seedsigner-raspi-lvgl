from typing import Any, Dict

from ._queue import clear_result_queue, poll_for_result, push_result


def _extract_label(item: Any) -> str:
    if isinstance(item, dict):
        label = item.get("label", "")
        return str(label)
    return str(item)


def button_list_screen(cfg_dict: Dict[str, Any]) -> None:
    if not isinstance(cfg_dict, dict):
        raise TypeError("cfg_dict must be a dict")
    if "top_nav" not in cfg_dict or "button_list" not in cfg_dict:
        raise ValueError("cfg_dict must include 'top_nav' and 'button_list'")
    if not isinstance(cfg_dict["top_nav"], dict):
        raise TypeError("top_nav must be a dict")
    if not isinstance(cfg_dict["button_list"], list):
        raise TypeError("button_list must be a list")

    buttons = cfg_dict["button_list"]
    if buttons:
        first_label = _extract_label(buttons[0])
        push_result(("button_selected", 0, first_label))


__all__ = [
    "button_list_screen",
    "clear_result_queue",
    "poll_for_result",
]
