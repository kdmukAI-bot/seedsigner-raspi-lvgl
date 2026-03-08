import importlib
import pytest


def test_native_module_import_and_queue_shape():
    try:
        mod = importlib.import_module("seedsigner_lvgl_native")
    except ModuleNotFoundError:
        pytest.skip("seedsigner_lvgl_native not built/installed in this test environment")

    mod.clear_result_queue()
    assert mod.poll_for_result() is None

    # MicroPython-style contract shape: dict with top_nav + button_list.
    cfg = {
        "top_nav": {"title": "Menu"},
        "button_list": [{"label": "Compiled Path"}],
    }
    mod.button_list_screen(cfg)

    # Stage D proof: call routed through compiled bridge path.
    assert mod._debug_last_path() == "compiled"

    ev = mod.poll_for_result()
    assert ev == ("button_selected", 0, "Compiled Path")
    assert isinstance(ev, tuple) and len(ev) == 3
    assert mod.poll_for_result() is None
