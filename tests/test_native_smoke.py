import importlib
import pytest


def _native_or_skip():
    try:
        return importlib.import_module("seedsigner_lvgl_screens")
    except ModuleNotFoundError:
        pytest.skip("seedsigner_lvgl_screens not built/installed in this test environment")


def test_native_module_import_and_queue_shape():
    mod = _native_or_skip()

    mod.lvgl_init(hor_res=240, ver_res=240)
    mod.clear_result_queue()
    assert mod.poll_for_result() is None

    cfg = {
        "top_nav": {"title": "Menu"},
        "button_list": ["Compiled Path"],
        "wait_timeout_ms": 250,
        "allow_timeout_fallback": True,
    }
    mod.button_list_screen(cfg)

    # Stage E proof: compiled path attempted; fallback is allowed only on timeout.
    assert mod._debug_last_path() in ("compiled", "fallback_timeout")

    ev = mod.poll_for_result()
    assert ev is not None
    assert isinstance(ev, tuple) and len(ev) == 3
    assert ev[0] in ("button_selected", "topnav_back", "topnav_power")
    mod.lvgl_shutdown()


def test_passphrase_screen_renders():
    mod = _native_or_skip()

    mod.lvgl_init(hor_res=240, ver_res=240)
    mod.clear_result_queue()

    # No timeout fallback exists for this screen; a short wait_timeout_ms lets the
    # render path run and return without a result rather than blocking forever.
    mod.seed_add_passphrase_screen({
        "top_nav": {"title": "Enter Passphrase"},
        "initial_text": "satoshi",
        "wait_timeout_ms": 250,
    })
    assert mod._debug_last_path() == "compiled"
    mod.lvgl_shutdown()


def test_callback_driven_topnav_and_ordering():
    mod = _native_or_skip()
    mod.clear_result_queue()

    # Reserved result codes arrive in the index slot (SEEDSIGNER_RET_* in
    # seedsigner.h): 1000 = back, 1001 = power, 1100 = screensaver dismiss.
    # Body buttons emit their 0-based index. label is informational only.
    mod._debug_emit_result("back", 1000)
    mod._debug_emit_result("power", 1001)
    mod._debug_emit_result("screensaver_dismiss", 1100)
    mod._debug_emit_result("First", 1)

    assert mod.poll_for_result() == ("topnav_back", -1, "back")
    assert mod.poll_for_result() == ("topnav_power", -1, "power")
    assert mod.poll_for_result() == ("button_selected", -1, "screensaver_dismiss")
    assert mod.poll_for_result() == ("button_selected", 1, "First")
    assert mod.poll_for_result() is None


def test_validation_errors():
    mod = _native_or_skip()
    mod.lvgl_init(hor_res=240, ver_res=240)

    with pytest.raises(RuntimeError):
        mod.button_list_screen({"top_nav": {}, "button_list": ["A"]})

    with pytest.raises(RuntimeError):
        mod.button_list_screen({"top_nav": {"title": "X"}, "button_list": [{"label": "A"}]})

    mod.lvgl_shutdown()
