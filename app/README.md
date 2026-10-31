# Breath On App

TypeScript + Vite 前端，Tauri 2 + Rust 桌面容器。页面涵盖欢迎、首页、设备、采集过程、结果、趋势、记录、聊天与个人设置。

## 运行

```sh
pnpm install --frozen-lockfile
pnpm dev
pnpm build
node --experimental-strip-types --test tests/metabolism.test.ts
```

浏览器默认在 `http://localhost:1420` 预览，使用 `src/lib/preview.ts` 的本地存储回退。Tauri 桌面开发使用 `pnpm tauri dev`，须先安装 Rust 和对应系统的 Tauri 构建依赖。

## 代码入口

| 路径 | 作用 |
| --- | --- |
| `src/views/` | 页面与交互 |
| `src/lib/api.ts` | Tauri 命令 / 浏览器预览适配 |
| `src/lib/metabolism.ts` | 演示估算逻辑；不代表经验证的生理模型 |
| `src-tauri/src/db.rs` | 本地记录、授权记录与导出 |
| `src-tauri/src/device/proto.rs` | BLE 数据帧解析及单元测试 |
| `src-tauri/src/device/mock.rs` | 当前实际使用的设备实现 |
| `public/` | 项目提供的品牌、角色与界面参考素材 |

## 集成边界

当前 `src-tauri/src/lib.rs` 实例化 `MockDevice`，没有完成真实 BLE 扫描 / 连接传输层。存在 BLE 协议解析器，不代表 App 已经连接真实设备。Android / iOS 构建未在本次发布中验证。

聊天页当前使用本地模板回复，不调用实时模型。板端语音网关代码位于相邻的 `services/backend/realtime/`。App 数据不会因为“同步授权”被自动上传；当前还需要实现真实同步传输。

页面中的头像、数字、丙酮分级与热量估算属于产品演示 / 工程规则，不是实测或临床结论。
