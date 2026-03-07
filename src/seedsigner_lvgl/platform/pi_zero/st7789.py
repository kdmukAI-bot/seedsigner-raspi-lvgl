from __future__ import annotations

from dataclasses import dataclass
from typing import Protocol
import time


class GPIOProtocol(Protocol):
    BOARD: int
    OUT: int
    HIGH: int
    LOW: int

    def setmode(self, mode: int) -> None: ...
    def setwarnings(self, enabled: bool) -> None: ...
    def setup(self, pin: int, mode: int) -> None: ...
    def output(self, pin: int, value: int) -> None: ...


class SPIProtocol(Protocol):
    max_speed_hz: int

    def writebytes(self, data: list[int]) -> None: ...
    def writebytes2(self, data: bytes | bytearray | list[int]) -> None: ...


@dataclass
class ST7789Config:
    width: int = 240
    height: int = 240
    dc_pin: int = 22
    rst_pin: int = 13
    bl_pin: int = 18
    spi_bus: int = 0
    spi_device: int = 0
    spi_max_speed_hz: int = 40_000_000


class ST7789Display:
    """Pi Zero ST7789 backend aligned with SeedSigner Python pin defaults."""

    def __init__(self, gpio: GPIOProtocol, spi: SPIProtocol, cfg: ST7789Config | None = None):
        self.gpio = gpio
        self.spi = spi
        self.cfg = cfg or ST7789Config()

    def init(self) -> None:
        self.gpio.setmode(self.gpio.BOARD)
        self.gpio.setwarnings(False)
        self.gpio.setup(self.cfg.dc_pin, self.gpio.OUT)
        self.gpio.setup(self.cfg.rst_pin, self.gpio.OUT)
        self.gpio.setup(self.cfg.bl_pin, self.gpio.OUT)
        self.gpio.output(self.cfg.bl_pin, self.gpio.HIGH)

        self.spi.max_speed_hz = self.cfg.spi_max_speed_hz

        self.reset()
        self.command(0x36)
        self.data(0x70)
        self.command(0x3A)
        self.data(0x05)
        self.command(0x21)  # inversion ON
        self.command(0x11)  # sleep out
        self.command(0x29)  # display on

    def reset(self) -> None:
        self.gpio.output(self.cfg.rst_pin, self.gpio.HIGH)
        time.sleep(0.01)
        self.gpio.output(self.cfg.rst_pin, self.gpio.LOW)
        time.sleep(0.01)
        self.gpio.output(self.cfg.rst_pin, self.gpio.HIGH)
        time.sleep(0.01)

    def command(self, cmd: int) -> None:
        self.gpio.output(self.cfg.dc_pin, self.gpio.LOW)
        self.spi.writebytes([cmd & 0xFF])

    def data(self, val: int) -> None:
        self.gpio.output(self.cfg.dc_pin, self.gpio.HIGH)
        self.spi.writebytes([val & 0xFF])

    def set_window(self, x_start: int, y_start: int, x_end: int, y_end: int) -> None:
        self.command(0x2A)
        self.data(0x00)
        self.data(x_start & 0xFF)
        self.data(0x00)
        self.data((x_end - 1) & 0xFF)

        self.command(0x2B)
        self.data(0x00)
        self.data(y_start & 0xFF)
        self.data(0x00)
        self.data((y_end - 1) & 0xFF)

        self.command(0x2C)

    def show_rgb565_frame(self, frame: bytes) -> None:
        expected = self.cfg.width * self.cfg.height * 2
        if len(frame) != expected:
            raise ValueError(f"frame must be exactly {expected} bytes for {self.cfg.width}x{self.cfg.height} RGB565")
        self.set_window(0, 0, self.cfg.width, self.cfg.height)
        self.gpio.output(self.cfg.dc_pin, self.gpio.HIGH)
        self.spi.writebytes2(frame)

    def clear_white(self) -> None:
        frame = bytes([0xFF]) * (self.cfg.width * self.cfg.height * 2)
        self.show_rgb565_frame(frame)
