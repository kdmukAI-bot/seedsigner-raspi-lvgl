from __future__ import annotations

import time
from dataclasses import dataclass, field
from typing import Callable, Dict, Iterable, List, Optional

from .input_events import ButtonEvent, ButtonName


@dataclass(frozen=True)
class PiButtonsConfig:
    pin_up: int = 31
    pin_down: int = 35
    pin_left: int = 29
    pin_right: int = 37
    pin_press: int = 33
    pin_key1: int = 40
    pin_key2: int = 38
    pin_key3: int = 36
    first_repeat_threshold_ms: int = 225
    next_repeat_threshold_ms: int = 250

    @property
    def pins(self) -> Dict[ButtonName, int]:
        return {
            "UP": self.pin_up,
            "DOWN": self.pin_down,
            "LEFT": self.pin_left,
            "RIGHT": self.pin_right,
            "PRESS": self.pin_press,
            "KEY1": self.pin_key1,
            "KEY2": self.pin_key2,
            "KEY3": self.pin_key3,
        }


@dataclass
class PiButtonsBackend:
    config: PiButtonsConfig = field(default_factory=PiButtonsConfig)
    gpio: object | None = None
    time_ms: Callable[[], int] = lambda: int(time.monotonic() * 1000)

    active_key: Optional[ButtonName] = None
    last_event_ms: Optional[int] = None
    repeat_started: bool = False

    def init_gpio(self) -> None:
        gpio = self._gpio()
        gpio.setwarnings(False)
        gpio.setmode(gpio.BOARD)
        for pin in self.config.pins.values():
            gpio.setup(pin, gpio.IN, pull_up_down=gpio.PUD_UP)

    def read_pressed(self, keys: Optional[Iterable[ButtonName]] = None) -> List[ButtonName]:
        gpio = self._gpio()
        pin_map = self.config.pins
        key_order = list(keys) if keys is not None else list(pin_map.keys())

        pressed: List[ButtonName] = []
        for key in key_order:
            pin = pin_map[key]
            # Active-low: 0 means pressed, 1 means released
            if gpio.input(pin) == 0:
                pressed.append(key)
        return pressed

    def poll_event(self, keys: Optional[Iterable[ButtonName]] = None) -> Optional[ButtonEvent]:
        now = self.time_ms()
        pressed = self.read_pressed(keys)

        if not pressed:
            self.active_key = None
            self.last_event_ms = None
            self.repeat_started = False
            return None

        key = pressed[0]

        if self.active_key != key:
            self.active_key = key
            self.last_event_ms = now
            self.repeat_started = False
            return (key, "press", now)

        if self.last_event_ms is None:
            self.last_event_ms = now
            return None

        elapsed = now - self.last_event_ms
        threshold = (
            self.config.next_repeat_threshold_ms if self.repeat_started else self.config.first_repeat_threshold_ms
        )

        if elapsed >= threshold:
            self.last_event_ms = now
            self.repeat_started = True
            return (key, "repeat", now)

        return None

    def _gpio(self):
        if self.gpio is not None:
            return self.gpio
        try:
            import RPi.GPIO as GPIO  # type: ignore
        except ImportError as exc:
            raise RuntimeError("RPi.GPIO is required on Pi for button backend") from exc
        self.gpio = GPIO
        return self.gpio
