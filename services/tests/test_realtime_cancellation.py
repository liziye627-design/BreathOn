"""Prove upstream speech cancellation is independent of a blocked speaker send."""

import base64
import json
from typing import final

import anyio
from anyio.abc import ByteStream
from anyio.streams.buffered import BufferedByteReceiveStream
from openai.types.realtime import ConversationItemTruncateEventParam
from pydantic import TypeAdapter
from test_realtime_support import FakeUpstream, fake_settings
from typing_extensions import override

from backend.realtime.session import Session
from backend.realtime.wire import Kind


@final
class BlockedSpeaker(ByteStream):
    """Receive stays open; a speaker write never finishes until cancelled."""

    def __init__(self) -> None:
        self.blocked = anyio.Event()
        self.cancelled = anyio.Event()

    @override
    async def send(self, item: bytes) -> None:
        if item[3] == Kind.SPEAKER:
            self.blocked.set()
            try:
                await anyio.sleep_forever()
            finally:
                self.cancelled.set()

    @override
    async def receive(self, max_bytes: int = 65536) -> bytes:
        await anyio.sleep_forever()
        return b""

    @override
    async def send_eof(self) -> None:
        return

    @override
    async def aclose(self) -> None:
        return


def test_vad_when_device_write_blocked() -> None:
    async def scenario() -> None:
        # Given a real session whose device writer is stalled mid-frame.
        session = Session(fake_settings())
        fake = FakeUpstream()
        speaker = BlockedSpeaker()
        async with anyio.create_task_group() as group:
            group.start_soon(session.run, speaker, BufferedByteReceiveStream(speaker), fake.connect)
            await fake.sent.receive()
            await fake.events.send('{"type":"session.updated"}')
            await fake.sent.receive()
            await fake.events.send('{"type":"response.created","response":{"id":"r","status":"in_progress"}}')
            await fake.events.send(
                json.dumps(
                    {
                        "type": "response.output_audio.delta",
                        "response_id": "r",
                        "item_id": "i",
                        "content_index": 0,
                        "delta": base64.b64encode(b"\x01\x00" * 24000).decode(),
                    }
                )
            )
            await speaker.blocked.wait()
            # When server VAD detects speech while that write is still blocked.
            await fake.events.send('{"type":"input_audio_buffer.speech_started"}')
            # Then truncate reaches upstream without waiting for the speaker.
            with anyio.fail_after(0.5):
                truncate = TypeAdapter(ConversationItemTruncateEventParam).validate_json(
                    await fake.sent.receive()
                )
            assert truncate["audio_end_ms"] == 0
            assert not speaker.cancelled.is_set()
            group.cancel_scope.cancel()
        assert speaker.cancelled.is_set()
        assert fake.closed.is_set()

    anyio.run(scenario)
