"""Official SDK typed client events serialized by the WebSocket transport."""

import base64
import json
from typing import TYPE_CHECKING, Final

if TYPE_CHECKING:
    from openai.types.realtime import (
        ConversationItemTruncateEventParam,
        InputAudioBufferAppendEventParam,
        ResponseCreateEventParam,
        SessionUpdateEventParam,
    )

from .config import MODEL, VOICE

INSTRUCTIONS: Final = (
    "你是灵息，使用中文和温柔自然的女声陪伴用户。每次回复1到3句短句，避免长篇解释。"
    "可在相关语境中提醒用户遵循其已明确说明的医嘱或用药计划；不要自行提出药名、剂量或修改方案。"
    "不根据模糊咳嗽声音推断诊断。运动陪伴要鼓励适度、量力而行，不适时停止。"
    "仅在当前对话中提供提醒，没有持久化定时调度；不能声称已设置闹钟或到点一定响。"
    "不在每次回复重复服药或运动提醒，只在用户语境相关时提及。没有工具，也不能发送外部消息。"
)


def configure() -> str:
    """Request the official PCM/VAD session, with no tools."""
    event: SessionUpdateEventParam = {
        "type": "session.update",
        "session": {
            "type": "realtime",
            "model": MODEL,
            "output_modalities": ["audio"],
            "instructions": INSTRUCTIONS,
            "max_output_tokens": 1024,
            "tools": [],
            "tool_choice": "none",
            "audio": {
                "input": {
                    "format": {"type": "audio/pcm", "rate": 24000},
                    "transcription": {"model": "gpt-4o-mini-transcribe", "language": "zh"},
                    "turn_detection": {
                        "type": "server_vad",
                        "interrupt_response": True,
                        "create_response": True,
                        "silence_duration_ms": 500,
                    },
                },
                "output": {"format": {"type": "audio/pcm", "rate": 24000}, "voice": VOICE},
            },
        },
    }
    return json.dumps(event, ensure_ascii=False)


def greeting() -> str:
    """Create the single short greeting after the session is acknowledged."""
    event: ResponseCreateEventParam = {
        "type": "response.create",
        "response": {
            "output_modalities": ["audio"],
            "max_output_tokens": 160,
            "instructions": "用温柔中文只说一句简短问候：你好，我是灵息，想和我聊聊吗。",
        },
    }
    return json.dumps(event, ensure_ascii=False)


def append_audio(pcm: bytes) -> str:
    """Encode 24 kHz input audio for the upstream transport."""
    event: InputAudioBufferAppendEventParam = {
        "type": "input_audio_buffer.append",
        "audio": base64.b64encode(pcm).decode("ascii"),
    }
    return json.dumps(event)


def truncate(item_id: str, content_index: int) -> str:
    """Remove unverified playback from conversation history conservatively."""
    event: ConversationItemTruncateEventParam = {
        "type": "conversation.item.truncate",
        "item_id": item_id,
        "content_index": content_index,
        "audio_end_ms": 0,
    }
    return json.dumps(event)
