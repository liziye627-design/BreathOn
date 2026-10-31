# 本次公开快照的验证

日期：2026-11-01。以下在整理后的独立仓库目录执行，不依赖原始目录里的构建输出；未操作开发板串口、未烧录、未调用付费模型测试。

| 检查 | 结果 |
| --- | --- |
| App `pnpm install --frozen-lockfile` | 通过 |
| App `pnpm build` | TypeScript 检查与 Vite 构建通过 |
| App 估算规则测试 | 5 项通过；仅验证工程规则，不验证医学准确性 |
| App Rust BLE 帧解析器 | Rust 2021 独立测试，4 项通过 |
| Python API / Realtime 测试 | 104 项通过，使用本地既有 Python 3.11 依赖环境 |
| C BLE 协议测试 | 通过 |
| C 音频帧 / 缓冲 / generation 测试 | 通过 |
| Wi-Fi 恢复测试 | 实时与展示两种模式通过；公开测试使用虚构网络凭据 |
| 原生 UI | 下载固定依赖、校验字体哈希、CMake 构建、CTest 3/3 通过 |
| TuyaOpen / T5AI 补丁 | 在固定 commit 的原始目标文件上执行 `git apply --check` |
| Gitleaks | 发布前对源码扫描；测试中的固定存储键名有一处精确的人工误报注释 |

CI 配置位于 `.github/workflows/checks.yml`，覆盖 App、C 协议、Rust 帧解析和 Python 服务。远端运行结果以 GitHub Actions 为准。

## 未覆盖

- 未在本次发布中从干净 SDK 完整编译或烧录 T5 固件。
- 未打包 Tauri 桌面安装器、Android APK 或 iOS 应用。
- 未验证 App 实际 BLE 连接、移动端权限和后台运行。
- 未验证真实传感器、摄像头、医学模型或长期临床数据。
- 未证明板端实时语音的持续听说、回声处理和打断已通过。

历史研发状态与本次验证范围不同，见 [STATUS.md](STATUS.md)。
