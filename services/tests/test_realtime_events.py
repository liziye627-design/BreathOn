"""VAD state, ASR/TTS transcript mapping and invalid upstream boundary tests."""

import json

import anyio
import pytest
from test_realtime_session import event

from backend.realtime.conversation import Conversation
from backend.realtime.errors import GatewayError
from backend.realtime.events import parse_event
from backend.realtime.wire import ServerControl


def test_vad_when_speech_starts_and_stops() -> None:
    async def scenario() -> None:
        # Given an acknowledged session, with no assistant response yet.
        session = Conversation()
        event(session, "session.updated")
        session.output.clear()
        # When VAD reports a complete input turn.
        event(session, "input_audio_buffer.speech_started")
        event(session, "input_audio_buffer.speech_stopped")
        # Then states reflect listening followed by thinking.
        frames = [(await session.output.take()).value for _ in range(2)]
        assert [ServerControl.model_validate_json(f.payload).state for f in frames] == [
            "listening",
            "thinking",
        ]

    anyio.run(scenario)


def test_transcripts_when_asr_and_tts_arrive() -> None:
    async def scenario() -> None:
        # Given one active response.
        session = Conversation()
        event(session, "session.updated")
        event(session, "response.created")
        session.output.clear()
        # When independent ASR and assistant transcript events arrive.
        session.handle_event(
            '{"type":"conversation.item.input_audio_transcription.completed","transcript":"你好"}'
        )
        identity = {"response_id": "r1", "item_id": "item1", "content_index": 0}
        for delta in ["你好", ",朋友"]:
            session.handle_event(
                json.dumps({**identity, "type": "response.output_audio_transcript.delta", "delta": delta})
            )
        session.handle_event(
            json.dumps(
                {**identity, "type": "response.output_audio_transcript.done", "transcript": "你好,朋友。"}
            )
        )
        # Then user text and accumulated/final assistant text retain their roles.
        frames = [(await session.output.take()).value for _ in range(4)]
        controls = [ServerControl.model_validate_json(f.payload) for f in frames]
        assert [(c.role, c.text) for c in controls] == [
            ("user", "你好"),
            ("assistant", "你好"),
            ("assistant", "你好,朋友"),
            ("assistant", "你好,朋友。"),
        ]

    anyio.run(scenario)


@pytest.mark.parametrize(
    "raw",
    [
        b"\xff",
        '{"type":"response.output_audio.delta"}',
        '{"type":"response.done","response":{"id":"r","status":"unknown"}}',
    ],
)
def test_upstream_when_malformed(raw: str | bytes) -> None:
    # Given invalid UTF-8, missing fields or an unknown response status.
    # When / Then known malformed events fail closed.
    with pytest.raises(GatewayError, match="upstream_protocol"):
        parse_event(raw)
