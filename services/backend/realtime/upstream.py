"""One TLS-verified official WSS connection, with no redirects or reconnects."""

import logging
import ssl
from collections.abc import AsyncGenerator
from contextlib import AbstractAsyncContextManager, asynccontextmanager
from typing import Protocol

import anyio
from typing_extensions import override
from websockets.asyncio.client import connect
from websockets.exceptions import WebSocketException

from .config import ENDPOINT, Settings
from .errors import GatewayError


class Upstream(Protocol):
    async def send(self, message: str) -> None: ...
    async def recv(self) -> str | bytes: ...


class Connector(Protocol):
    def __call__(self, settings: Settings) -> AbstractAsyncContextManager[Upstream]: ...


class NoRedirectConnect(connect):
    """Returning the original exception aborts before any redirected handshake."""

    @override
    def process_redirect(self, exc: Exception) -> Exception:
        return exc


@asynccontextmanager
async def open_upstream(settings: Settings) -> AsyncGenerator[Upstream, None]:
    """Open and close one official session without automatic retries."""
    # Suppress third-party wire logging even if the application's root level is DEBUG.
    wire_logger = logging.Logger("breathon.realtime.wire", level=logging.CRITICAL + 1)
    wire_logger.addHandler(logging.NullHandler())
    wire_logger.propagate = False
    try:
        async with NoRedirectConnect(
            ENDPOINT,
            additional_headers={"Authorization": f"Bearer {settings.api_key.get_secret_value()}"},
            ssl=ssl.create_default_context(),
            proxy=None,
            compression=None,
            max_size=256 * 1024,
            max_queue=1,
            write_limit=4096,
            open_timeout=settings.upstream_timeout,
            close_timeout=1,
            logger=wire_logger,
        ) as websocket:
            try:
                yield websocket
            finally:
                with anyio.move_on_after(2, shield=True):
                    await websocket.close()
    except (WebSocketException, OSError, TimeoutError):
        raise GatewayError("upstream_unavailable") from None
