//! 设备传输抽象。真实 BLE 实现接入后，只需新增一个实现 `DeviceLink` 的类型。
mod mock;
pub mod proto;

pub use mock::MockDevice;
use serde::Serialize;

#[derive(Debug, Clone, Serialize)]
pub struct DeviceInfo {
    pub name: String,
    pub connected: bool,
    pub battery: u8,
    pub firmware: String,
    /// 气体传感器是否已接入固件。未接入时 App 不得显示"已就绪"。
    pub sensor_ready: bool,
    /// 数据通道：mock / ble
    pub transport: String,
}

/// 一次采样的结果。字段口径与 AeroMet `Metric` 对齐，浓度单位固定 ppm。
#[derive(Debug, Clone, Serialize)]
pub struct Reading {
    pub acetone_ppm: f64,
    pub source: String,
    pub quality: String,
    pub calibration: String,
    pub sensor_model: Option<String>,
}

#[derive(Debug, thiserror::Error)]
pub enum DeviceError {
    #[error("设备未连接")]
    NotConnected,
}

pub trait DeviceLink: Send {
    fn info(&self) -> DeviceInfo;
    fn connect(&mut self) -> Result<(), DeviceError>;
    fn calibrate(&mut self) -> Result<(), DeviceError>;
    fn sample(&mut self) -> Result<Reading, DeviceError>;
}
