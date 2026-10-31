"""Failure diagnostics retain machine reasons while excluding arbitrary remote text."""

import logging

import pytest
from test_realtime_session import event

from backend.realtime.conversation import Conversation
from backend.realtime.errors import GatewayError


@pytest.mark.parametrize(
    ("status", "details", "expected"),
    [
        (
            "incomplete",
            '{"type":"incomplete","reason":"max_output_tokens"}',
            (
                "status=incomplete status_details.type=incomplete "
                "status_details.reason=max_output_tokens status_details.error.code=none"
            ),
        ),
        (
            "incomplete",
            '{"type":"incomplete","reason":"content_filter"}',
            (
                "status=incomplete status_details.type=incomplete "
                "status_details.reason=content_filter status_details.error.code=none"
            ),
        ),
        (
            "failed",
            '{"type":"failed","error":{"code":"server_error","message":"PRIVATE_BODY"}}',
            (
                "status=failed status_details.type=failed "
                "status_details.reason=none status_details.error.code=server_error"
            ),
        ),
        (
            "failed",
            '{"type":"failed","error":{"code":"rate_limit_exceeded"}}',
            (
                "status=failed status_details.type=failed "
                "status_details.reason=none status_details.error.code=rate_limit_exceeded"
            ),
        ),
        (
            "failed",
            "null",
            (
                "status=failed status_details.type=none "
                "status_details.reason=none status_details.error.code=none"
            ),
        ),
    ],
)
def test_diagnostic_when_response_fails(
    caplog: pytest.LogCaptureFixture, status: str, details: str, expected: str
) -> None:
    # Given an active response, and a remote terminal failure.
    conversation = Conversation()
    event(conversation, "session.updated")
    event(conversation, "response.created")
    raw = (
        '{"type":"response.done","response":{"id":"r1","status":"'
        + status
        + '","status_details":'
        + details
        + "}}"
    )
    # When the existing failure path runs, it must still raise response_failed.
    with caplog.at_level(logging.WARNING), pytest.raises(GatewayError, match="response_failed"):
        conversation.handle_event(raw)
    # Then machine-only detail fields and a local generation identify the cause.
    messages = [record.getMessage() for record in caplog.records]
    assert len(messages) == 1
    assert "generation=1 " + expected in messages[0]
    assert "PRIVATE_BODY" not in caplog.text


@pytest.mark.parametrize(
    "untrusted", ["sk_fake_private_key", "PRIVATE_TRANSCRIPT", "evil\\nstatus=completed", "x" * 1000]
)
def test_diagnostic_when_detail_contains_untrusted_text(
    caplog: pytest.LogCaptureFixture, untrusted: str
) -> None:
    # Given arbitrary strings in each otherwise machine-readable detail field.
    conversation = Conversation()
    event(conversation, "session.updated")
    event(conversation, "response.created")
    raw = (
        '{"type":"response.done","response":{"id":"r1","status":"failed",'
        '"status_details":{"type":"'
        + untrusted
        + '","reason":"'
        + untrusted
        + '","error":{"code":"'
        + untrusted
        + '","message":"PRIVATE_BODY"}}}}'
    )
    # When / Then failure remains failure and only fixed unknown labels are logged.
    with caplog.at_level(logging.WARNING), pytest.raises(GatewayError, match="response_failed"):
        conversation.handle_event(raw)
    assert (
        "status_details.type=unknown status_details.reason=unknown status_details.error.code=unknown"
        in caplog.text
    )
    assert untrusted not in caplog.text
    assert "PRIVATE_BODY" not in caplog.text
