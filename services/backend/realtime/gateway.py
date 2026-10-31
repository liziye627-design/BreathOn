"""Authentication, one-session admission, deadlines and terminal cleanup."""

import hmac
import logging
from typing import assert_never, final

import anyio
from anyio.abc import ByteStream, SocketAttribute
from anyio.streams.buffered import BufferedByteReceiveStream

from .config import Settings
from .errors import ErrorCode, GatewayError
from .networking import pin_outgoing_interface
from .session import Session
from .upstream import Connector, open_upstream
from .wire import Hello, Interrupted, Kind, Ping, ServerControl, Stop, parse_control, read_frame


def _code(error: GatewayError | BaseExceptionGroup[GatewayError]) -> ErrorCode:
    match error:
        case GatewayError():
            return error.code
        case BaseExceptionGroup():
            return _code(error.exceptions[0])
        case unreachable:
            assert_never(unreachable)


@final
class Gateway:
    """One paid owner; waiting/unauthenticated sockets never open upstream."""

    def __init__(self, settings: Settings, connector: Connector = open_upstream) -> None:
        self.settings = settings
        self.connector = connector
        self._owner = anyio.CapacityLimiter(1)

    async def _authenticate(self, reader: BufferedByteReceiveStream) -> None:
        try:
            with anyio.fail_after(self.settings.hello_timeout):
                frame = await read_frame(reader)
        except TimeoutError:
            raise GatewayError("hello_timeout") from None
        if frame.kind != Kind.CONTROL or frame.generation != 0:
            raise GatewayError("auth_failed")
        hello = parse_control(frame)
        match hello:
            case Hello():
                supplied = hello.token.get_secret_value().encode("utf-8")
                expected = self.settings.device_token.get_secret_value().encode("utf-8")
                if not hmac.compare_digest(supplied, expected):
                    raise GatewayError("auth_failed")
            case Stop() | Ping() | Interrupted():
                raise GatewayError("auth_failed")
            case unreachable:
                assert_never(unreachable)

    async def handle(self, stream: ByteStream) -> None:
        """Sanitize known failures at the boundary; let programming errors propagate."""
        session: Session | None = None
        failure: ErrorCode | None = None
        admitted = False
        try:
            try:
                reader = BufferedByteReceiveStream(stream)
                await self._authenticate(reader)
                try:
                    self._owner.acquire_nowait()
                    admitted = True
                except anyio.WouldBlock:
                    raise GatewayError("busy") from None
                session = Session(self.settings)
                with anyio.move_on_after(self.settings.max_session_seconds) as deadline:
                    await session.run(stream, reader, self.connector)
                if deadline.cancel_called:
                    failure = "session_timeout"
            except* GatewayError as errors:
                failure = _code(errors)
            except* (anyio.BrokenResourceError, anyio.ClosedResourceError, OSError):
                failure = "device_disconnected"
        finally:
            if session is not None:
                logging.getLogger(__name__).info(
                    "realtime.session.summary microphone_bytes=%d speaker_bytes=%d responses=%d",
                    session.microphone_bytes,
                    session.speaker_bytes,
                    session.conversation.generation,
                )
                logging.getLogger(__name__).info(
                    "realtime.microphone.levels rms=%.2f peak=%d dc=%.2f clipped=%d samples=%d",
                    session.microphone_levels.rms,
                    session.microphone_levels.peak,
                    session.microphone_levels.mean,
                    session.microphone_levels.clipped,
                    session.microphone_levels.samples,
                )
            try:
                if session is None or not session.writing:
                    await self._finish(stream, session, failure)
            finally:
                with anyio.move_on_after(2, shield=True):
                    await stream.aclose()
                if admitted:
                    self._owner.release()

    async def _finish(self, stream: ByteStream, session: Session | None, failure: ErrorCode | None) -> None:
        controls: list[bytes] = []
        if session is not None and session.conversation.device_generation:
            controls.append(
                ServerControl(type="interrupt").frame(session.conversation.device_generation).encode()
            )
        if failure is not None:
            # Only the enum is logged: no exception repr, remote body, device ID, or transcript.
            logging.getLogger(__name__).warning("realtime.session.closed code=%s", failure)
            controls.append(ServerControl(type="error", code=failure).frame().encode())
        controls.extend(
            [
                ServerControl(type="state", state="stopped").frame().encode(),
                ServerControl(type="closed").frame().encode(),
            ]
        )
        try:
            with anyio.move_on_after(1, shield=True):
                await stream.send(b"".join(controls))
        except (anyio.BrokenResourceError, anyio.ClosedResourceError, OSError):
            return  # Peer already gone; outer finally still closes and releases ownership.


async def serve(settings: Settings) -> None:
    """Listen only when explicitly invoked; never reconnect a paid session."""
    gateway = Gateway(settings)
    async with await anyio.create_tcp_listener(
        local_host=settings.host, local_port=settings.port
    ) as listener:
        if settings.interface_index is not None:
            for bound in listener.listeners:
                sock = bound.extra(SocketAttribute.raw_socket)  # noqa: S610 - AnyIO socket metadata, not SQL.
                pin_outgoing_interface(sock, settings.interface_index)
        await listener.serve(gateway.handle)
