"""Deterministic event mapping and cancellation with a fake upstream."""

import base64
import json

import anyio
import pytest
from openai.types.realtime import ConversationItemTruncateEventParam, ResponseCreateEventParam
from pydantic import TypeAdapter

from backend.realtime.config import Settings
from backend.realtime.conversation import Conversation
from backend.realtime.errors import GatewayError
from backend.realtime.wire import Kind, ServerControl


def event(session: Conversation, kind: str, response_id: str = "r1") -> None:
    session.handle_event(json.dumps({"type": kind, "response": {"id": response_id, "status": "in_progress"}}))


def audio(session: Conversation, response_id: str = "r1") -> None:
    session.handle_event(
        json.dumps(
            {
                "type": "response.output_audio.delta",
                "response_id": response_id,
                "item_id": "item-" + response_id,
                "content_index": 0,
                "delta": base64.b64encode(b"\x01\x00" * 24000).decode(),
            }
        )
    )


def test_greeting_when_session_acknowledged_once() -> None:
    async def scenario() -> None:
        # Given a new authorized session, no greeting or ready yet.
        session = Conversation()
        assert not session.ready.is_set()
        # When the service acknowledges twice.
        event(session, "session.updated")
        event(session, "session.updated")
        # Then one ready and one bounded greeting are queued.
        frame = (await session.output.take()).value
        assert ServerControl.model_validate_json(frame.payload).type == "ready"
        command = TypeAdapter(ResponseCreateEventParam).validate_json((await session.input.take()).value)
        assert command["type"] == "response.create"
        assert "response" in command
        assert "max_output_tokens" in command["response"]
        assert command["response"]["max_output_tokens"] == 160
        with anyio.move_on_after(0) as scope:
            await session.input.take()
        assert scope.cancel_called

    anyio.run(scenario)


def test_interruption_when_speaker_full_and_late_audio_arrives() -> None:
    async def scenario() -> None:
        # Given pending media and an unstarted speaker consumer.
        session = Conversation()
        event(session, "session.updated")
        session.input.clear()
        session.output.clear()
        event(session, "response.created")
        audio(session)
        # When speech arrives and a stale delta follows immediately.
        event(session, "input_audio_buffer.speech_started")
        audio(session)
        # Then no interrupt is sent for unseen audio; truncate is independent of playback.
        frame = (await session.output.take()).value
        assert frame == ServerControl(type="state", state="listening").frame()
        with anyio.move_on_after(0) as scope:
            await session.output.take()
        assert scope.cancel_called
        command = TypeAdapter(ConversationItemTruncateEventParam).validate_json(
            (await session.input.take()).value
        )
        assert command["type"] == "conversation.item.truncate"
        assert command["audio_end_ms"] == 0
        assert command["item_id"] == "item-r1"
        event(session, "response.created", "r2")
        audio(session, "r1")
        audio(session, "r2")
        frames = [(await session.output.take()).value for _ in range(3)]
        assert frames[0].generation == 2
        assert ServerControl.model_validate_json(frames[0].payload).type == "audio_start"
        assert frames[2].kind == Kind.SPEAKER
        assert frames[2].generation == 2

    anyio.run(scenario)


@pytest.mark.parametrize("status", ["failed", "incomplete"])
def test_response_when_terminal_failure(status: str) -> None:
    # Given active audio with its audio-done event already received.
    session = Conversation()
    event(session, "session.updated")
    event(session, "response.created")
    audio(session)
    # When / Then terminal failure is never converted into successful completion.
    with pytest.raises(GatewayError, match="response_failed"):
        session.handle_event(
            json.dumps({"type": "response.done", "response": {"id": "r1", "status": status}})
        )


def test_configuration_when_credentials_missing() -> None:
    # Given explicitly empty fake environment; never inspect the user's credentials.
    # When / Then startup fails closed.
    with pytest.raises(GatewayError, match="credentials_missing"):
        Settings.from_env({})


def test_configuration_when_limit_over_180() -> None:
    # Given fake credentials and an excessive session limit.
    values = {
        "OPENAI_API_KEY": "fake",
        "BREATHON_DEVICE_TOKEN": "fake",
        "BREATHON_MAX_SESSION_SECONDS": "181",
    }
    # When / Then the paid-session hard ceiling is enforced.
    with pytest.raises(GatewayError, match="invalid_config"):
        Settings.from_env(values)
