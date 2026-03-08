import seedsigner_lvgl as api


class FakeBackend:
    def __init__(self, events):
        self._events = list(events)

    def poll_event(self):
        if self._events:
            return self._events.pop(0)
        return None


def _patch_backend(monkeypatch, events):
    fake = FakeBackend(events)
    monkeypatch.setattr(api, "_make_input_backend", lambda: fake)
    monkeypatch.setattr(api, "_sleep", lambda _s: None)


def test_api_smoke_and_queue_semantics(monkeypatch):
    api.clear_result_queue()
    assert api.poll_for_result() is None

    _patch_backend(monkeypatch, [("PRESS", "press", 1)])
    cfg = {
        "top_nav": {"title": "Menu", "show_back_button": False, "show_power_button": False},
        "button_list": [{"label": "First Option"}, {"label": "Second Option"}],
    }

    api.button_list_screen(cfg)
    event = api.poll_for_result()

    assert isinstance(event, tuple)
    assert event == ("button_selected", 0, "First Option")
    assert api.poll_for_result() is None


def test_empty_button_list_produces_no_event():
    api.clear_result_queue()
    api.button_list_screen({"top_nav": {}, "button_list": []})
    assert api.poll_for_result() is None


def test_navigation_and_select(monkeypatch):
    api.clear_result_queue()
    # down, down, up, select -> index 1
    _patch_backend(
        monkeypatch,
        [
            ("DOWN", "press", 1),
            ("DOWN", "press", 2),
            ("UP", "press", 3),
            ("PRESS", "press", 4),
        ],
    )

    api.button_list_screen(
        {
            "top_nav": {"show_back_button": False},
            "button_list": ["A", "B", "C"],
        }
    )

    assert api.poll_for_result() == ("button_selected", 1, "B")


def test_bounds_clamped(monkeypatch):
    api.clear_result_queue()
    _patch_backend(
        monkeypatch,
        [
            ("UP", "press", 1),
            ("UP", "repeat", 2),
            ("DOWN", "press", 3),
            ("DOWN", "repeat", 4),
            ("DOWN", "repeat", 5),
            ("PRESS", "press", 6),
        ],
    )
    api.button_list_screen(
        {
            "top_nav": {"show_back_button": False},
            "button_list": ["A", "B", "C"],
        }
    )
    assert api.poll_for_result() == ("button_selected", 2, "C")


def test_back_action_when_enabled(monkeypatch):
    api.clear_result_queue()
    _patch_backend(monkeypatch, [("KEY1", "press", 1)])
    api.button_list_screen(
        {
            "top_nav": {"show_back_button": True},
            "button_list": ["A", "B"],
        }
    )
    assert api.poll_for_result() == ("back", -1, "back")


def test_poll_queue_ordering_deterministic(monkeypatch):
    api.clear_result_queue()

    _patch_backend(monkeypatch, [("PRESS", "press", 1)])
    api.button_list_screen({"top_nav": {}, "button_list": ["A"]})

    _patch_backend(monkeypatch, [("PRESS", "press", 2)])
    api.button_list_screen({"top_nav": {}, "button_list": ["X"]})

    assert api.poll_for_result() == ("button_selected", 0, "A")
    assert api.poll_for_result() == ("button_selected", 0, "X")
    assert api.poll_for_result() is None
