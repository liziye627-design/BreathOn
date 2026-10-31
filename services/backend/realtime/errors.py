"""Stable public failures; never carry remote bodies or secrets."""

from typing import Literal

ErrorCode = Literal[
    "invalid_frame",
    "invalid_control",
    "auth_failed",
    "credentials_missing",
    "invalid_config",
    "busy",
    "hello_timeout",
    "session_timeout",
    "upstream_timeout",
    "upstream_unavailable",
    "upstream_protocol",
    "upstream_error",
    "response_failed",
    "media_overflow",
    "control_overflow",
    "device_disconnected",
    "write_timeout",
]


class GatewayError(Exception):
    """A sanitized failure safe to send to the device and operator."""

    def __init__(self, code: ErrorCode) -> None:
        self.code: ErrorCode = code
        super().__init__(code)
