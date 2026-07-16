# ws_telemetry 模块（代码旁说明）

编入 `dji_sdk_demo_on_manifold3`：连云端 WS、上报 FC 遥测、收 command、控云台、启停 Liveview/RTMP。

## 完整文档（请优先阅读）

在工作区 `PSDK/`（与 `Payload-SDK` 同级）下：

| 文档 | 内容 |
|------|------|
| `docs/03-ws-cloud-integration.md` | **协议、指令表、RTMP、架构（主文档）** |
| `docs/04-deploy-dpk.md` | 编译 / 打包 / 安装 |
| `docs/README.md` | 文档索引 |
| `README.md` | 工作区入口 |

## 本目录文件

| 文件 | 作用 |
|------|------|
| `ws_telemetry.c` / `.h` | WS 客户端、telemetry、command 分发 |
| `ws_telemetry_config.h` | Host / Port / Path / Token / RTMP / 开关 |
| `ws_liveview.c` / `.h` | Liveview H264 + ffmpeg 推流 |
| `README.zh-CN.md` | 本文件 |

## 改配置

编辑 `ws_telemetry_config.h` → 重新 `make` + `build_dpk` + 安装（见 `docs/04-deploy-dpk.md`）。

## 极简联调

```text
连接: ws://HOST:PORT/manifold-ws?device_id=...&token=...&role=device
上行: telemetry / heartbeat / command_result
下行: command（gimbal_* / live_* / ping / 飞控 dry-run）
应用: dji_app_ctl start PSDK
```
