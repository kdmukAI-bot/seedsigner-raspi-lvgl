import importlib
import pytest


def _native_or_skip():
    try:
        return importlib.import_module("seedsigner_lvgl_native")
    except ModuleNotFoundError:
        pytest.skip("seedsigner_lvgl_native not built/installed in this test environment")


def test_native_module_import_and_queue_shape():
    mod = _native_or_skip()

    mod.clear_result_queue()
    assert mod.poll_for_result() is None

    cfg = {
        "top_nav": {"title": "Menu"},
        "button_list": ["Compiled Path"],
    }
    mod.button_list_screen(cfg)

    # Stage E proof: compiled path attempted; fallback is allowed only on timeout.
    assert mod._debug_last_path() in ("compiled", "fallback_timeout")

    ev = mod.poll_for_result()
    assert ev is not None
    assert isinstance(ev, tuple) and len(ev) == 3
    assert ev[0] in ("button_selected", "topnav_back", "topnav_power")


def test_callback_driven_topnav_and_ordering():
    mod = _native_or_skip()
    mod.clear_result_queue()

    mod._debug_emit_result("topnav_back", 0xFFFFFFFF)
    mod._debug_emit_result("topnav_power", 0xFFFFFFFF)
    mod._debug_emit_result("First", 1)

    assert mod.poll_for_result() == ("topnav_back", -1, "topnav_back")
    assert mod.poll_for_result() == ("topnav_power", -1, "topnav_power")
    assert mod.poll_for_result() == ("button_selected", 1, "First")
    assert mod.poll_for_result() is None


def test_validation_errors():
    mod = _native_or_skip()

    with pytest.raises(RuntimeError):
        mod.button_list_screen({"top_nav": {}, "button_list": ["A"]})

    with pytest.raises(RuntimeError):
        mod.button_list_screen({"top_nav": {"title": "X"}, "button_list": [{"label": "A"}]})
