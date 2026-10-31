use super::{DeviceError, DeviceInfo, DeviceLink, Reading};
use std::time::{SystemTime, UNIX_EPOCH};

/// 无硬件时使用的模拟设备。所有读数显式标记 source=mock，
/// 输出 0.3–1.8 ppm 的伪随机值（对齐设计稿的演示范围，非医学参考区间），仅用于联调，绝不代表实测。
pub struct MockDevice {
    connected: bool,
    seed: u64,
}

impl MockDevice {
    pub fn new() -> Self {
        let seed = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_nanos() as u64 | 1)
            .unwrap_or(1);
        Self { connected: true, seed }
    }

    fn next(&mut self) -> f64 {
        self.seed ^= self.seed << 13;
        self.seed ^= self.seed >> 7;
        self.seed ^= self.seed << 17;
        (self.seed % 10_000) as f64 / 10_000.0
    }
}

impl DeviceLink for MockDevice {
    fn info(&self) -> DeviceInfo {
        DeviceInfo {
            name: "灵息 BREATHON 智能挂坠".into(),
            connected: self.connected,
            battery: 86,
            firmware: "mock-0.1".into(),
            sensor_ready: false,
            transport: "mock".into(),
        }
    }

    fn connect(&mut self) -> Result<(), DeviceError> {
        self.connected = true;
        Ok(())
    }

    fn calibrate(&mut self) -> Result<(), DeviceError> {
        if self.connected { Ok(()) } else { Err(DeviceError::NotConnected) }
    }

    fn sample(&mut self) -> Result<Reading, DeviceError> {
        if !self.connected {
            return Err(DeviceError::NotConnected);
        }
        let v = 0.3 + self.next() * 1.5;
        Ok(Reading {
            acetone_ppm: (v * 100.0).round() / 100.0,
            source: "mock".into(),
            quality: "unknown".into(),
            calibration: "uncalibrated".into(),
            sensor_model: None,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sample_in_range_and_marked_mock() {
        let mut d = MockDevice::new();
        for _ in 0..100 {
            let r = d.sample().unwrap();
            assert!((0.3..=1.8).contains(&r.acetone_ppm));
            assert_eq!(r.source, "mock");
            assert_eq!(r.calibration, "uncalibrated");
        }
    }

    #[test]
    fn mock_never_reports_sensor_ready() {
        assert!(!MockDevice::new().info().sensor_ready);
    }
}
