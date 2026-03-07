from collections import deque
from typing import Deque, Optional, Tuple

ResultEvent = Tuple[str, int, str]
_result_queue: Deque[ResultEvent] = deque()


def clear_result_queue() -> None:
    _result_queue.clear()


def push_result(event: ResultEvent) -> None:
    _result_queue.append(event)


def poll_for_result() -> Optional[ResultEvent]:
    if _result_queue:
        return _result_queue.popleft()
    return None
