mod db;
mod device;

use db::{day_start, range_start, Consent, Db, Measurement, NewMeasurement, UserLog};
use device::{DeviceInfo, DeviceLink, MockDevice, Reading};
use serde::Serialize;
use std::sync::Mutex;
use tauri::{Manager, State};

struct AppState {
    db: Mutex<Db>,
    device: Mutex<Box<dyn DeviceLink>>,
}

type CmdResult<T> = Result<T, String>;

fn err<E: std::fmt::Display>(e: E) -> String {
    e.to_string()
}

fn now() -> i64 {
    chrono::Local::now().timestamp()
}

#[derive(Serialize)]
struct Insight {
    /// 固定为 false：当前是本地模板文案，不是大模型输出。
    is_llm: bool,
    headline: String,
    summary: String,
    tips: Vec<(String, String)>,
}

#[derive(Serialize)]
struct DayLogs {
    logs: Vec<UserLog>,
}

#[tauri::command]
fn device_info(s: State<AppState>) -> CmdResult<DeviceInfo> {
    Ok(s.device.lock().map_err(err)?.info())
}

#[tauri::command]
fn device_connect(s: State<AppState>) -> CmdResult<DeviceInfo> {
    let mut d = s.device.lock().map_err(err)?;
    d.connect().map_err(err)?;
    Ok(d.info())
}

#[tauri::command]
fn calibrate(s: State<AppState>) -> CmdResult<()> {
    s.device.lock().map_err(err)?.calibrate().map_err(err)
}

#[tauri::command]
fn sample(s: State<AppState>) -> CmdResult<Reading> {
    s.device.lock().map_err(err)?.sample().map_err(err)
}

/// 保存一次测量。`sync` 是本次测量的单独授权选择，授权记录同步留痕。
#[tauri::command]
fn save_measurement(s: State<AppState>, m: NewMeasurement, sync: bool, ts: Option<i64>) -> CmdResult<i64> {
    m.validate()?;
    s.db.lock().map_err(err)?.insert(ts.unwrap_or_else(now), &m, sync).map_err(err)
}

#[tauri::command]
fn get_records(s: State<AppState>, range: String) -> CmdResult<Vec<Measurement>> {
    s.db.lock().map_err(err)?.since(range_start(&range, now())).map_err(err)
}

#[tauri::command]
fn add_log(s: State<AppState>, log: UserLog, ts: Option<i64>) -> CmdResult<i64> {
    log.validate()?;
    s.db.lock().map_err(err)?.add_log(ts.unwrap_or_else(now), &log).map_err(err)
}

/// 取某一天（传入当天任意时刻的时间戳）的用户记录。
#[tauri::command]
fn logs_for_day(s: State<AppState>, day_ts: i64) -> CmdResult<DayLogs> {
    let start = day_start(day_ts);
    let logs = s.db.lock().map_err(err)?.logs_between(start, start + 86_400).map_err(err)?;
    Ok(DayLogs { logs })
}

#[tauri::command]
fn delete_log(s: State<AppState>, id: i64) -> CmdResult<usize> {
    s.db.lock().map_err(err)?.delete_log(id).map_err(err)
}

/// 全局授权开关（默认关闭）。每次变更都写入授权留痕。
#[tauri::command]
fn set_consent(s: State<AppState>, scope: String, granted: bool) -> CmdResult<()> {
    if scope != "upload_default" {
        return Err("未知授权范围".into());
    }
    let db = s.db.lock().map_err(err)?;
    db.set_setting("consent_upload_default", if granted { "1" } else { "0" }).map_err(err)?;
    db.add_consent(now(), &scope, granted, None).map_err(err)
}

#[tauri::command]
fn consent_history(s: State<AppState>) -> CmdResult<Vec<Consent>> {
    s.db.lock().map_err(err)?.consents(50).map_err(err)
}

#[tauri::command]
fn get_setting(s: State<AppState>, key: String) -> CmdResult<Option<String>> {
    s.db.lock().map_err(err)?.setting(&key).map_err(err)
}

#[tauri::command]
fn set_setting(s: State<AppState>, key: String, value: String) -> CmdResult<()> {
    s.db.lock().map_err(err)?.set_setting(&key, &value).map_err(err)
}

#[tauri::command]
fn export_records(s: State<AppState>) -> CmdResult<String> {
    let db = s.db.lock().map_err(err)?;
    let out = serde_json::json!({
        "measurements": db.all().map_err(err)?,
        "user_logs": db.logs_between(0, i64::MAX).map_err(err)?,
        "consents": db.consents(10_000).map_err(err)?,
    });
    serde_json::to_string_pretty(&out).map_err(err)
}

#[tauri::command]
fn delete_records(s: State<AppState>) -> CmdResult<usize> {
    s.db.lock().map_err(err)?.delete_all().map_err(err)
}

/// 本地模板文案。有 mock 数据时明确声明；不做诊断、不判定状态、不评论用药。
#[tauri::command]
fn insight(s: State<AppState>) -> CmdResult<Insight> {
    let rows = s.db.lock().map_err(err)?.since(range_start("7d", now())).map_err(err)?;
    let (headline, summary) = if rows.is_empty() {
        ("还没有检测记录".to_string(), "完成几次检测后，这里会汇总你的长期趋势。".to_string())
    } else if rows.iter().any(|m| m.source == "mock") {
        (
            "这里包含工程测试输入".to_string(),
            format!("最近 7 日有 {} 条记录，其中含模拟数据，仅用于验证流程，不代表真实呼气结果。", rows.len()),
        )
    } else {
        let avg = rows.iter().map(|m| m.acetone_ppm).sum::<f64>() / rows.len() as f64;
        (
            "已记录传感器输入".to_string(),
            format!("最近 7 日共 {} 次记录，平均 {:.2} ppm。该数值仅作个人趋势参考，未经校准验证前不用于健康判断。", rows.len(), avg),
        )
    };
    let tip = |a: &str, b: &str| (a.to_string(), b.to_string());
    Ok(Insight {
        is_llm: false,
        headline,
        summary,
        tips: vec![
            tip("规律饮食", "保持均衡营养，避免过度节食"),
            tip("适量活动", "按自己的节奏安排日常活动"),
            tip("充足饮水", "把饮水量记在“记录”里，观察自己的习惯"),
        ],
    })
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .setup(|app| {
            let dir = app.path().app_data_dir()?;
            std::fs::create_dir_all(&dir)?;
            let db = Db::open(&dir.join("breathon.db"))?;
            app.manage(AppState {
                db: Mutex::new(db),
                device: Mutex::new(Box::new(MockDevice::new())),
            });
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            device_info, device_connect, calibrate, sample, save_measurement, get_records,
            add_log, logs_for_day, delete_log, set_consent, consent_history, get_setting,
            set_setting, export_records, delete_records, insight
        ])
        .run(tauri::generate_context!())
        .expect("error while running BREATHON");
}
