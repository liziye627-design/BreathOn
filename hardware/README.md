# NiceMCU T5 硬件源码

`firmware/` 是应用适配与协议代码，`breathon-ui/` 是原生 LVGL 页面。原厂板卡驱动、TuyaOpen SDK、T5 编译器和设备授权数据不整包放进本仓库。

## 无开发板验证

纯 C 协议测试可以直接运行：

```sh
gcc -std=c11 -Wall -Wextra -Werror -Ifirmware firmware/tests/test_ble_proto.c firmware/breathon_ble_proto.c -o ble-test
./ble-test
gcc -std=c11 -Wall -Wextra -Werror -Ifirmware firmware/tests/test_realtime.c firmware/realtime_wire.c -o realtime-test
./realtime-test
```

Windows 原生 UI 预览（需要 Git、CMake、Ninja、GCC）：

```powershell
./Setup-Preview.ps1
cmake -S breathon-ui -B breathon-ui/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build breathon-ui/build -j 6
New-Item -ItemType Directory -Force breathon-ui/evidence | Out-Null
ctest --test-dir breathon-ui/build --output-on-failure
```

脚本获取固定版本 LVGL 和两份字库，校验字库 SHA256；这些外部文件被 `.gitignore` 排除。预览产物在 `breathon-ui/evidence/`，可运行 `breathon-ui/sim/Export-Previews.ps1` 导出 PNG。

## SDK 集成

这是一份源码与补丁快照，不是已经在干净 SDK 上重新验证过的整包固件构建发布。SDK 版本见下表；请在独立 SDK 副本中集成，不覆盖正在运行的其他板测工程。

| 仓库 | 固定 commit |
| --- | --- |
| `https://github.com/tuya/TuyaOpen.git` | `695e571fb4f62932dbf86a86fde664fcb7f2c0b4` |
| `https://github.com/tuya/TuyaOpen-T5AI.git` | `69793c66bebf58ef4f886282626c2ea96a28f9a2` |

1. 获取上述版本及 SDK 子模块，在 SDK 的 `platform/T5AI` 安装固定 T5AI 平台。
2. 分别在 TuyaOpen / T5AI 根目录运行 `git apply --check`，再应用 `patches/tuyaopen.patch` / `patches/t5ai.patch`。
3. 把 `sdk-overrides/` 合并到 TuyaOpen 根目录；这是补丁需要的新增网络请求跟踪文件。
4. 把 `firmware/` 根目录的 `.c` / `.h` 复制到 `apps/tuya.ai/your_chat_bot/src/`；把 `breathon-ui/src/` 和 `breathon-ui/include/breathon_ui.h` 复制到该应用的 `src/breathon/`。先获取外部小字库。
5. 在官方 SDK 环境中选择 `NICEMCU_T5_2_8ISP.config`。注意固定版本配置名使用 `ISP` 拼写；不要随意换其他 T5 板型。
6. 启用 `ENABLE_AI_CHAT_CUSTOM_UI`、`ENABLE_LIBLVGL`、`ENABLE_LVGL_TP`、`LVGL_VERSION_9`、`LV_FONT_MONTSERRAT_12`、`LV_FONT_MONTSERRAT_32`、`FONT_TEXT_SIZE_18_2`；视频组件当前关闭。
7. 使用 SDK 的 `tos.py build` 构建并核对板型、版本、分区和校验结果，再安排单独实板验收。

`firmware/tests/*.ps1` 中部分测试需要将 SDK 放在本目录的 `vendor/TuyaOpen-official`，并依赖其 cJSON / WAV 解码器；它们不会自动下载完整 SDK。

## 烧录与语音模式

当前 `BREATHON_PRESENTATION_ONLY=1` 为本地演示。启用真实语音需要切换模式并配置板端到网关的连接；不能仅凭通话界面判定模型已经接通。

本仓库不提供自动烧录脚本和预编译 `.bin`。烧录目标是开发板 Flash，通过实际下载串口进行；Windows 的 D 盘或 SD 卡不是这块 MCU 的系统安装目标。不要全擦设备授权 / 校准区域，同一时间仅允许一个任务占用烧录串口。

真实语音、BLE 模拟来源标记和未接入传感器的限制见 [状态页](../docs/STATUS.md)。
