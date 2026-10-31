"""Use the real Windows socket option; no administrator changes or API calls."""

import socket
import sys

import pytest

from backend.realtime.networking import pin_outgoing_interface


@pytest.mark.skipif(sys.platform != "win32", reason="Windows routing option")
def test_listener_keeps_explicit_outgoing_interface() -> None:
    index = next(index for index, name in socket.if_nameindex() if "loopback" in name.casefold())
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        pin_outgoing_interface(listener, index)
        assert listener.getsockopt(socket.IPPROTO_IP, 31) == index
