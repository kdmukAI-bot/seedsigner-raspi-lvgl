from seedsigner_lvgl.platform.pi_zero import PiButtonsBackend, PiButtonsConfig


class FakeGPIO:
    BOARD = "BOARD"
    IN = "IN"
    PUD_UP = "PUD_UP"

    def __init__(self):
        self.mode = None
        self.warnings = None
        self.setup_calls = []
        self.pin_values = {}

    def setwarnings(self, flag):
        self.warnings = flag

    def setmode(self, mode):
        self.mode = mode

    def setup(self, pin, mode, pull_up_down=None):
        self.setup_calls.append((pin, mode, pull_up_down))
        self.pin_values.setdefault(pin, 1)

    def input(self, pin):
        return self.pin_values.get(pin, 1)


class FakeClock:
    def __init__(self, start=0):
        self.now = start

    def __call__(self):
        return self.now

    def advance(self, ms):
        self.now += ms


def test_pin_setup_uses_expected_board_pins():
    gpio = FakeGPIO()
    backend = PiButtonsBackend(gpio=gpio)
    backend.init_gpio()

    config = PiButtonsConfig()
    expected_pins = set(config.pins.values())
    actual_pins = {call[0] for call in gpio.setup_calls}

    assert gpio.mode == gpio.BOARD
    assert expected_pins == actual_pins
    assert all(call[1] == gpio.IN for call in gpio.setup_calls)
    assert all(call[2] == gpio.PUD_UP for call in gpio.setup_calls)


def test_active_low_press_detection():
    gpio = FakeGPIO()
    backend = PiButtonsBackend(gpio=gpio)
    backend.init_gpio()

    pins = backend.config.pins
    gpio.pin_values[pins["UP"]] = 0
    gpio.pin_values[pins["KEY1"]] = 0

    pressed = backend.read_pressed()
    assert "UP" in pressed
    assert "KEY1" in pressed


def test_repeat_timing_behavior_and_event_shape():
    gpio = FakeGPIO()
    clock = FakeClock(start=1000)
    backend = PiButtonsBackend(gpio=gpio, time_ms=clock)
    backend.init_gpio()

    up_pin = backend.config.pins["UP"]
    gpio.pin_values[up_pin] = 0

    ev1 = backend.poll_event()
    assert ev1 == ("UP", "press", 1000)
    assert isinstance(ev1, tuple) and len(ev1) == 3

    clock.advance(200)
    assert backend.poll_event() is None

    clock.advance(25)
    ev2 = backend.poll_event()
    assert ev2 == ("UP", "repeat", 1225)

    clock.advance(249)
    assert backend.poll_event() is None

    clock.advance(1)
    ev3 = backend.poll_event()
    assert ev3 == ("UP", "repeat", 1475)

    gpio.pin_values[up_pin] = 1
    assert backend.poll_event() is None
