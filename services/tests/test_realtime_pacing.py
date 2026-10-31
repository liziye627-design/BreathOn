"""Deterministic media deadlines with an isolated clock and real AnyIO cancellation."""

from dataclasses import dataclass
from types import SimpleNamespace

import anyio
import pytest

from backend.realtime import buffers
from backend.realtime.buffers import Entry, MediaBudget, PriorityBuffer


@dataclass
class Clock:
    """Mutable test clock; never replaces the shared AnyIO module's clock."""

    now: float = 100.0

    def current_time(self) -> float:
        return self.now


@pytest.fixture
def clock(monkeypatch: pytest.MonkeyPatch) -> Clock:
    clock = Clock()
    monkeypatch.setattr(
        buffers,
        "anyio",
        SimpleNamespace(
            current_time=clock.current_time,
            Event=anyio.Event,
            move_on_after=anyio.move_on_after,
        ),
    )
    return clock


async def poll(queue: PriorityBuffer[str]) -> Entry[str] | None:
    """Return only an immediately eligible entry, cancelling a blocked take."""
    with anyio.move_on_after(0):
        return await queue.take()
    return None


@pytest.mark.parametrize(("jitter", "send_time"), [(0.012, 0.0), (0.0, 0.005), (0.012, 0.005)])
def test_media_deadlines_when_wakes_and_sends_are_late(clock: Clock, jitter: float, send_time: float) -> None:
    async def scenario() -> None:
        # Given 100 consecutive 20ms frames and a fresh clock at a nonzero epoch.
        budget = MediaBudget()
        queue = PriorityBuffer[str](budget)
        for _ in range(100):
            queue.media("pcm", size=640, generation=1, duration=0.02)
        queue.media("end", size=0, generation=1)
        # When dispatch wakes late and each send consumes time.
        for index in range(100):
            clock.now = 100 + index * 0.02 + (jitter if index % 2 else 0)
            item = await poll(queue)
            # Then eligibility follows elapsed media time, without accumulated delay.
            assert item is not None, f"frame {index} missed its media deadline"
            assert item.value == "pcm"
            clock.now += send_time
            queue.task_done(item)
        clock.now = 102.0 + jitter
        end = await poll(queue)
        assert end is not None
        assert end.value == "end"
        queue.task_done(end)
        assert budget.used == 0

    anyio.run(scenario)


@pytest.mark.parametrize("duration", [0.01, 0.02])
def test_catch_up_when_writer_stalls_is_limited_to_40ms(clock: Clock, duration: float) -> None:
    async def scenario() -> None:
        # Given a writer holding its first frame while more PCM is queued.
        budget = MediaBudget()
        queue = PriorityBuffer[str](budget)
        for _ in range(12):
            queue.media("pcm", size=320, generation=1, duration=duration)
        first = await queue.take()
        # When that send stalls for one second, completion cannot restart pacing.
        clock.now += 1
        queue.task_done(first)
        burst = 0.0
        while (item := await poll(queue)) is not None:
            burst += item.duration
            queue.task_done(item)
        # Then catch-up is useful but bounded, and the next frame needs time to pass.
        # A floating-point deadline tie may postpone the boundary frame.
        assert 0.04 - duration <= burst <= 0.04 + 1e-12
        clock.now += duration
        resumed = await poll(queue)
        assert resumed is not None
        queue.task_done(resumed)
        queue.clear()
        assert budget.used == 0

    anyio.run(scenario)


def test_first_frame_when_clock_epoch_is_large_has_no_catch_up_credit(clock: Clock) -> None:
    async def scenario() -> None:
        # Given a newly created buffer, not an overdue stream.
        queue = PriorityBuffer[str](MediaBudget())
        for _ in range(2):
            queue.media("pcm", size=640, generation=1, duration=0.02)
        # When its first frame completes immediately.
        first = await queue.take()
        queue.task_done(first)
        # Then a second frame must wait; a cancelled take keeps its reservation.
        assert await poll(queue) is None
        assert queue._budget.used == 640
        clock.now += 0.02
        second = await poll(queue)
        assert second is not None
        queue.task_done(second)
        assert queue._budget.used == 0

    anyio.run(scenario)


@pytest.mark.parametrize("cleanup", ["discard", "clear"])
def test_interruption_when_frame_is_in_flight_preserves_controls_and_budget(
    clock: Clock, cleanup: str
) -> None:
    async def scenario() -> None:
        # Given an in-flight frame and queued frames sharing the same reservation.
        budget = MediaBudget()
        queue = PriorityBuffer[str](budget)
        queue.media("old", size=640, generation=1, duration=0.02)
        queue.media("discarded", size=640, generation=1, duration=0.02)
        held = await queue.take()
        # When cancellation discards queued media before the in-flight send finishes.
        if cleanup == "discard":
            queue.discard(1)
            queue.discard(1)
        else:
            queue.clear()
            queue.clear()
        assert budget.used == 640
        queue.control("interrupt", generation=1)
        queue.media("new", size=640, generation=2, duration=0.02)
        queue.media("next", size=640, generation=2, duration=0.02)
        clock.now += 1
        queue.task_done(held)
        # Then control bypasses pacing; completing old media cannot delay the new turn.
        control = await poll(queue)
        assert control is not None
        assert control.value == "interrupt"
        queue.task_done(control)
        new = await poll(queue)
        assert new is not None
        assert new.value == "new"
        queue.task_done(new)
        assert await poll(queue) is None
        clock.now += 0.02
        following = await poll(queue)
        assert following is not None
        assert following.value == "next"
        queue.task_done(following)
        assert budget.used == 0

    anyio.run(scenario)


def test_microphone_when_duration_is_zero_remains_unpaced(clock: Clock) -> None:
    async def scenario() -> None:
        # Given microphone chunks without playback durations.
        clock.now = 200.0
        budget = MediaBudget()
        queue = PriorityBuffer[str](budget, "microphone")
        for _ in range(10):
            queue.media("mic", size=960, generation=0)
        # When the clock does not advance between writes.
        for _ in range(10):
            item = await poll(queue)
            # Then every chunk remains immediately eligible.
            assert item is not None
            queue.task_done(item)
        assert budget.used == 0
        assert budget.limit == 512 * 1024

    anyio.run(scenario)


def test_control_when_media_waits_does_not_advance_its_deadline(clock: Clock) -> None:
    async def scenario() -> None:
        # Given two paced frames with the first already completed.
        queue = PriorityBuffer[str](MediaBudget())
        for _ in range(2):
            queue.media("pcm", size=640, generation=1, duration=0.02)
        queue.task_done(await queue.take())
        clock.now += 0.005
        # When urgent control arrives before the next media deadline.
        queue.control("interrupt")
        control = await poll(queue)
        assert control is not None
        assert control.value == "interrupt"
        queue.task_done(control)
        # Then control is immediate, but PCM still waits until its original deadline.
        assert await poll(queue) is None
        clock.now = 100.02
        media = await poll(queue)
        assert media is not None
        queue.task_done(media)
        assert queue._budget.used == 0

    anyio.run(scenario)
