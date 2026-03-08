from typing import Literal, Tuple

ButtonName = Literal["UP", "DOWN", "LEFT", "RIGHT", "PRESS", "KEY1", "KEY2", "KEY3"]
ButtonEvent = Tuple[ButtonName, Literal["press", "repeat"], int]
