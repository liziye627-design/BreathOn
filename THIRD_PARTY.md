# 第三方来源

| 组件 | 来源 / 固定版本 | 发布方式 |
| --- | --- | --- |
| TuyaOpen | `tuya/TuyaOpen` · `695e571fb4f62932dbf86a86fde664fcb7f2c0b4` | 仅提供针对已修改文件的补丁和新增适配代码；上游 Apache-2.0 |
| T5AI 平台 | `tuya/TuyaOpen-T5AI` · `69793c66bebf58ef4f886282626c2ea96a28f9a2` | 仅提供 Wi-Fi 修改补丁；保留 Apache-2.0 许可文本 |
| LVGL | `lvgl/lvgl` · `e1c0b21b2723d391b885de4b2ee5cc997eccca91` | 不整包上传；用户独立获取，依赖自身许可 |
| 普惠体 LVGL 字库 | NiceMCU 源码 / TuyaOpen 中 `font_puhui_18_2.c`、`font_puhui_14_1.c` | 不随仓库重新分发字库；脚本按固定来源与哈希获取，使用前阅读上游字体许可 |
| App 依赖 | Tauri、Vite、TypeScript、Rust crates | 由 `pnpm-lock.yaml` / `Cargo.lock` 记录，按各自许可下载 |
| Python 依赖 | FastAPI、Pydantic、AnyIO、OpenAI SDK、soxr 等 | 由 requirements 文件记录，按各自许可安装 |

上游入口：

- [TuyaOpen](https://github.com/tuya/TuyaOpen)
- [TuyaOpen-T5AI](https://github.com/tuya/TuyaOpen-T5AI)
- [NiceMCU 板卡](https://github.com/NiceMCU-01/NiceMCU-T5-2.8IPS)
- [NiceMCU 字库来源](https://github.com/NiceMCU-01/tuyaopen_nicmcu_t5/tree/5424591cc7fb8bcfaef4ddeb5caac1f67625c4b5)
- [LVGL](https://github.com/lvgl/lvgl)

`app/public/` 中的品牌、角色、界面切图来自项目提供的设计素材，本次仅用于项目展示，未单独授予第三方再分发许可。原生 LVGL 界面由 C 图元实现，预览图由同一套界面代码渲染。
