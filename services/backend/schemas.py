"""AeroMet v0.7 API contract: sample values always have per-metric provenance."""
from __future__ import annotations
from datetime import datetime, timezone
from enum import Enum
from typing import Any, Literal
from pydantic import BaseModel, Field, field_validator, model_validator

UNITS = {
    "acetone_ppm": ("ppm", 0, 300),
    "co2_pct": ("%", 0, 15),
    "o2_pct": ("%", 0, 25),
    "flow_lpm": ("L/min", -100, 100),
    "temperature_c": ("°C", -20, 90),
    "humidity_pct": ("%RH", 0, 100),
    "pressure_kpa": ("kPa", 50, 130),
}

class State(str, Enum):
    idle = "idle"
    warmup = "warmup"
    baseline = "baseline"
    ready = "ready"
    blowing = "blowing"
    measuring = "measuring"
    purging = "purging"
    completed = "completed"
    invalid = "invalid"
    error = "error"

class Metric(BaseModel):
    value: float | None = None
    unit: str
    source: Literal["mock", "sensor"]
    sensor_model: str | None = Field(default=None, max_length=100)
    calibration: Literal["uncalibrated", "verified", "not_applicable"] = "uncalibrated"
    quality: Literal["ok", "suspect", "invalid", "unknown"] = "unknown"

    @field_validator("value")
    @classmethod
    def finite_value(cls, v):
        import math
        if v is not None and not math.isfinite(v):
            raise ValueError("non-finite metric value")
        return v

class MeasurementIn(BaseModel):
    schema_version: Literal["1.0"] = "1.0"
    device_id: str = Field(min_length=1, max_length=64, pattern=r"^[A-Za-z0-9._-]+$")
    session_id: str | None = Field(default=None, max_length=64)
    captured_at: datetime = Field(default_factory=lambda: datetime.now(timezone.utc))
    state: State = State.measuring
    metrics: dict[str, Metric] = Field(default_factory=dict)
    quality_flags: list[str] = Field(default_factory=list, max_length=15)
    transport: Literal["mock_script", "wifi", "ble_gateway", "manual_test"] = "mock_script"

    @field_validator("captured_at")
    @classmethod
    def aware_time(cls, v):
        if v.tzinfo is None or v.utcoffset() is None:
            raise ValueError("captured_at must include timezone")
        return v

    @model_validator(mode="after")
    def validate_metrics(self):
        for name, metric in self.metrics.items():
            if name not in UNITS:
                raise ValueError(f"Unsupported metric: {name}")
            expected_unit, low, high = UNITS[name]
            if metric.unit != expected_unit:
                raise ValueError(f"Wrong unit for {name}: expected {expected_unit}")
            if metric.value is not None and not low <= metric.value <= high:
                raise ValueError(f"Value outside engineering guard range: {name}")
            if metric.source == "sensor" and not metric.sensor_model:
                raise ValueError(f"sensor_model required for sensor value: {name}")
            if metric.source == "mock" and metric.calibration == "verified":
                raise ValueError("Mock readings cannot be calibration verified")
        return self

class MeasurementOut(MeasurementIn):
    id: str
    recorded_at: datetime
    contains_mock: bool
    contains_sensor: bool

class DeviceIn(BaseModel):
    device_id: str = Field(min_length=1, max_length=64, pattern=r"^[A-Za-z0-9._-]+$")
    display_name: str = Field(default="呼呼鲸开发板", max_length=80)
    model: str = Field(default="NiceMCU-T5-2.8IPS", max_length=100)
    firmware_version: str = Field(default="unverified", max_length=60)
    hardware_status: Literal["planned", "unverified", "online", "offline"] = "unverified"

class SessionIn(BaseModel):
    device_id: str = Field(min_length=1, max_length=64)
    user_id: str = Field(default="demo-user", min_length=1, max_length=64)
    label: str = Field(default="呼气检测演示", max_length=100)
    mode: Literal["demo", "sensor"] = "demo"

class SessionPatch(BaseModel):
    status: Literal["in_progress", "completed", "invalid"]

class LogIn(BaseModel):
    user_id: str = Field(default="demo-user", max_length=64)
    kind: Literal["meal", "weight", "activity", "medication_note", "symptom_note", "sleep"]
    text: str = Field(default="", max_length=500)
    occurred_at: datetime = Field(default_factory=lambda: datetime.now(timezone.utc))
    payload: dict[str, Any] = Field(default_factory=dict)
    source: Literal["manual", "mock"] = "manual"
    image_id: str | None = Field(default=None, max_length=64)
    needs_confirmation: bool = True

    @field_validator("occurred_at")
    @classmethod
    def aware_time(cls, v):
        if v.tzinfo is None or v.utcoffset() is None:
            raise ValueError("occurred_at must include timezone")
        return v

class SeedIn(BaseModel):
    scenario: Literal["baseline", "trend", "mixed"] = "mixed"
    count: int = Field(default=28, ge=3, le=120)

class ReferenceItem(BaseModel):
    title: str
    url: str
    provider: str
    use_case: str
    integration: str
    observed_at: str | None = None
