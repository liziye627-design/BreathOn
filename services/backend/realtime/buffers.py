"""Nonblocking bounded producers; one consumer with control priority."""

import logging
from collections import deque
from dataclasses import dataclass
from typing import Final, Generic, Literal, TypeVar, final

import anyio

from .errors import GatewayError

T = TypeVar("T")
MAX_CONTROLS: Final = 128
MAX_MEDIA_ITEMS: Final = 4096
MAX_MEDIA_BURST: Final = 0.04


@final
class MediaBudget:
    """Mutable accounting shared by both directions, including writes in flight."""

    def __init__(self, limit: int = 512 * 1024) -> None:
        self.limit = limit
        self.used = 0

    def reserve(self, size: int) -> None:
        if self.used + size > self.limit:
            raise GatewayError("media_overflow")
        self.used += size

    def release(self, size: int) -> None:
        self.used -= size


@dataclass(frozen=True, slots=True)
class Entry(Generic[T]):
    value: T
    size: int = 0
    generation: int = 0
    duration: float = 0


@final
class PriorityBuffer(Generic[T]):
    """Mutable queues accessed only on the event-loop thread; producers never wait."""

    def __init__(
        self, budget: MediaBudget, direction: Literal["microphone", "speaker", "unspecified"] = "unspecified"
    ) -> None:
        self._budget = budget
        self._direction = direction
        self._controls: deque[Entry[T]] = deque()
        self._media: deque[Entry[T]] = deque()
        self._changed = anyio.Event()
        self._next_media: float | None = None

    def _wake(self) -> None:
        self._changed.set()
        self._changed = anyio.Event()

    def control(self, value: T, *, generation: int = 0) -> None:
        if len(self._controls) >= MAX_CONTROLS:
            raise GatewayError("control_overflow")
        self._controls.append(Entry(value, generation=generation))
        self._wake()

    def media(self, value: T, *, size: int, generation: int, duration: float = 0) -> None:
        # Zero-byte drain markers also need an item-count bound.
        if self._budget.used + size > self._budget.limit or len(self._media) >= MAX_MEDIA_ITEMS:
            logging.getLogger(__name__).warning(
                "realtime.media.full direction=%s shared_bytes=%d queued_bytes=%d items=%d incoming_bytes=%d",
                self._direction,
                self._budget.used,
                sum(item.size for item in self._media),
                len(self._media),
                size,
            )
        if len(self._media) >= MAX_MEDIA_ITEMS:
            raise GatewayError("media_overflow")
        self._budget.reserve(size)
        self._media.append(Entry(value, size, generation, duration))
        self._wake()

    def discard(self, generation: int) -> None:
        self._controls = deque(x for x in self._controls if x.generation != generation)
        for item in self._media:
            if item.generation == generation:
                self._budget.release(item.size)
        self._media = deque(x for x in self._media if x.generation != generation)
        if not self._media:
            self._next_media = None
        self._wake()

    def clear(self) -> None:
        for item in self._media:
            self._budget.release(item.size)
        self._controls.clear()
        self._media.clear()
        self._next_media = None
        self._wake()

    async def take(self) -> Entry[T]:
        while True:
            if self._controls:
                return self._controls.popleft()
            now = anyio.current_time()
            delay = 0.0 if self._next_media is None else self._next_media - now
            if self._media and delay <= 0:
                item = self._media.popleft()
                if item.duration:
                    # Anchor to media time, not send completion or a late wake.
                    # Include this frame in the 40ms catch-up burst allowance.
                    deadline = (
                        now
                        if self._next_media is None
                        else max(self._next_media, now - MAX_MEDIA_BURST + item.duration)
                    )
                    self._next_media = deadline + item.duration
                return item
            changed = self._changed
            if self._media:
                with anyio.move_on_after(delay):
                    await changed.wait()
            else:
                await changed.wait()

    def task_done(self, item: Entry[T]) -> None:
        self._budget.release(item.size)
