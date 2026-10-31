"""Terminal failures, peer disconnects and released ownership over local TCP."""

import json

import anyio
from anyio.streams.buffered import BufferedByteReceiveStream
from test_realtime_support import FakeUpstream, device, hello, next_control

from backend.realtime.wire import Frame, Kind


def test_disconnect_when_board_drops_socket() -> None:
    async def scenario() -> None:
        # Given an active session.
        fake = FakeUpstream()
        async with device(fake) as board:
            await board.start()
            # When the board disappears without sending STOP.
            await board.stream.aclose()
            # Then the paid connection is cancelled and released.
            with anyio.fail_after(2):
                await fake.closed.wait()

    anyio.run(scenario)


def test_failure_when_response_done_not_completed() -> None:
    async def scenario() -> None:
        # Given an active response.
        fake = FakeUpstream()
        async with device(fake) as board:
            await board.start()
            await fake.events.send(
                '{"type":"response.created","response":{"id":"r1","status":"in_progress"}}'
            )
            # When upstream reports failure, with a private raw detail.
            await fake.events.send(
                json.dumps(
                    {
                        "type": "response.done",
                        "response": {
                            "id": "r1",
                            "status": "failed",
                            "status_details": {
                                "type": "failed",
                                "error": {"code": "server_error", "message": "private-upstream-detail"},
                            },
                        },
                    }
                )
            )
            # Then the public failure is sanitized and the paid owner closes.
            failure = await next_control(board.reader, "error")
            assert failure.code == "response_failed"
            assert "private-upstream-detail" not in failure.model_dump_json()
            await next_control(board.reader, "closed")
            assert fake.closed.is_set()

    anyio.run(scenario)


def test_ownership_when_previous_session_stopped() -> None:
    async def scenario() -> None:
        # Given an owner that explicitly stops.
        fake = FakeUpstream()
        async with device(fake) as board:
            await board.start()
            # When STOP finishes and a new explicit hello follows.
            await board.stream.send(Frame(Kind.CONTROL, b'{"type":"stop"}').encode())
            await next_control(board.reader, "closed")
            async with await anyio.connect_tcp("127.0.0.1", board.port) as second:
                await second.send(hello())
                await fake.sent.receive()
                await fake.events.send('{"type":"session.updated"}')
                # Then a new explicitly started session may acquire the released slot.
                await next_control(BufferedByteReceiveStream(second), "ready")
                assert fake.connections == 2

    anyio.run(scenario)
