//! 本地 SQLite：测量（带来源/质量/校准）、用户记录、授权留痕、设置。
//! 字段口径对齐 AeroMet V0.7：`source` 只允许 mock/sensor，单位固定 ppm。
use chrono::{Duration, Local, TimeZone};
use rusqlite::{params, Connection};
use serde::{Deserialize, Serialize};

pub type DbResult<T> = Result<T, rusqlite::Error>;
pub const CONSENT_VERSION: &str = "1";

/// 待写入的测量。校验规则与 AeroMet `MeasurementIn` 一致。
#[derive(Debug, Clone, Deserialize)]
pub struct NewMeasurement {
    pub acetone_ppm: f64,
    pub source: String,
    pub quality: String,
    pub calibration: String,
    pub sensor_model: Option<String>,
}

impl NewMeasurement {
    pub fn validate(&self) -> Result<(), String> {
        if !self.acetone_ppm.is_finite() || !(0.0..=300.0).contains(&self.acetone_ppm) {
            return Err("丙酮读数超出工程保护范围 0–300 ppm".into());
        }
        if !matches!(self.source.as_str(), "mock" | "sensor") {
            return Err("source 只能是 mock 或 sensor".into());
        }
        if !matches!(self.quality.as_str(), "ok" | "suspect" | "invalid" | "unknown") {
            return Err("quality 取值无效".into());
        }
        if !matches!(self.calibration.as_str(), "uncalibrated" | "verified" | "not_applicable") {
            return Err("calibration 取值无效".into());
        }
        if self.source == "sensor" && self.sensor_model.as_deref().unwrap_or("").is_empty() {
            return Err("传感器读数必须带 sensor_model".into());
        }
        if self.source == "mock" && self.calibration == "verified" {
            return Err("模拟读数不能标记为已校准".into());
        }
        Ok(())
    }
}

#[derive(Debug, Clone, Serialize)]
pub struct Measurement {
    pub id: i64,
    pub ts: i64,
    pub acetone_ppm: f64,
    pub source: String,
    pub quality: String,
    pub calibration: String,
    pub sensor_model: Option<String>,
    pub consent_sync: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct UserLog {
    #[serde(default)]
    pub id: i64,
    #[serde(default)]
    pub ts: i64,
    pub kind: String,
    pub value: Option<f64>,
    pub unit: Option<String>,
    #[serde(default)]
    pub text: String,
}

impl UserLog {
    pub fn validate(&self) -> Result<(), String> {
        let range = match self.kind.as_str() {
            "weight" => Some((20.0, 300.0)),
            "activity" => Some((1.0, 600.0)),
            "water" | "meal" => Some((1.0, 20.0)),
            "medication_note" => None,
            "sleep" => Some((0.5, 24.0)),
            _ => return Err("不支持的记录类型".into()),
        };
        if self.text.chars().count() > 500 {
            return Err("备注最多 500 字".into());
        }
        match (range, self.value) {
            (Some((lo, hi)), Some(v)) if v.is_finite() && (lo..=hi).contains(&v) => Ok(()),
            (Some(_), _) => Err("记录数值缺失或超出合理范围".into()),
            (None, _) if self.text.trim().is_empty() => Err("用药备注不能为空".into()),
            (None, _) => Ok(()),
        }
    }
}

#[derive(Debug, Clone, Serialize)]
pub struct Consent {
    pub id: i64,
    pub ts: i64,
    pub scope: String,
    pub granted: bool,
    pub measurement_id: Option<i64>,
    pub version: String,
}

pub struct Db {
    conn: Connection,
}

impl Db {
    pub fn open(path: &std::path::Path) -> DbResult<Self> {
        Self::init(Connection::open(path)?)
    }

    pub fn memory() -> DbResult<Self> {
        Self::init(Connection::open_in_memory()?)
    }

    fn init(conn: Connection) -> DbResult<Self> {
        conn.execute_batch(
            "CREATE TABLE IF NOT EXISTS measurements(
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                ts INTEGER NOT NULL,
                acetone_ppm REAL NOT NULL,
                source TEXT NOT NULL,
                quality TEXT NOT NULL,
                calibration TEXT NOT NULL,
                sensor_model TEXT,
                consent_sync INTEGER NOT NULL DEFAULT 0);
             CREATE INDEX IF NOT EXISTS idx_m_ts ON measurements(ts);
             CREATE TABLE IF NOT EXISTS user_logs(
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                ts INTEGER NOT NULL, kind TEXT NOT NULL,
                value REAL, unit TEXT, text TEXT NOT NULL DEFAULT '');
             CREATE INDEX IF NOT EXISTS idx_l_ts ON user_logs(ts);
             CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY, value TEXT NOT NULL);
             CREATE TABLE IF NOT EXISTS consents(
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                ts INTEGER NOT NULL, scope TEXT NOT NULL, granted INTEGER NOT NULL,
                measurement_id INTEGER, version TEXT NOT NULL);
             PRAGMA user_version = 1;",
        )?;
        Ok(Self { conn })
    }

    pub fn insert(&self, ts: i64, m: &NewMeasurement, consent_sync: bool) -> DbResult<i64> {
        self.conn.execute(
            "INSERT INTO measurements(ts,acetone_ppm,source,quality,calibration,sensor_model,consent_sync)
             VALUES(?1,?2,?3,?4,?5,?6,?7)",
            params![ts, m.acetone_ppm, m.source, m.quality, m.calibration, m.sensor_model, consent_sync],
        )?;
        let id = self.conn.last_insert_rowid();
        self.add_consent(ts, "measurement_sync", consent_sync, Some(id))?;
        Ok(id)
    }

    pub fn since(&self, from_ts: i64) -> DbResult<Vec<Measurement>> {
        let mut st = self.conn.prepare(
            "SELECT id,ts,acetone_ppm,source,quality,calibration,sensor_model,consent_sync
             FROM measurements WHERE ts>=?1 ORDER BY ts",
        )?;
        let rows = st.query_map([from_ts], |r| {
            Ok(Measurement {
                id: r.get(0)?,
                ts: r.get(1)?,
                acetone_ppm: r.get(2)?,
                source: r.get(3)?,
                quality: r.get(4)?,
                calibration: r.get(5)?,
                sensor_model: r.get(6)?,
                consent_sync: r.get(7)?,
            })
        })?;
        rows.collect()
    }

    pub fn all(&self) -> DbResult<Vec<Measurement>> {
        self.since(0)
    }

    pub fn add_log(&self, ts: i64, l: &UserLog) -> DbResult<i64> {
        self.conn.execute(
            "INSERT INTO user_logs(ts,kind,value,unit,text) VALUES(?1,?2,?3,?4,?5)",
            params![ts, l.kind, l.value, l.unit, l.text],
        )?;
        Ok(self.conn.last_insert_rowid())
    }

    pub fn logs_between(&self, from_ts: i64, to_ts: i64) -> DbResult<Vec<UserLog>> {
        let mut st = self.conn.prepare(
            "SELECT id,ts,kind,value,unit,text FROM user_logs WHERE ts>=?1 AND ts<?2 ORDER BY ts",
        )?;
        let rows = st.query_map([from_ts, to_ts], |r| {
            Ok(UserLog { id: r.get(0)?, ts: r.get(1)?, kind: r.get(2)?, value: r.get(3)?, unit: r.get(4)?, text: r.get(5)? })
        })?;
        rows.collect()
    }

    pub fn delete_log(&self, id: i64) -> DbResult<usize> {
        self.conn.execute("DELETE FROM user_logs WHERE id=?1", [id])
    }

    pub fn add_consent(&self, ts: i64, scope: &str, granted: bool, mid: Option<i64>) -> DbResult<()> {
        self.conn.execute(
            "INSERT INTO consents(ts,scope,granted,measurement_id,version) VALUES(?1,?2,?3,?4,?5)",
            params![ts, scope, granted, mid, CONSENT_VERSION],
        )?;
        Ok(())
    }

    pub fn consents(&self, limit: i64) -> DbResult<Vec<Consent>> {
        let mut st = self.conn.prepare(
            "SELECT id,ts,scope,granted,measurement_id,version FROM consents ORDER BY ts DESC,id DESC LIMIT ?1",
        )?;
        let rows = st.query_map([limit], |r| {
            Ok(Consent { id: r.get(0)?, ts: r.get(1)?, scope: r.get(2)?, granted: r.get(3)?, measurement_id: r.get(4)?, version: r.get(5)? })
        })?;
        rows.collect()
    }

    pub fn consent_count(&self) -> DbResult<i64> {
        self.conn.query_row("SELECT COUNT(*) FROM consents", [], |r| r.get(0))
    }

    /// 删除测量、用户记录和与测量绑定的授权；全局授权变更历史保留。
    pub fn delete_all(&self) -> DbResult<usize> {
        self.conn.execute("DELETE FROM consents WHERE measurement_id IS NOT NULL", [])?;
        self.conn.execute("DELETE FROM user_logs", [])?;
        self.conn.execute("DELETE FROM measurements", [])
    }

    pub fn setting(&self, key: &str) -> DbResult<Option<String>> {
        match self.conn.query_row("SELECT value FROM settings WHERE key=?1", [key], |r| r.get(0)) {
            Ok(v) => Ok(Some(v)),
            Err(rusqlite::Error::QueryReturnedNoRows) => Ok(None),
            Err(e) => Err(e),
        }
    }

    pub fn set_setting(&self, key: &str, value: &str) -> DbResult<()> {
        self.conn.execute(
            "INSERT INTO settings(key,value) VALUES(?1,?2) ON CONFLICT(key) DO UPDATE SET value=?2",
            params![key, value],
        )?;
        Ok(())
    }
}

/// 区间起点（本地时间）：today=当日零点，7d/30d/90d 为向前 N 天。
pub fn range_start(range: &str, now: i64) -> i64 {
    let dt = Local.timestamp_opt(now, 0).single().unwrap_or_else(Local::now);
    let days = match range {
        "today" => return day_start(now),
        "7d" => 7,
        "90d" => 90,
        _ => 30,
    };
    (dt - Duration::days(days)).timestamp()
}

/// 给定时间所在本地日的零点。
pub fn day_start(ts: i64) -> i64 {
    let dt = Local.timestamp_opt(ts, 0).single().unwrap_or_else(Local::now);
    dt.date_naive()
        .and_hms_opt(0, 0, 0)
        .and_then(|n| Local.from_local_datetime(&n).single())
        .map(|d| d.timestamp())
        .unwrap_or(ts - 86_400)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn mock(v: f64) -> NewMeasurement {
        NewMeasurement { acetone_ppm: v, source: "mock".into(), quality: "unknown".into(), calibration: "uncalibrated".into(), sensor_model: None }
    }

    #[test]
    fn range_queries() {
        let db = Db::memory().unwrap();
        let now = 1_800_000_000;
        for (d, v) in [(0, 1.0), (3, 1.2), (20, 1.4), (40, 1.6), (80, 1.8)] {
            db.insert(now - d * 86_400 - 100, &mock(v), d == 3).unwrap();
        }
        assert_eq!(db.since(range_start("7d", now)).unwrap().len(), 2);
        assert_eq!(db.since(range_start("30d", now)).unwrap().len(), 3);
        assert_eq!(db.since(range_start("90d", now)).unwrap().len(), 5);
        assert_eq!(db.consent_count().unwrap(), 5);
        db.delete_all().unwrap();
        assert!(db.all().unwrap().is_empty());
        assert_eq!(db.consent_count().unwrap(), 0);
    }

    #[test]
    fn measurement_validation_matches_aeromet_contract() {
        assert!(mock(1.2).validate().is_ok());
        assert!(mock(301.0).validate().is_err());
        assert!(mock(f64::NAN).validate().is_err());
        let mut m = mock(1.0);
        m.calibration = "verified".into();
        assert!(m.validate().is_err(), "mock 不能标已校准");
        let mut s = mock(1.0);
        s.source = "sensor".into();
        assert!(s.validate().is_err(), "sensor 缺 sensor_model");
        s.sensor_model = Some("CM2201".into());
        assert!(s.validate().is_ok());
        s.source = "manual".into();
        assert!(s.validate().is_err());
    }

    #[test]
    fn log_validation_and_roundtrip() {
        let db = Db::memory().unwrap();
        let w = UserLog { id: 0, ts: 0, kind: "weight".into(), value: Some(62.3), unit: Some("kg".into()), text: "".into() };
        assert!(w.validate().is_ok());
        let bad = UserLog { value: Some(500.0), ..w.clone() };
        assert!(bad.validate().is_err());
        let note = UserLog { kind: "medication_note".into(), value: None, text: "".into(), ..w.clone() };
        assert!(note.validate().is_err());
        db.add_log(1_000, &w).unwrap();
        db.add_log(90_000, &w).unwrap();
        assert_eq!(db.logs_between(0, 86_400).unwrap().len(), 1);
        let id = db.logs_between(0, 86_400).unwrap()[0].id;
        assert_eq!(db.delete_log(id).unwrap(), 1);
    }

    #[test]
    fn global_consent_history_survives_delete_all() {
        let db = Db::memory().unwrap();
        db.add_consent(10, "upload_default", true, None).unwrap();
        db.insert(20, &mock(1.0), true).unwrap();
        db.delete_all().unwrap();
        let c = db.consents(10).unwrap();
        assert_eq!(c.len(), 1);
        assert_eq!(c[0].scope, "upload_default");
    }

    #[test]
    fn settings_roundtrip() {
        let db = Db::memory().unwrap();
        assert_eq!(db.setting("k").unwrap(), None);
        db.set_setting("k", "1").unwrap();
        db.set_setting("k", "2").unwrap();
        assert_eq!(db.setting("k").unwrap().as_deref(), Some("2"));
    }
}
