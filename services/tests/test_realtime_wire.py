"""Wire boundaries exercised without credentials or network access."""

import struct

import anyio
import pytest
from anyio.streams.buffered import BufferedByteReceiveStream

from backend.realtime.errors import GatewayError
from backend.realtime.wire import Frame, Kind, ServerControl, read_frame


@pytest.mark.parametrize("chunk_size", [1, 3, 12, 4096])
def test_frame_when_fragmented(chunk_size: int) -> None:
    async def scenario() -> None:
        # Given a fragmented wire stream, including adjacent frames.
        expected = Frame(Kind.MICROPHONE, b"\x01\x02" * 320)
        raw = expected.encode() * 2
        send, receive = anyio.create_memory_object_stream[bytes](4096)
        async with send, receive:
            for start in range(0, len(raw), chunk_size):
                send.send_nowait(raw[start : start + chunk_size])
            buffered = BufferedByteReceiveStream(receive)
            # When / Then both frames retain their exact boundaries.
            assert await read_frame(buffered) == expected
            assert await read_frame(buffered) == expected

    anyio.run(scenario)


@pytest.mark.parametrize(
    "header",
    [
        struct.pack("!2sBBII", b"XX", 1, 1, 0, 0),
        struct.pack("!2sBBII", b"BR", 2, 1, 0, 0),
        struct.pack("!2sBBII", b"BR", 1, 9, 0, 0),
        struct.pack("!2sBBII", b"BR", 1, 2, 4097, 0),
        struct.pack("!2sBBII", b"BR", 1, 2, 3, 0),
    ],
)
def test_reject_when_header_invalid(header: bytes) -> None:
    async def scenario() -> None:
        # Given only a malicious header, with no following payload.
        send, receive = anyio.create_memory_object_stream[bytes](1)
        async with send, receive:
            send.send_nowait(header)
            # When / Then rejection happens before waiting for the payload.
            with pytest.raises(GatewayError), anyio.fail_after(0.5):
                await read_frame(BufferedByteReceiveStream(receive))

    anyio.run(scenario)


@pytest.mark.parametrize("text", ["中文🎵" * 2000, '\x00\n"\\' * 2000])
def test_transcript_when_untrusted_text_long(text: str) -> None:
    # Given long Unicode or JSON-escaped content.
    control = ServerControl(type="transcript", role="assistant", text=text)
    # When it crosses the board boundary.
    frame = control.frame()
    decoded = ServerControl.model_validate_json(frame.payload)
    # Then both byte bounds and UTF-8 hold.
    assert len(frame.payload) <= 4096
    assert decoded.text is not None
    assert len(decoded.text.encode("utf-8")) <= 512
