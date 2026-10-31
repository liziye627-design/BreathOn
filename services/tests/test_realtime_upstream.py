"""Exercise real websockets redirect handling using only disposable loopback HTTP."""

import anyio
import pytest
from anyio.abc import SocketAttribute, SocketStream
from anyio.streams.buffered import BufferedByteReceiveStream
from websockets.exceptions import InvalidStatus

from backend.realtime.upstream import NoRedirectConnect


@pytest.mark.parametrize("status", [301, 302, 307, 308])
def test_redirect_when_server_requests_another_origin(status: int) -> None:
    async def scenario() -> None:
        # Given a real local HTTP upgrade endpoint that always redirects.
        attempts: list[bytes] = []
        async with await anyio.create_tcp_listener(local_host="127.0.0.1", local_port=0) as listener:
            port = listener.extra(SocketAttribute.local_port)

            async def redirect(stream: SocketStream) -> None:
                async with stream:
                    request = await BufferedByteReceiveStream(stream).receive_until(b"\r\n\r\n", 4096)
                    attempts.append(request)
                    await stream.send(
                        (
                            f"HTTP/1.1 {status} Redirect\r\n"
                            f"Location: ws://localhost:{port}/other\r\n"
                            "Content-Length: 0\r\nConnection: close\r\n\r\n"
                        ).encode()
                    )

            async with anyio.create_task_group() as group:
                group.start_soon(listener.serve, redirect)
                # When the production connect subclass encounters the redirect.
                with pytest.raises(InvalidStatus) as rejected, anyio.fail_after(2):
                    async with NoRedirectConnect(
                        f"ws://127.0.0.1:{port}/first",
                        proxy=None,
                        additional_headers={"Authorization": "Bearer fake-only"},
                    ):
                        pytest.fail("Redirect must not establish a WebSocket")
                # Then the original error is returned after exactly one handshake.
                assert rejected.value.response.status_code == status
                assert len(attempts) == 1
                group.cancel_scope.cancel()

    anyio.run(scenario)
