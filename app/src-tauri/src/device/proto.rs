//! BREATHON BLE 协议 v1，与固件 `firmware/breathon_ble_proto.h` 逐字节一致。
//!
//! 命令（App → 板子）：`[MAGIC][cmd]`
//! READING（板子 → App）：`[MAGIC][0x01][seq][ppm_x100 lo][ppm_x100 hi][battery][state][flags][progress]`
//! ACK（板子 → App）：`[MAGIC][0x02][seq][cmd][result]`

pub const DEVICE_NAME: &str = "BREATHON";
/// TuyaOpen 通用服务 V2（16 位 0xFD50）。
pub const SERVICE_UUID: &str = "0000fd50-0000-1000-8000-00805f9b34fb";
pub const WRITE_UUID: &str = "00000001-0000-1001-8001-00805f9b07d0";
pub const NOTIFY_UUID: &str = "00000002-0000-1001-8001-00805f9b07d0";

pub const MAGIC: u8 = 0xB7;
const FRAME_READING: u8 = 0x01;
const FRAME_ACK: u8 = 0x02;
const FLAG_MOCK: u8 = 0x01;
const FLAG_FINAL: u8 = 0x02;
const BATTERY_UNKNOWN: u8 = 0xFF;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum Cmd {
    Start = 0x10,
    Stop = 0x11,
    Calibrate = 0x12,
    Status = 0x13,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum State {
    Idle,
    Sampling,
    Calibrating,
    Done,
    Unknown(u8),
}

impl From<u8> for State {
    fn from(v: u8) -> Self {
        match v {
            0 => State::Idle,
            1 => State::Sampling,
            2 => State::Calibrating,
            3 => State::Done,
            x => State::Unknown(x),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AckResult {
    Ok,
    Busy,
    Bad,
    Other(u8),
}

#[derive(Debug, Clone, PartialEq)]
pub enum Frame {
    Reading {
        seq: u8,
        ppm: f64,
        /// None = 板子没有电量计
        battery: Option<u8>,
        state: State,
        mock: bool,
        is_final: bool,
        progress: u8,
    },
    Ack {
        seq: u8,
        cmd: u8,
        result: AckResult,
    },
}

pub fn command(cmd: Cmd) -> [u8; 2] {
    [MAGIC, cmd as u8]
}

/// 解析一帧通知。长度、魔数、类型任何一项不符都返回 None。
pub fn parse(data: &[u8]) -> Option<Frame> {
    match data {
        [MAGIC, FRAME_READING, seq, lo, hi, battery, state, flags, progress] => Some(Frame::Reading {
            seq: *seq,
            ppm: f64::from(u16::from_le_bytes([*lo, *hi])) / 100.0,
            battery: (*battery != BATTERY_UNKNOWN).then_some((*battery).min(100)),
            state: State::from(*state),
            mock: flags & FLAG_MOCK != 0,
            is_final: flags & FLAG_FINAL != 0,
            progress: (*progress).min(100),
        }),
        [MAGIC, FRAME_ACK, seq, cmd, result] => Some(Frame::Ack {
            seq: *seq,
            cmd: *cmd,
            result: match result {
                0 => AckResult::Ok,
                1 => AckResult::Busy,
                2 => AckResult::Bad,
                x => AckResult::Other(*x),
            },
        }),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn commands_match_firmware() {
        assert_eq!(command(Cmd::Start), [0xB7, 0x10]);
        assert_eq!(command(Cmd::Stop), [0xB7, 0x11]);
        assert_eq!(command(Cmd::Calibrate), [0xB7, 0x12]);
        assert_eq!(command(Cmd::Status), [0xB7, 0x13]);
    }

    #[test]
    fn parses_final_mock_reading() {
        // 1.23 ppm = 123 = 0x007B，电量未知，DONE，mock|final，100%
        let f = parse(&[0xB7, 0x01, 7, 0x7B, 0x00, 0xFF, 3, 0x03, 100]).unwrap();
        assert_eq!(
            f,
            Frame::Reading {
                seq: 7,
                ppm: 1.23,
                battery: None,
                state: State::Done,
                mock: true,
                is_final: true,
                progress: 100
            }
        );
    }

    #[test]
    fn parses_ack_and_clamps_garbage() {
        assert_eq!(
            parse(&[0xB7, 0x02, 1, 0x10, 1]),
            Some(Frame::Ack { seq: 1, cmd: 0x10, result: AckResult::Busy })
        );
        match parse(&[0xB7, 0x01, 0, 0, 0, 180, 9, 0, 250]).unwrap() {
            Frame::Reading { battery, state, progress, mock, .. } => {
                assert_eq!(battery, Some(100));
                assert_eq!(state, State::Unknown(9));
                assert_eq!(progress, 100);
                assert!(!mock);
            }
            other => panic!("unexpected {other:?}"),
        }
    }

    #[test]
    fn rejects_bad_frames() {
        assert_eq!(parse(&[]), None);
        assert_eq!(parse(&[0x00, 0x01, 0, 0, 0, 0, 0, 0, 0]), None); // 魔数不对
        assert_eq!(parse(&[0xB7, 0x01, 0, 0]), None); // 长度不对
        assert_eq!(parse(&[0xB7, 0x09, 0, 0, 0]), None); // 未知类型
    }
}
