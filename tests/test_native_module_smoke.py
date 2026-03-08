import importlib
import pytest


def test_native_module_import_and_queue_semantics():
    try:
        mod = importlib.import_module("seedsigner_lvgl_native")
    except ModuleNotFoundError:
        pytest.skip("seedsigner_lvgl_native not built/installed in this test environment")

    mod.clear_result_queue()
    assert mod.poll_for_result() is None

    mod.button_list_screen({"top_nav": {"title": "Menu"}, "button_list": ["First"]})
    event = mod.poll_for_result()
    assert event is not None
    assert event[0] in ("button_selected", "topnav_back", "topnav_power")
    assert mod.poll_for_result() is None
