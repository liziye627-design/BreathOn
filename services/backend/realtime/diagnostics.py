"""Enumerated failure diagnostics; arbitrary upstream strings never reach logs."""

import logging
from typing import TYPE_CHECKING, Final

if TYPE_CHECKING:
    from .events import ResponseInfo

_TYPES: Final = frozenset(["completed", "cancelled", "incomplete", "failed"])
_REASONS: Final = frozenset(["turn_detected", "client_cancelled", "max_output_tokens", "content_filter"])
_ERROR_CODES: Final = frozenset(
    [
        "server_error",
        "rate_limit_exceeded",
        "insufficient_quota",
        "invalid_request_error",
        "invalid_api_key",
        "authentication_error",
        "permission_denied",
        "context_length_exceeded",
        "content_filter",
        "audio_unintelligible",
    ]
)


def _label(value: str | None, allowed: frozenset[str]) -> str:
    if value is None:
        return "none"
    return value if value in allowed else "unknown"


def log_response_failure(response: "ResponseInfo", generation: int) -> None:
    """Log only enum labels and a local generation before the existing failure path."""
    details = response.status_details
    kind = _label(details.type if details is not None else None, _TYPES)
    reason = _label(details.reason if details is not None else None, _REASONS)
    error = details.error if details is not None else None
    code = _label(error.code if error is not None else None, _ERROR_CODES)
    logging.getLogger(__name__).warning(
        "realtime.response.failed generation=%d status=%s status_details.type=%s "
        "status_details.reason=%s status_details.error.code=%s",
        generation,
        response.status,
        kind,
        reason,
        code,
    )
