from seedsigner_lvgl import button_list_screen, clear_result_queue, poll_for_result


def test_api_smoke_and_queue_semantics():
    clear_result_queue()
    assert poll_for_result() is None

    cfg = {
        "top_nav": {"title": "Menu"},
        "button_list": [{"label": "First Option"}, {"label": "Second Option"}],
    }

    button_list_screen(cfg)
    event = poll_for_result()

    assert isinstance(event, tuple)
    assert event == ("button_selected", 0, "First Option")
    assert poll_for_result() is None


def test_empty_button_list_produces_no_event():
    clear_result_queue()
    button_list_screen({"top_nav": {}, "button_list": []})
    assert poll_for_result() is None
