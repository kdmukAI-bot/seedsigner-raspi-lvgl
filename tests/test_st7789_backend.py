from seedsigner_lvgl.platform.pi_zero import ST7789Display, ST7789Config


class FakeGPIO:
    BOARD = 10
    OUT = 1
    HIGH = 1
    LOW = 0

    def __init__(self):
        self.mode = None
        self.warn = None
        self.setups = []
        self.outputs = []

    def setmode(self, mode):
        self.mode = mode

    def setwarnings(self, enabled):
        self.warn = enabled

    def setup(self, pin, mode):
        self.setups.append((pin, mode))

    def output(self, pin, value):
        self.outputs.append((pin, value))


class FakeSPI:
    def __init__(self):
        self.max_speed_hz = 0
        self.writes = []
        self.writes2 = []

    def writebytes(self, data):
        self.writes.append(tuple(data))

    def writebytes2(self, data):
        self.writes2.append(bytes(data))


def test_init_sets_expected_defaults():
    gpio = FakeGPIO()
    spi = FakeSPI()
    d = ST7789Display(gpio=gpio, spi=spi)

    d.init()

    assert gpio.mode == gpio.BOARD
    assert spi.max_speed_hz == 40_000_000
    assert (22, gpio.OUT) in gpio.setups
    assert (13, gpio.OUT) in gpio.setups
    assert (18, gpio.OUT) in gpio.setups


def test_show_rgb565_frame_size_check_and_write():
    gpio = FakeGPIO()
    spi = FakeSPI()
    cfg = ST7789Config(width=2, height=2)
    d = ST7789Display(gpio=gpio, spi=spi, cfg=cfg)

    d.init()

    good = bytes([0x00] * 8)  # 2*2*2
    d.show_rgb565_frame(good)
    assert spi.writes2[-1] == good

    bad = bytes([0x00] * 7)
    try:
        d.show_rgb565_frame(bad)
        assert False, "expected ValueError"
    except ValueError:
        pass
