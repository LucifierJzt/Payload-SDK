# 妙算 3 → manifold-ws-hub 遥测上报

本模块已编入 `dji_sdk_demo_on_manifold3`：应用启动后自动连接云端 WebSocket，按 1Hz 上报 FC 订阅数据。

## 已接入位置

| 文件 | 作用 |
|------|------|
| `ws_telemetry.c` / `.h` | WS 客户端 + FC 拉取 + 组 JSON |
| `ws_telemetry_config.h` | **云端地址 / device_id / token** |
| `manifold3/application/dji_sdk_config.h` | `CONFIG_MODULE_SAMPLE_WS_TELEMETRY_ON` |
| `manifold3/application/main.c` | `DjiCore_ApplicationStart()` 后启动服务 |

## 改云端配置

编辑：

`samples/sample_c/module_sample/ws_telemetry/ws_telemetry_config.h`

```c
#define WS_TELEMETRY_HOST           "你的云IP或域名"  // 不要写 ws://
#define WS_TELEMETRY_PORT           8080
#define WS_TELEMETRY_DEVICE_ID      "manifold-001"
#define WS_TELEMETRY_DEVICE_TOKEN   "与云端 .env 一致"
```

须与 `manifold-ws-hub` 服务器 `.env` 中 `DEVICE_ID` / `DEVICE_TOKEN` 一致。

当前默认：

- Host: `106.15.43.174`
- Port: `8080`（明文 `ws://`，**不支持 WSS/TLS**；若只用 443，需在云上开放 8080 或后续加 TLS）

改完后重新交叉编译并打 `.dpk`。

## 编译与打包（本机 Mac）

```bash
export PATH="/Volumes/disk_out/Users/lucifier/workspace/PSDK/tools/bin:$PATH"
cd /Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK
mkdir -p build && cd build
# 若尚未 cmake：
# cmake ..
make -j$(sysctl -n hw.ncpu) dji_sdk_demo_on_manifold3
cd ..
bash tools/build_dpk/build_dpk.sh \
  -i samples/sample_c/platform/linux/manifold3/app_json/app.json \
  -o build/dpk
```

产物：

- 二进制：`build/bin/dji_sdk_demo_on_manifold3`
- 安装包：`build/dpk/PSDK_v01.00.00.01.dpk`（版本以 `app.json` 的 `firmware_version` 为准）

`app.json` 中 `user_app_id` / `firmware_version` 须与 `dji_sdk_app_info.h`、代码内 `T_DjiFirmwareVersion` 一致。

## 安装到妙算 3

1. 妙算能上网（模块/天线/网线任一）。
2. 将 `PSDK_v01.00.00.01.dpk` 拷到遥控器存储或 SD。
3. 妙算装在飞机上，开飞机 + 遥控器，遥控器联网。
4. **DJI Pilot 2** → 应用管理 → 安装该 `.dpk` → 启动应用。
5. 云端验收：

```bash
curl -s http://106.15.43.174:8080/health
curl -s http://106.15.43.174:8080/status
# 浏览器打开看板
open http://106.15.43.174:8080/
```

`/status` 中对应 `device_id` 应 `online: true`，并有最新 `telemetry`。

## 调试（不装 dpk 时）

有线/SSH 到妙算后可直接跑二进制（路径按你拷贝位置）：

```bash
# 日志里应有：
# ws telemetry service started
# ws telemetry connected to 106.15.43.174:8080 as manifold-001
# ws telemetry seq=...
```

妙算须能访问 `云主机:8080`（安全组/防火墙放行 TCP 8080）。

## 上报内容

- `telemetry`：姿态、速度、融合位置、电池等（FC 有数据时）
- `heartbeat`：约每 5 秒

未上飞机或 FC 无数据时仍会连 hub，字段可能为 0/null。

## 双向：云端下发 command

设备会解析服务端下发的 **WebSocket 文本帧** JSON。

### 云端 → 设备

```json
{"type":"command","req_id":"cmd-1","payload":{"name":"ping"}}
{"type":"command","req_id":"cmd-2","payload":{"name":"set_get_param","confirm":true}}
{"type":"command","req_id":"cmd-3","payload":{"name":"takeoff_landing","confirm":true}}
```

| name | 说明 | 需要 confirm:true |
|------|------|-------------------|
| `ping` | 联通测试 | 否 |
| `status` | 是否忙、是否允许飞控 | 否 |
| `set_get_param` | 飞控参数 sample | 是 |
| `takeoff_landing` | 起飞降落 sample | 是 |
| `position_ctrl` | 位置控制 sample | 是 |
| `velocity_ctrl` | 速度控制 sample | 是 |
| `go_home_force_land` | 返航/迫降 sample | 是 |
| `arrest_flying` | 停飞 sample | 是 |
| `gimbal_rotate` | 云台转动（pitch/roll/yaw 度） | 否 |
| `gimbal_reset` | 云台回中 | 否 |
| `gimbal_mode` | 云台模式 free/yaw_follow/fpv | 否 |

`WS_TELEMETRY_ALLOW_FLIGHT_CMD` 在 `ws_telemetry_config.h`（默认 1）。飞控指令**必须**带 `"confirm":true`。  
云台指令默认开启（`WS_TELEMETRY_ALLOW_GIMBAL_CMD`），**不需要** confirm，室内可测。

### 云台指令示例

```json
{"type":"command","req_id":"g1","payload":{"name":"gimbal_reset"}}

{"type":"command","req_id":"g2","payload":{"name":"gimbal_rotate","pitch":-20,"yaw":0,"roll":0,"time":0.5,"mode":"relative"}}

{"type":"command","req_id":"g3","payload":{"name":"gimbal_rotate","pitch":-45,"mode":"absolute","time":1.0}}

{"type":"command","req_id":"g4","payload":{"name":"gimbal_mode","mode":"yaw_follow"}}
```

- `mode`: `relative`（默认，相对当前）/ `absolute` / `speed`
- 同时遥测 `payload.gimbal` 会反映角度变化
- 挂载口默认 `PAYLOAD_PORT_NO1`（`WS_TELEMETRY_GIMBAL_MOUNT_POS`）

### 设备 → 云端

```json
{"type":"command_result","req_id":"cmd-1","payload":{"ok":true,"message":"pong"}}
{"type":"command_result","req_id":"cmd-3","payload":{"ok":true,"message":"flight_sample_accepted"}}
{"type":"command_result","req_id":"cmd-3","payload":{"ok":true,"message":"flight_sample_done"}}
```

服务端需把观察端/业务的 command **转发到 device 这条 WS 连接**（`manifold-ws-hub` 已支持 viewer→device；自研 Java 服务需自行实现推送）。

## 注意

- 本模块是 **出站 WS 客户端**，与 DJI `CloudApi` 样例无关。
- 仅 **ws://host:port**；生产建议 Nginx 反代 `wss://` 后再扩展客户端 TLS。
- token 勿提交到公开仓库；换 token 后务必重编安装。
