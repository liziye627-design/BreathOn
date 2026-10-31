"""Observable TCP behavior with in-memory fake OpenAI events."""

import base64
import json

import anyio
import pytest
from anyio.streams.buffered import BufferedByteReceiveStream
from openai.types.realtime import InputAudioBufferAppendEventParam
from pydantic import TypeAdapter
from test_realtime_support import FakeUpstream, device, fake_settings, hello, next_control

from backend.realtime.wire import Frame, Kind, ServerControl, read_frame


def test_rejection_when_auth_wrong() -> None:
    async def scenario() -> None:
        # Given a fresh gateway with no upstream connections.
        fake = FakeUpstream()
        async with device(fake) as board:
            # When the pairing token is wrong.
            await board.stream.send(hello("wrong"))
            # Then the stable error precedes close, without paid connection.
            assert (await next_control(board.reader, "error")).code == "auth_failed"
            await next_control(board.reader, "closed")
            assert fake.connections == 0

    anyio.run(scenario)


def test_audio_when_bidirectional_session_completes() -> None:
    async def scenario() -> None:
        # Given an authorized, acknowledged session.
        fake = FakeUpstream()
        async with device(fake) as board:
            await board.start()
            # When board microphone arrives and the service produces a short response.
            await board.stream.send(Frame(Kind.MICROPHONE, b"\x01\x00" * 2000).encode())
            with anyio.fail_after(2):
                upstream_audio = TypeAdapter(InputAudioBufferAppendEventParam).validate_json(
                    await fake.sent.receive()
                )
            assert len(base64.b64decode(upstream_audio["audio"])) % 2 == 0
            await fake.events.send(
                '{"type":"response.created","response":{"id":"r1","status":"in_progress"}}'
            )
            await fake.events.send(
                json.dumps(
                    {
                        "type": "response.output_audio.delta",
                        "response_id": "r1",
                        "item_id": "item1",
                        "content_index": 0,
                        "delta": base64.b64encode(b"\x01\x00" * 2400).decode(),
                    }
                )
            )
            await fake.events.send('{"type":"response.done","response":{"id":"r1","status":"completed"}}')
            # Then audio_start precedes exactly 0.1 s of 16k PCM and audio_end follows it.
            await next_control(board.reader, "audio_start")
            pcm = bytearray()
            with anyio.fail_after(2):
                while True:
                    frame = await read_frame(board.reader)
                    if frame.kind == Kind.SPEAKER:
                        assert frame.generation == 1
                        pcm.extend(frame.payload)
                    elif ServerControl.model_validate_json(frame.payload).type == "audio_end":
                        break
            assert len(pcm) == 3200
            await board.stream.send(Frame(Kind.CONTROL, b'{"type":"stop"}').encode())
            await next_control(board.reader, "closed")
            assert fake.closed.is_set()

    anyio.run(scenario)


def test_busy_when_second_device_authenticated() -> None:
    async def scenario() -> None:
        # Given one active paid owner.
        fake = FakeUpstream()
        async with device(fake) as board:
            await board.start()
            # When a second paired device requests a session.
            async with await anyio.connect_tcp("127.0.0.1", board.port) as second:
                await second.send(hello())
                # Then admission is rejected before opening another upstream.
                error = await next_control(BufferedByteReceiveStream(second), "error")
                assert error.code == "busy"
                assert fake.connections == 1

    anyio.run(scenario)


def test_stop_when_upstream_connect_hangs() -> None:
    async def scenario() -> None:
        # Given a paid handshake that has not completed.
        fake = FakeUpstream()
        fake.block_connect = True
        async with device(fake) as board:
            await board.stream.send(hello())
            await fake.opened.wait()
            # When STOP arrives, the device reader remains available.
            await board.stream.send(Frame(Kind.CONTROL, b'{"type":"stop"}').encode())
            # Then the handshake is cancelled and its resources released.
            await next_control(board.reader, "closed")
            assert fake.closed.is_set()

    anyio.run(scenario)


@pytest.mark.parametrize("mode", ["hello", "session", "upstream"])
def test_timeout_when_peer_stalls(mode: str) -> None:
    async def scenario() -> None:
        # Given small injected test deadlines (production remains 5/180/15 seconds).
        fake = FakeUpstream()
        limits = (
            fake_settings(hello_timeout=0.1, max_session_seconds=0.1)
            if mode != "upstream"
            else fake_settings(upstream_timeout=0.1)
        )
        async with device(fake, limits) as board:
            # When the relevant peer does not progress.
            if mode != "hello":
                await board.stream.send(hello())
            # Then the matching error closes the session and releases upstream.
            error = await next_control(board.reader, "error")
            assert error.code == f"{mode}_timeout"
            await next_control(board.reader, "closed")
            assert fake.connections == (0 if mode == "hello" else 1)
            assert mode == "hello" or fake.closed.is_set()

    anyio.run(scenario)
