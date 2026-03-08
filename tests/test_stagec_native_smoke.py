import importlib
import pytest


def test_native_module_import_and_queue_shape():
    try:
        mod = importlib.import_module("seedsigner_lvgl_native")
    except ModuleNotFoundError:
        pytest.skip("seedsigner_lvgl_native not built/installed in this test environment")

    mod.clear_result_queue()
    assert mod.poll_for_result() is None

    mod.button_list_screen({"top_nav": {}, "button_list": ["A"]})
    ev = mod.poll_for_result()
    assert ev == ("button_selected", 0, "A")
    assert mod.poll_for_result() is None
