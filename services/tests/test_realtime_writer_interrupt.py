"""Exercise writer cancellation and terminal controls without a live upstream."""

import base64
import json
from typing import final

import anyio
import pytest
from anyio.abc import ByteStream
from anyio.streams.buffered import BufferedByteReceiveStream
from openai.types.realtime import ConversationItemTruncateEventParam
from pydantic import TypeAdapter
from test_realtime_pre_audio_interrupt import active_conversation
from test_realtime_session import audio, event
from test_realtime_support import FakeUpstream, fake_settings, hello
from typing_extensions import override

from backend.realtime.errors import ErrorCode
from backend.realtime.gateway import Gateway
from backend.realtime.session import Session
from backend.realtime.wire import Kind, ServerControl


@final
class GatedDevice(ByteStream):
    """Capture writes and gate one frame to expose the writer's in-flight state."""

    def __init__(self, block_kind: Kind | None = None) -> None:
        self.block_kind = block_kind
        self.blocked = anyio.Event()
        self.release = anyio.Event()
        self.drained = anyio.Event()
        self.closed = anyio.Event()
        self.handshake = b""
        self.fail_start = False
        self.writes: list[bytes] = []

    @override
    async def send(self, item: bytes) -> None:
        is_start = item == ServerControl(type="audio_start").frame(1).encode()
        if not self.blocked.is_set() and (
            (self.block_kind == Kind.CONTROL and is_start)
            or (self.block_kind == Kind.SPEAKER and item[3] == Kind.SPEAKER)
        ):
            self.blocked.set()
            await self.release.wait()
        if is_start and self.fail_start:
            self.writes.append(item[:6])  # Model a truncated frame on the transport.
            raise anyio.BrokenResourceError
        self.writes.append(item)
        if item == ServerControl(type="pong").frame().encode():
            self.drained.set()

    @override
    async def receive(self, max_bytes: int = 65536) -> bytes:
        if self.handshake:
            chunk, self.handshake = self.handshake[:max_bytes], self.handshake[max_bytes:]
            return chunk
        await anyio.sleep_forever()
        return b""

    @override
    async def send_eof(self) -> None:
        return

    @override
    async def aclose(self) -> None:
        self.closed.set()


@pytest.mark.parametrize("block_kind", [Kind.CONTROL, Kind.SPEAKER], ids=["audio_start", "pcm"])
def test_writer_interrupt_when_send_is_in_flight(block_kind: Kind) -> None:
    async def scenario() -> None:
        # Given all real Session pumps, with the device paused inside send().
        session = Session(fake_settings())
        upstream = FakeUpstream()
        stream = GatedDevice(block_kind)
        with anyio.fail_after(3):
            async with anyio.create_task_group() as group:
                group.start_soon(session.run, stream, BufferedByteReceiveStream(stream), upstream.connect)
                await upstream.sent.receive()
                await upstream.events.send('{"type":"session.updated"}')
                await upstream.sent.receive()
                await upstream.events.send(
                    '{"type":"response.created","response":{"id":"r1","status":"in_progress"}}'
                )
                delta = json.dumps(
                    {
                        "type": "response.output_audio.delta",
                        "response_id": "r1",
                        "item_id": "item-r1",
                        "content_index": 0,
                        "delta": base64.b64encode(b"\x01\x00" * 24000).decode(),
                    }
                )
                await upstream.events.send(delta)
                await stream.blocked.wait()
                written_before = len(stream.writes)
                # When VAD cancels during that await, truncate must proceed independently.
                await upstream.events.send('{"type":"input_audio_buffer.speech_started"}')
                truncate = TypeAdapter(ConversationItemTruncateEventParam).validate_json(
                    await upstream.sent.receive()
                )
                assert (truncate["item_id"], truncate["audio_end_ms"]) == ("item-r1", 0)
                assert session.writing
                session.conversation.handle_event(delta)  # Stale audio after processed cancellation.
                session.conversation.output.media(ServerControl(type="pong").frame(), size=0, generation=0)
                stream.release.set()
                await stream.drained.wait()
                session.stopped.set()
                # Then only the in-flight frame precedes interrupt; no queued or late PCM follows.
                assert stream.writes[written_before + 1 :] == [
                    ServerControl(type="interrupt").frame(1).encode(),
                    ServerControl(type="state", state="listening").frame().encode(),
                    ServerControl(type="pong").frame().encode(),
                ]
            assert session.conversation.device_generation == 1
            assert session.speaker_bytes == (640 if block_kind == Kind.SPEAKER else 0)
            assert upstream.closed.is_set()

    anyio.run(scenario, backend_options={"debug": True})


def test_gateway_closes_without_appending_controls_when_start_send_fails() -> None:
    async def scenario() -> None:
        # Given the real gateway and a device which fails midway through audio_start.
        upstream = FakeUpstream()
        gateway = Gateway(fake_settings(), upstream.connect)
        stream = GatedDevice(Kind.CONTROL)
        stream.handshake = hello()
        stream.fail_start = True
        with anyio.fail_after(3):
            async with anyio.create_task_group() as group:
                group.start_soon(gateway.handle, stream)
                await upstream.sent.receive()
                await upstream.events.send('{"type":"session.updated"}')
                await upstream.sent.receive()
                await upstream.events.send(
                    '{"type":"response.created","response":{"id":"r1","status":"in_progress"}}'
                )
                await upstream.events.send(
                    json.dumps(
                        {
                            "type": "response.output_audio.delta",
                            "response_id": "r1",
                            "item_id": "item-r1",
                            "content_index": 0,
                            "delta": base64.b64encode(b"\x01\x00" * 24000).decode(),
                        }
                    )
                )
                await stream.blocked.wait()
                # When the committed start send raises after a partial frame.
                stream.release.set()
                await stream.closed.wait()
            # Then the existing writing guard closes without appending a corrupt control batch.
            assert stream.writes[2:] == [
                ServerControl(type="audio_start").frame(1).encode()[:6],
            ]
            assert upstream.closed.is_set()

    anyio.run(scenario, backend_options={"debug": True})


@pytest.mark.parametrize("state", ["no_session", "no_audio", "queued", "started", "successor"])
@pytest.mark.parametrize("failure", [None, "response_failed"])
def test_finish_interrupt_uses_device_generation(state: str, failure: ErrorCode | None) -> None:
    async def scenario() -> None:
        # Given a current response which may be newer than the last committed audio start.
        session = Session(fake_settings())
        session.conversation = active_conversation()
        if state in {"queued", "started", "successor"}:
            audio(session.conversation)
        if state in {"started", "successor"}:
            session.conversation.audio_starting(1)
        if state == "successor":
            event(session.conversation, "response.created", "r2")
            audio(session.conversation, "r2")
        session.conversation.output.clear()  # Session.run clears pending controls on exit.
        stream = GatedDevice()
        gateway = Gateway(fake_settings(), FakeUpstream().connect)
        # When terminal cleanup writes its actual encoded control batch.
        await gateway._finish(stream, None if state == "no_session" else session, failure)
        # Then only the known device generation is interrupted, before error/stopped/closed.
        expected: list[bytes] = []
        if state in {"started", "successor"}:
            expected.append(ServerControl(type="interrupt").frame(1).encode())
        if failure is not None:
            expected.append(ServerControl(type="error", code=failure).frame().encode())
        expected.extend(
            [
                ServerControl(type="state", state="stopped").frame().encode(),
                ServerControl(type="closed").frame().encode(),
            ]
        )
        assert stream.writes == [b"".join(expected)]

    anyio.run(scenario, backend_options={"debug": True})
