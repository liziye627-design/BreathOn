"""Backpressure and control priority without sleeping or actual media sinks."""

import anyio
import pytest

from backend.realtime.buffers import MediaBudget, PriorityBuffer
from backend.realtime.errors import GatewayError


def test_control_when_media_budget_full() -> None:
    async def scenario() -> None:
        # Given a saturated queue and a consumer that has not drained it.
        budget = MediaBudget(limit=640)
        queue = PriorityBuffer[str](budget)
        queue.media("stale", size=640, generation=1)
        # When cancellation arrives, neither operation awaits free space.
        queue.discard(1)
        queue.control("interrupt", generation=1)
        queue.media("new", size=640, generation=2)
        # Then control is delivered first and discarded media never appears.
        with anyio.fail_after(0.5):
            assert (await queue.take()).value == "interrupt"
            assert (await queue.take()).value == "new"

    anyio.run(scenario)


def test_overflow_when_combined_directions_exceed_budget() -> None:
    # Given one shared budget for microphone and speaker backlogs.
    budget = MediaBudget()
    microphone = PriorityBuffer[bytes](budget)
    speaker = PriorityBuffer[bytes](budget)
    microphone.media(b"", size=512 * 1024, generation=0)
    # When / Then the other direction fails synchronously, without dropping PCM.
    with pytest.raises(GatewayError, match="media_overflow"):
        speaker.media(b"", size=2, generation=1)


def test_audio_end_when_media_pending() -> None:
    async def scenario() -> None:
        # Given ordered PCM and its terminal marker, plus an urgent control.
        queue = PriorityBuffer[str](MediaBudget())
        queue.media("pcm", size=640, generation=1)
        queue.media("end", size=0, generation=1)
        queue.control("pong")
        # When / Then terminal markers cannot overtake their own PCM.
        assert [(await queue.take()).value for _ in range(3)] == ["pong", "pcm", "end"]

    anyio.run(scenario)
