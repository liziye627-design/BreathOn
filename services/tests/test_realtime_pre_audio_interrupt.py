"""Cancellation must not advertise an audio generation the MCU has never seen."""

import base64
import json

import anyio
import pytest
from openai.types.realtime import ConversationItemTruncateEventParam
from pydantic import TypeAdapter
from test_realtime_session import audio, event

from backend.realtime.conversation import Conversation
from backend.realtime.wire import Frame, Kind, ServerControl


def active_conversation() -> Conversation:
    conversation = Conversation()
    event(conversation, "session.updated")
    conversation.input.clear()
    conversation.output.clear()
    event(conversation, "response.created")
    return conversation


async def drain(conversation: Conversation) -> list[Frame]:
    conversation.output.media(ServerControl(type="pong").frame(), size=0, generation=0)
    frames: list[Frame] = []
    with anyio.fail_after(1):
        while True:
            item = await conversation.output.take()
            conversation.output.task_done(item)
            if item.value == ServerControl(type="pong").frame():
                return frames
            frames.append(item.value)


def cancel(conversation: Conversation, trigger: str) -> None:
    if trigger == "cancelled":
        conversation.handle_event('{"type":"response.done","response":{"id":"r1","status":"cancelled"}}')
    else:
        event(conversation, trigger, "r2")


def assert_mcu_accepts(frames: list[Frame], latest: int = 0) -> None:
    # NiceMCU-bringup/firmware/app_breathon_realtime_control.c:61-78,112.
    for frame in frames:
        if frame.kind == Kind.SPEAKER:
            assert 0 < frame.generation <= latest
        else:
            control = ServerControl.model_validate_json(frame.payload)
            if control.type == "audio_start":
                assert frame.generation > 0
                latest = max(latest, frame.generation)
            elif control.type in {"interrupt", "audio_end"}:
                assert 0 < frame.generation <= latest, (control.type, frame.generation, latest)


@pytest.mark.parametrize("audio_state", ["none", "buffered", "queued"])
@pytest.mark.parametrize("trigger", ["input_audio_buffer.speech_started", "cancelled", "response.created"])
def test_cancel_when_audio_start_has_not_reached_writer(audio_state: str, trigger: str) -> None:
    async def scenario() -> None:
        # Given a response with no audio, converter-only audio, or a queued start.
        conversation = active_conversation()
        if audio_state == "queued":
            audio(conversation)
        elif audio_state == "buffered":
            conversation.handle_event(
                json.dumps(
                    {
                        "type": "response.output_audio.delta",
                        "response_id": "r1",
                        "item_id": "item-r1",
                        "content_index": 0,
                        "delta": base64.b64encode(b"\x01\x00" * 16).decode(),
                    }
                )
            )
        turn = conversation.turn
        assert turn is not None
        assert turn.started == (audio_state == "queued")
        # When cancelled, even late deltas and completion must stay cancelled.
        cancel(conversation, trigger)
        audio(conversation)
        conversation.handle_event('{"type":"response.done","response":{"id":"r1","status":"completed"}}')
        # Then there is no future interrupt, discarded start, end, or stale PCM.
        frames = await drain(conversation)
        assert_mcu_accepts(frames)
        expected = (
            [] if trigger == "response.created" else [ServerControl(type="state", state="listening").frame()]
        )
        assert frames == expected
        assert turn.interrupted
        if audio_state != "none":
            command = await conversation.input.take()
            truncate = TypeAdapter(ConversationItemTruncateEventParam).validate_json(command.value)
            conversation.input.task_done(command)
            assert (truncate["item_id"], truncate["audio_end_ms"]) == ("item-r1", 0)
        with anyio.move_on_after(0) as scope:
            await conversation.input.take()
        assert scope.cancel_called

    anyio.run(scenario, backend_options={"debug": True})


@pytest.mark.parametrize("playing", [False, True], ids=["start_in_flight", "pcm_in_flight"])
@pytest.mark.parametrize("trigger", ["input_audio_buffer.speech_started", "cancelled", "response.created"])
def test_cancel_when_device_start_is_committed(trigger: str, *, playing: bool) -> None:
    async def scenario() -> None:
        # Given the sole writer has committed a start, possibly already sending PCM.
        conversation = active_conversation()
        audio(conversation)
        in_flight = await conversation.output.take()
        start = in_flight.value
        assert ServerControl.model_validate_json(start.payload).type == "audio_start"
        # Session must make this synchronous call before awaiting stream.send.
        conversation.audio_starting(start.generation)
        if playing:
            conversation.output.task_done(in_flight)
            speaking = await conversation.output.take()
            conversation.output.task_done(speaking)
            in_flight = await conversation.output.take()
            assert in_flight.value.kind == Kind.SPEAKER
        # When cancellation happens during send, late PCM must not follow it.
        cancel(conversation, trigger)
        audio(conversation)
        if trigger != "response.created":
            conversation.interrupt()  # Repeated interruption is idempotent.
        # Then the in-flight frame completes before one real interrupt; queued PCM is gone.
        conversation.output.task_done(in_flight)
        frames = await drain(conversation)
        assert_mcu_accepts([start, *frames])
        expected = [ServerControl(type="interrupt").frame(1)]
        if trigger != "response.created":
            expected.append(ServerControl(type="state", state="listening").frame())
        assert frames == expected
        assert conversation.device_generation == 1

    anyio.run(scenario, backend_options={"debug": True})


def test_cancel_unsent_successor_preserves_prior_playback_interrupt() -> None:
    async def scenario() -> None:
        # Given generation 1's start is in flight when generation 2 is queued.
        conversation = active_conversation()
        audio(conversation)
        start = await conversation.output.take()
        conversation.audio_starting(start.value.generation)
        event(conversation, "response.created", "r2")
        audio(conversation, "r2")
        # When the successor is cancelled before the writer reaches its start.
        event(conversation, "input_audio_buffer.speech_started")
        audio(conversation, "r1")
        audio(conversation, "r2")
        # Then cancellation of generation 1 survives; generation 2 has no wire output.
        conversation.output.task_done(start)
        frames = await drain(conversation)
        assert_mcu_accepts([start.value, *frames])
        assert frames == [
            ServerControl(type="interrupt").frame(1),
            ServerControl(type="state", state="listening").frame(),
        ]

    anyio.run(scenario, backend_options={"debug": True})


def test_next_response_plays_when_first_response_cancelled_before_start() -> None:
    async def scenario() -> None:
        # Given generation 1's queued start and PCM have been cancelled.
        conversation = active_conversation()
        audio(conversation)
        event(conversation, "input_audio_buffer.speech_started")
        assert_mcu_accepts(await drain(conversation))
        # When a subsequent response arrives together with stale old audio.
        event(conversation, "response.created", "r2")
        audio(conversation, "r1")
        audio(conversation, "r2")
        # Then the new start and PCM use generation 2, which the MCU accepts directly.
        frames: list[Frame] = []
        for _ in range(3):
            item = await conversation.output.take()
            frames.append(item.value)
            conversation.output.task_done(item)
        assert_mcu_accepts(frames)
        assert frames[0] == ServerControl(type="audio_start").frame(2)
        assert frames[2].kind == Kind.SPEAKER
        assert frames[2].generation == 2

    anyio.run(scenario, backend_options={"debug": True})


@pytest.mark.parametrize("completed", [False, True])
def test_interrupt_when_response_has_only_transcript(*, completed: bool) -> None:
    async def scenario() -> None:
        # Given an identified assistant item with no audio ever emitted.
        conversation = active_conversation()
        transcript = (
            '{"type":"response.output_audio_transcript.delta","response_id":"r1",'
            '"item_id":"item-r1","content_index":0,"delta":"hello"}'
        )
        conversation.handle_event(transcript)
        if completed:
            conversation.handle_event('{"type":"response.done","response":{"id":"r1","status":"completed"}}')
        # When speech cancels it, late transcripts are ignored too.
        event(conversation, "input_audio_buffer.speech_started")
        conversation.handle_event(transcript)
        # Then only generation-zero listening controls remain; truncation still occurs.
        frames = await drain(conversation)
        assert_mcu_accepts(frames)
        assert frames == [ServerControl(type="state", state="listening").frame()] * (1 + completed)
        command = await conversation.input.take()
        truncate = TypeAdapter(ConversationItemTruncateEventParam).validate_json(command.value)
        conversation.input.task_done(command)
        assert (truncate["item_id"], truncate["audio_end_ms"]) == ("item-r1", 0)

    anyio.run(scenario, backend_options={"debug": True})
