# 架构与协议入口

## 板端

`hardware/firmware/app_breathon_ui.c` 连接 TuyaOpen 与 LVGL 页面。Wi-Fi 恢复由 `app_breathon_wifi*.c` 处理；实时语音分别由控制、网络与播放模块维护。`realtime_wire.*` 定义音频与控制帧，`realtime_request.*` 和 `session_core.*` 提供状态 / 请求管理。

`hardware/breathon-ui/src/` 是原生 C 界面；`sim/` 与固件复用同一套 UI 代码进行桌面验证。展示模式与真实语音由 `app_breathon_realtime_mode.h` 区分。

## BLE

帧定义：`hardware/firmware/breathon_ble_proto.h`。

| 项目 | 值 |
| --- | --- |
| 广播名 | `BREATHON` |
| 服务 | `0xFD50` |
| Write | `00000001-0000-1001-8001-00805f9b07d0` |
| Notify | `00000002-0000-1001-8001-00805f9b07d0` |
| Magic | `0xB7` |
| 命令 | Start `0x10`、Stop `0x11`、Calibrate `0x12`、Status `0x13` |

App 解析器位于 `app/src-tauri/src/device/proto.rs`。BLE 包中的数据来源限制见 [STATUS.md](STATUS.md)。

## 服务与 App

- `services/backend/main.py`：设备、会话、测量与记录 REST 接口。
- `services/shared/openapi.json`：已有接口定义快照；运行时 `/openapi.json` 以实际服务代码为准。
- `services/backend/realtime/`：网关鉴权、缓冲、上游连接、重采样、会话取消与音频发送。
- `app/src/lib/api.ts`：浏览器预览与 Tauri 命令适配；当前未直接调用数据服务。
- `app/src-tauri/src/db.rs`：独立本地 SQLite 存储。

配置来自各自环境；公开仓库不包含本机 IP、可用凭据或设备授权配置。
