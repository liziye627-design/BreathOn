"""Independent device/upstream pumps in a structured AnyIO task group."""

import logging
from typing import assert_never, final

import anyio
from anyio.abc import ByteStream
from anyio.streams.buffered import BufferedByteReceiveStream
from websockets.exceptions import WebSocketException

from . import commands
from .audio import PCMLevels, PCMResampler
from .config import Settings
from .conversation import Conversation
from .errors import GatewayError
from .upstream import Connector, Upstream
from .wire import Frame, Hello, Interrupted, Kind, Ping, ServerControl, Stop, parse_control, read_frame


@final
class Session:
    """Own cancellable pumps; the conversation maps events without network awaits."""

    def __init__(self, settings: Settings) -> None:
        self.settings = settings
        self.conversation = Conversation()
        self.stopped = anyio.Event()
        self.writing = False
        self.microphone_bytes = 0
        self.speaker_bytes = 0
        self.microphone_levels = PCMLevels()
        self._microphone = PCMResampler(16000, 24000)

    async def _device_read(self, reader: BufferedByteReceiveStream) -> None:
        while True:
            frame = await read_frame(reader)
            match frame.kind:
                case Kind.MICROPHONE:
                    if not self.conversation.ready.is_set():
                        raise GatewayError("invalid_frame")
                    self.microphone_bytes += len(frame.payload)
                    self.microphone_levels.feed(frame.payload)
                    pcm = self._microphone.feed(frame.payload)
                    if pcm:
                        self.conversation.input.media(commands.append_audio(pcm), size=len(pcm), generation=0)
                case Kind.CONTROL:
                    self._control(frame)
                case Kind.SPEAKER:
                    raise GatewayError("invalid_frame")
                case unreachable:
                    assert_never(unreachable)

    def _control(self, frame: Frame) -> None:
        match parse_control(frame):
            case Stop():
                self.stopped.set()
            case Ping():
                self.conversation.output.control(ServerControl(type="pong").frame())
            case Interrupted():
                turn = self.conversation.turn
                if turn is None or frame.generation != turn.generation or not turn.interrupted:
                    raise GatewayError("invalid_control")
            case Hello():
                raise GatewayError("invalid_control")
            case unreachable:
                assert_never(unreachable)

    async def _device_write(self, stream: ByteStream) -> None:
        while True:
            item = await self.conversation.output.take()
            try:
                with anyio.fail_after(2):
                    if item.value.kind == Kind.CONTROL:
                        control = ServerControl.model_validate_json(item.value.payload)
                        logging.getLogger(__name__).debug(
                            "realtime.device.control type=%s generation=%d",
                            control.type,
                            item.value.generation,
                        )
                        if control.type == "audio_start":
                            self.conversation.audio_starting(item.value.generation)
                    self.writing = True
                    await stream.send(item.value.encode())
                    self.writing = False
                    match item.value.kind:
                        case Kind.SPEAKER:
                            self.speaker_bytes += len(item.value.payload)
                        case Kind.CONTROL:
                            pass
                        case Kind.MICROPHONE:
                            raise GatewayError("invalid_frame")
                        case unreachable:
                            assert_never(unreachable)
            finally:
                self.conversation.output.task_done(item)

    async def _upstream_write(self, upstream: Upstream) -> None:
        while True:
            item = await self.conversation.input.take()
            try:
                with anyio.fail_after(2):
                    await upstream.send(item.value)
            finally:
                self.conversation.input.task_done(item)

    async def _upstream_read(self, upstream: Upstream) -> None:
        while True:
            self.conversation.handle_event(await upstream.recv())

    async def _connected(self, connector: Connector) -> None:
        async with connector(self.settings) as upstream:
            with anyio.fail_after(self.settings.upstream_timeout):
                await upstream.send(commands.configure())
            async with anyio.create_task_group() as group:
                group.start_soon(self._upstream_read, upstream)
                group.start_soon(self._upstream_write, upstream)
                with anyio.fail_after(self.settings.upstream_timeout):
                    await self.conversation.ready.wait()
                await anyio.sleep_forever()

    async def run(self, stream: ByteStream, reader: BufferedByteReceiveStream, connector: Connector) -> None:
        try:
            async with anyio.create_task_group() as group:
                group.start_soon(self._device_read, reader)
                group.start_soon(self._device_write, stream)
                group.start_soon(self._connected, connector)
                await self.stopped.wait()
                group.cancel_scope.cancel()
        except* TimeoutError:
            raise GatewayError("upstream_timeout") from None
        except* WebSocketException:
            raise GatewayError("upstream_unavailable") from None
        finally:
            self.conversation.input.clear()
            self.conversation.output.clear()
