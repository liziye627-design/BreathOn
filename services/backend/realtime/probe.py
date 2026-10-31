"""Bounded official Realtime probe; never print raw API errors or credentials."""

from __future__ import annotations

import base64
import binascii
import json
import os
import time
import wave
from pathlib import Path

from pydantic import BaseModel, ConfigDict, ValidationError

# One connection attempt: redirects never cause a second authenticated request.
os.environ["WEBSOCKETS_MAX_REDIRECTS"] = "1"
from websockets.exceptions import ConnectionClosed, InvalidHandshake, InvalidStatus
from websockets.sync.client import connect


class ProbeError(BaseModel):
    code: str | None = None
    type: str | None = None


class Response(BaseModel):
    status: str = ""


class Event(BaseModel):
    model_config = ConfigDict(extra="ignore")
    type: str
    delta: str = ""
    error: ProbeError | None = None
    response: Response | None = None


def main() -> int:
    """Request one short spoken greeting, with a maximum 40-second session."""
    key = os.environ.get("OPENAI_API_KEY")
    if not key:
        print('{"ok":false,"code":"missing_key"}')
        return 2
    model = os.environ.get("BREATHON_REALTIME_MODEL", "gpt-realtime-2.1")
    if model not in {"gpt-realtime-2.1", "gpt-realtime"}:
        print('{"ok":false,"code":"unsupported_probe_model"}')
        return 2
    voice = "marin"
    audio = bytearray()
    counts: dict[str, int] = {}
    started = time.monotonic()
    configured = False
    completed = False
    try:
        with connect(
            f"wss://api.openai.com/v1/realtime?model={model}",
            additional_headers={"Authorization": f"Bearer {key}"},
            open_timeout=15,
            close_timeout=3,
            compression=None,
            max_size=2**20,
            max_queue=16,
        ) as connection:
            while time.monotonic() - started < 40:
                event = Event.model_validate_json(connection.recv(timeout=15))
                counts[event.type] = counts.get(event.type, 0) + 1
                match event.type:
                    case "session.created":
                        connection.send(
                            json.dumps(
                                {
                                    "type": "session.update",
                                    "session": {
                                        "type": "realtime",
                                        "output_modalities": ["audio"],
                                        "instructions": "你是灵息，使用自然温柔的中文短句。",
                                        "audio": {
                                            "input": {
                                                "format": {"type": "audio/pcm", "rate": 24000},
                                                "turn_detection": None,
                                            },
                                            "output": {
                                                "format": {"type": "audio/pcm", "rate": 24000},
                                                "voice": voice,
                                            },
                                        },
                                    },
                                }
                            )
                        )
                    case "session.updated":
                        configured = True
                        connection.send(
                            json.dumps(
                                {
                                    "type": "response.create",
                                    "response": {
                                        "output_modalities": ["audio"],
                                        "max_output_tokens": 160,
                                        "instructions": "请只说：你好，我是灵息，很高兴陪你聊天。",
                                    },
                                }
                            )
                        )
                    case "response.output_audio.delta":
                        audio.extend(base64.b64decode(event.delta, validate=True))
                        if len(audio) > 1_440_000:
                            print('{"ok":false,"code":"audio_limit"}')
                            return 2
                    case "response.done":
                        if event.response is None or event.response.status != "completed":
                            print(
                                json.dumps(
                                    {
                                        "ok": False,
                                        "code": "response_not_completed",
                                        "status": event.response.status if event.response else "missing",
                                    }
                                )
                            )
                            return 2
                        completed = True
                        break
                    case "error":
                        print(
                            json.dumps(
                                {
                                    "ok": False,
                                    "code": event.error.code if event.error else "api_error",
                                    "category": event.error.type if event.error else None,
                                }
                            )
                        )
                        return 2
                    case _:
                        # The public event stream has additional lifecycle events.
                        continue
    except InvalidStatus as error:
        print(
            json.dumps({"ok": False, "code": "handshake_rejected", "http_status": error.response.status_code})
        )
        return 2
    except (
        InvalidHandshake,
        ConnectionClosed,
        TimeoutError,
        OSError,
        ValidationError,
        binascii.Error,
    ) as error:
        print(json.dumps({"ok": False, "code": type(error).__name__}))
        return 2
    if not configured or not completed or not audio:
        print('{"ok":false,"code":"no_audio"}')
        return 2
    output = Path(__file__).resolve().parents[2] / "evidence" / "realtime-api-greeting.wav"
    output.parent.mkdir(exist_ok=True)
    with wave.open(str(output), "wb") as recording:
        recording.setnchannels(1)
        recording.setsampwidth(2)
        recording.setframerate(24000)
        recording.writeframes(audio)
    result = {
        "ok": True,
        "model": model,
        "voice": voice,
        "pcm_bytes": len(audio),
        "seconds": len(audio) / 48000,
        "elapsed_seconds": round(time.monotonic() - started, 2),
        "events": counts,
        "acoustic_test": False,
    }
    report = output.with_suffix(".json")
    report.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
