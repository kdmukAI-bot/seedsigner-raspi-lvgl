import importlib
import pytest


def test_native_module_import_and_queue_semantics():
    try:
        mod = importlib.import_module("seedsigner_lvgl_native")
    except ModuleNotFoundError:
        pytest.skip("seedsigner_lvgl_native not built/installed in this test environment")

    mod.lvgl_init(hor_res=240, ver_res=240)
    mod.clear_result_queue()
    assert mod.poll_for_result() is None

    mod.button_list_screen({
        "top_nav": {"title": "Menu"},
        "button_list": ["First"],
        "wait_timeout_ms": 250,
        "allow_timeout_fallback": True,
    })
    event = mod.poll_for_result()
    assert event is not None
    assert event[0] in ("button_selected", "topnav_back", "topnav_power")
    assert mod.poll_for_result() is None
    mod.lvgl_shutdown()


def test_native_module_lifecycle_api():
    try:
        mod = importlib.import_module("seedsigner_lvgl_native")
    except ModuleNotFoundError:
        pytest.skip("seedsigner_lvgl_native not built/installed in this test environment")

    mod.lvgl_init(hor_res=240, ver_res=240)
    mod.lvgl_shutdown()


def test_button_list_requires_explicit_init():
    try:
        mod = importlib.import_module("seedsigner_lvgl_native")
    except ModuleNotFoundError:
        pytest.skip("seedsigner_lvgl_native not built/installed in this test environment")

    mod.lvgl_shutdown()
    with pytest.raises(RuntimeError):
        mod.button_list_screen({
            "top_nav": {"title": "Menu"},
            "button_list": ["First"],
            "wait_timeout_ms": 250,
            "allow_timeout_fallback": True,
        })
