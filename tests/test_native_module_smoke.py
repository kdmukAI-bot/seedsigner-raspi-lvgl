import importlib


def test_native_module_import_and_queue_semantics():
    mod = importlib.import_module("seedsigner_lvgl_native")

    mod.clear_result_queue()
    assert mod.poll_for_result() is None

    mod.button_list_screen({"top_nav": {}, "button_list": [{"label": "First"}]})
    event = mod.poll_for_result()
    assert event == ("button_selected", 0, "First")
    assert mod.poll_for_result() is None
