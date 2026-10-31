# 数据接口与实时语音网关

两项独立服务：`backend.main` 提供数据 API 和 Web 调试台；`backend.realtime` 接收板端音频并连接 OpenAI Realtime。API Key 只由网关读取，不写入 App 或固件。

## 数据 API

需要 Python 3.11 和 uv。在本目录执行：

```sh
uv venv --python 3.11
uv pip install -r backend/requirements.txt
uv run --no-project python -m uvicorn backend.main:app --host 127.0.0.1 --port 8000
```

访问 `http://127.0.0.1:8000` 和 `/docs`。默认自动生成演示数据；设 `AEROMET_AUTO_SEED=0` 可关闭。`.env.example` 是变量清单，程序不会自动加载它，需要由终端或启动器设置环境变量。

开发默认 Token 为 `local-demo`。仅用于本机测试；如需连接可信局域网设备，先自行设置 `AEROMET_TOKEN`、CORS 和访问控制。此原型不适合直接暴露到公网。

## 实时语音

```sh
uv pip install -r backend/realtime-requirements.txt
# 在本机环境中设置 OPENAI_API_KEY 与 BREATHON_DEVICE_TOKEN 后：
uv run --no-project python -m backend.realtime
```

| 环境变量 | 用途 / 默认值 |
| --- | --- |
| `OPENAI_API_KEY` | 从使用者自己的凭据存储注入 |
| `BREATHON_DEVICE_TOKEN` | 板子与网关共享的独立接入凭据 |
| `BREATHON_BIND_HOST` | 默认 `127.0.0.1`；实板连接需指定电脑真实局域网地址 |
| `BREATHON_PORT` | 默认 `8766` |
| `BREATHON_BIND_INTERFACE` | 可选接口索引 |
| `BREATHON_MAX_SESSION_SECONDS` | 默认且最多 180 秒 |

当前代码固定模型 `gpt-realtime-2.1`、声音 `marin`，这是开发时的配置快照，不是对所有账号和地区可用性的保证。配置解析会拒绝不匹配的模型 / 声音覆盖值。运行真实会话会产生模型 API 用量。

板端使用局域网 TCP 音频协议，当前不提供传输 TLS；应限制在受控测试网络。Wi-Fi 连上路由器、电脑网关可达、模型服务可达和扬声器实际出声，是四项不同的验收条件。

## 测试

```sh
uv pip install -r backend/requirements.txt -r backend/realtime-requirements.txt
uv run --no-project python -m pytest tests -q
```

测试使用模拟上游与隔离数据库，不需要真实 API Key。实时连通性探针 `python -m backend.realtime.probe` 是可选的付费联网测试，本次公开仓库检查未运行它。
