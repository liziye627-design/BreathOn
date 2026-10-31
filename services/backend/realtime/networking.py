"""Pin an explicitly selected Windows LAN socket without changing OS routes."""

import socket
import struct
import sys
from typing import Final

from .errors import GatewayError

# Winsock IPPROTO_IP IP_UNICAST_IF; not exported by CPython 3.11 on Windows.
# https://learn.microsoft.com/windows/win32/winsock/ipproto-ip-socket-options
_IP_UNICAST_IF: Final = 31


def pin_outgoing_interface(sock: socket.socket, index: int) -> None:
    """Use network byte order as required by Winsock for this socket option."""
    if sys.platform != "win32":
        raise GatewayError("invalid_config")
    sock.setsockopt(socket.IPPROTO_IP, _IP_UNICAST_IF, struct.pack("!I", index))
