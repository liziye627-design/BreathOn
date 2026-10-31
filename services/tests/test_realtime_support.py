"""Disposable loopback harness; never calls the official connector."""

import json
from collections.abc import AsyncGenerator
from contextlib import asynccontextmanager
from dataclasses import dataclass
from typing import final

import anyio
from anyio.abc import SocketAttribute, SocketStream
from anyio.streams.buffered import BufferedByteReceiveStream
from pydantic import SecretStr

from backend.realtime.config import Settings
from backend.realtime.gateway import Gateway
from backend.realtime.upstream import Upstream
from backend.realtime.wire import Frame, Kind, ServerControl, read_frame


@final
class FakeUpstream:
    """A bounded channel peer with observable connection and release events."""

    def __init__(self) -> None:
        self.events, self._events = anyio.create_memory_object_stream[str](128)
        self._sent, self.sent = anyio.create_memory_object_stream[str](128)
        self.opened = anyio.Event()
        self.closed = anyio.Event()
        self.connections = 0
        self.block_connect = False

    async def send(self, message: str) -> None:
        await self._sent.send(message)

    async def recv(self) -> str:
        return await self._events.receive()

    @asynccontextmanager
    async def connect(self, settings: Settings) -> AsyncGenerator[Upstream, None]:
        assert settings.api_key.get_secret_value() == "fake-key"
        self.connections += 1
        self.opened.set()
        try:
            if self.block_connect:
                await anyio.sleep_forever()
            yield self
        finally:
            self.closed.set()


def fake_settings(
    *, hello_timeout: float = 5, max_session_seconds: float = 180, upstream_timeout: float = 15
) -> Settings:
    return Settings(
        api_key=SecretStr("fake-key"),
        device_token=SecretStr("local-pairing"),
        hello_timeout=hello_timeout,
        max_session_seconds=max_session_seconds,
        upstream_timeout=upstream_timeout,
    )


def hello(token: str = "local-pairing") -> bytes:
    return Frame(
        Kind.CONTROL,
        json.dumps(
            {"type": "hello", "token": token, "device_id": "test-board", "sample_rate": 16000}
        ).encode(),
    ).encode()


async def next_control(reader: BufferedByteReceiveStream, kind: str) -> ServerControl:
    with anyio.fail_after(3):
        while True:
            frame = await read_frame(reader)
            if frame.kind == Kind.CONTROL:
                control = ServerControl.model_validate_json(frame.payload)
                if control.type == kind:
                    return control


@dataclass(frozen=True, slots=True)
class Device:
    stream: SocketStream
    reader: BufferedByteReceiveStream
    port: int
    upstream: FakeUpstream

    async def start(self) -> None:
        await self.stream.send(hello())
        await self.upstream.opened.wait()
        await self.upstream.sent.receive()  # Session configuration.
        await self.upstream.events.send('{"type":"session.updated"}')
        await next_control(self.reader, "ready")
        await self.upstream.sent.receive()  # One greeting.


@asynccontextmanager
async def device(upstream: FakeUpstream, settings: Settings | None = None) -> AsyncGenerator[Device, None]:
    gateway = Gateway(settings or fake_settings(), upstream.connect)
    async with await anyio.create_tcp_listener(local_host="127.0.0.1", local_port=0) as listener:
        port = listener.extra(SocketAttribute.local_port)
        async with anyio.create_task_group() as group:
            group.start_soon(listener.serve, gateway.handle)
            async with await anyio.connect_tcp("127.0.0.1", port) as stream:
                yield Device(stream, BufferedByteReceiveStream(stream), port, upstream)
            group.cancel_scope.cancel()
