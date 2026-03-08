from __future__ import annotations

from typing import Any, Dict, Iterable, List, Tuple
import time

from ._queue import clear_result_queue, poll_for_result, push_result


def _sleep(seconds: float) -> None:
    time.sleep(seconds)


def _make_input_backend():
    from .platform.pi_zero import PiButtonsBackend

    backend = PiButtonsBackend()
    backend.init_gpio()
    return backend


def _extract_label(item: Any) -> str:
    if isinstance(item, dict):
        return str(item.get("label", ""))
    if isinstance(item, tuple) and len(item) >= 1:
        return str(item[0])
    return str(item)


def _normalize_button_list(button_list: List[Any]) -> List[Tuple[str, Any]]:
    normalized: List[Tuple[str, Any]] = []
    for i, item in enumerate(button_list):
        if isinstance(item, tuple):
            if len(item) == 0:
                raise ValueError(f"button_list tuple at index {i} must not be empty")
            label = str(item[0])
            value = item[1] if len(item) > 1 else item[0]
            normalized.append((label, value))
        elif isinstance(item, dict):
            if "label" not in item:
                raise ValueError(f"button_list dict at index {i} must include 'label'")
            label = str(item["label"])
            value = item.get("value", label)
            normalized.append((label, value))
        else:
            label = str(item)
            normalized.append((label, item))
    return normalized


def button_list_screen(cfg_dict: Dict[str, Any]) -> None:
    if not isinstance(cfg_dict, dict):
        raise TypeError("cfg_dict must be a dict")
    if "top_nav" not in cfg_dict or "button_list" not in cfg_dict:
        raise ValueError("cfg_dict must include 'top_nav' and 'button_list'")
    if not isinstance(cfg_dict["top_nav"], dict):
        raise TypeError("top_nav must be a dict")
    if not isinstance(cfg_dict["button_list"], list):
        raise TypeError("button_list must be a list")

    top_nav = cfg_dict["top_nav"]
    show_back_button = bool(top_nav.get("show_back_button", False))

    buttons = _normalize_button_list(cfg_dict["button_list"])
    if not buttons:
        return

    selected_index = 0
    backend = _make_input_backend()

    while True:
        ev = backend.poll_event()
        if ev is None:
            _sleep(0.01)
            continue

        key, event_type, _ts = ev
        if event_type not in ("press", "repeat"):
            continue

        if key == "UP":
            selected_index = max(0, selected_index - 1)
        elif key == "DOWN":
            selected_index = min(len(buttons) - 1, selected_index + 1)
        elif key == "PRESS" and event_type == "press":
            label, _value = buttons[selected_index]
            push_result(("button_selected", selected_index, label))
            return
        elif key == "KEY1" and event_type == "press" and show_back_button:
            push_result(("back", -1, "back"))
            return
        elif key in ("LEFT", "RIGHT"):
            # no-op for current content-only list behavior
            pass


__all__ = [
    "button_list_screen",
    "clear_result_queue",
    "poll_for_result",
]
