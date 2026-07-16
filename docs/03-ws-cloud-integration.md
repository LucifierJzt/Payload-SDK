# 云端 WebSocket 联调说明（妙算 3 / M4T）

> 整理日期：2026-07-16  
> 范围：设备侧 PSDK 模块 + 云端 WS 协议 + 直播 RTMP  
> 代码：`Payload-SDK/samples/sample_c/module_sample/ws_telemetry/`

---

## 1. 架构

```text
┌─────────────┐     command      ┌──────────────────┐
│ 云端 / 业务  │ ───────────────► │ WS 服务           │
│ 前端/后端   │ ◄── telemetry ── │ (自研或 hub)      │
└─────────────┘                  └────────┬─────────┘
                                          │ WS
                     device 连接          │ /manifold-ws?device_id&token&role=device
                                          ▼
                                 ┌────────────────────┐
                                 │ 妙算 3 PSDK App    │
                                 │ (dji_sdk_demo_...) │
                                 └─────────┬──────────┘
                        FC 订阅 / 云台 API / Liveview
                                           ▼
                                      飞机 M4T
                                           │
                              Liveview H264 │
                                           ▼
                                 ffmpeg → RTMP 直播服务器
```

要点：

- **控机/控云台/启停直播**：云端把 JSON **推到 device 这条 WS**；妙算执行 PSDK API。  
- **视频不走 WS JSON**；实时画面走 **RTMP**（或 UDP）。  
- 官方文档「相机视频流传输」是**载荷推流给 Pilot**；本方案是 **Liveview 从飞机拉流再推 RTMP**。

---

## 2. 连接参数（设备侧）

配置文件：

`Payload-SDK/samples/sample_c/module_sample/ws_telemetry/ws_telemetry_config.h`

| 宏 | 当前典型值 | 说明 |
|----|------------|------|
| `WS_TELEMETRY_HOST` | `1.13.253.227` | 仅 host，无 scheme |
| `WS_TELEMETRY_PORT` | `6791` | TCP 端口 |
| `WS_TELEMETRY_WS_PATH` | `/manifold-ws` | **不要**再拼 `/v1/ws` |
| `WS_TELEMETRY_DEVICE_ID` | `manifold-001` | 与云端约定 |
| `WS_TELEMETRY_DEVICE_TOKEN` | （配置内） | 鉴权；勿公开仓库泄露 |
| `WS_TELEMETRY_HZ` | `1` | 遥测频率 |
| `WS_TELEMETRY_FLIGHT_DRY_RUN` | `1` | 室内：飞控指令只模拟 |
| `WS_LIVEVIEW_PUSH_URL` | `rtmp://121.40.203.74/live/manifold` | 默认 RTMP |
| `WS_LIVEVIEW_PUSH_ENABLE` | `1` | 启用推流管线 |

设备连接 URL：

```text
ws://1.13.253.227:6791/manifold-ws?device_id=manifold-001&token=<TOKEN>&role=device
```

实现：明文 TCP + WebSocket Upgrade（**无 TLS**）。生产建议反代 `wss://` 后再扩展客户端。

---

## 3. 上行：telemetry / heartbeat

### 3.1 heartbeat（约每 5s）

```json
{
  "type": "heartbeat",
  "ts": 1710000000000,
  "device_id": "manifold-001",
  "payload": { "uptime_s": 123 }
}
```

### 3.2 telemetry（约 1Hz）

```json
{
  "type": "telemetry",
  "ts": 1710000000000,
  "device_id": "manifold-001",
  "payload": {
    "seq": 40,
    "aircraft_type": "M4T",
    "flight_mode": "p_gps",
    "flight_status": "stopped",
    "armed": false,
    "gps": {
      "lat": 0.0,
      "lon": 0.0,
      "alt_m": 133.4,
      "satellites": 0,
      "valid": false,
      "signal_level": 0,
      "fix_state": 0
    },
    "attitude": { "roll_deg": 0.1, "pitch_deg": 0.2, "yaw_deg": 129.0 },
    "gimbal": {
      "pitch_deg": -12.0,
      "roll_deg": 0.0,
      "yaw_deg": 5.0,
      "valid": true
    },
    "velocity": { "vx_m_s": 0, "vy_m_s": 0, "vz_m_s": 0, "health": 1 },
    "battery": { "percent": 80, "voltage_v": 15.2 },
    "home": { "lat": null, "lon": null, "set": false },
    "extra": { "display_mode": 6, "gps_ok": false, "gimbal_ok": true }
  }
}
```

### 3.3 FC 订阅 topic（设备侧）

| 数据 | Topic |
|------|--------|
| 姿态 | `QUATERNION` |
| 速度 | `VELOCITY` |
| 位置 | `POSITION_FUSED` + `GPS_POSITION` 回退 |
| 星数/精度 | `GPS_DETAILS` / `GPS_SIGNAL_LEVEL` |
| 高度 | `ALTITUDE_FUSED` |
| 电池 | 优先 `BATTERY_SINGLE_INFO_INDEX1`，回退 whole |
| 飞行状态/模式 | `STATUS_FLIGHT` / `STATUS_DISPLAYMODE` |
| Home | `HOME_POINT_*` |
| 云台角 | `GIMBAL_ANGLES` |

室内：GPS `valid=false` 常见；动云台应看到 `gimbal.*` 变化。

---

## 4. 下行：command → command_result

云端必须向 **已建立的 device WS 连接写文本帧**（不能只收不发）。

### 4.1 通用格式

```json
{
  "type": "command",
  "req_id": "任意字符串",
  "payload": {
    "name": "指令名",
    "...": "见下表"
  }
}
```

设备回复：

```json
{
  "type": "command_result",
  "ts": 1710000000000,
  "device_id": "manifold-001",
  "req_id": "任意字符串",
  "payload": {
    "ok": true,
    "message": "人类可读结果"
  }
}
```

### 4.2 指令表

#### 联通 / 状态

| name | payload | 说明 |
|------|---------|------|
| `ping` | — | 回 `pong` |
| `status` | — | busy / dry_run / live / push 等 |

#### 云台（室内可测，无需 confirm）

| name | 主要字段 | 说明 |
|------|----------|------|
| `gimbal_reset` | — | 俯仰+偏航回中 |
| `gimbal_mode` | `mode`: `yaw_follow` / `free` / `fpv` | 工作模式 |
| `gimbal_rotate` | `pitch`,`roll`,`yaw`（度）,`time`（秒）,`mode` | 转动 |

`gimbal_rotate.mode`：

- `relative`（默认）：相对当前  
- `absolute`：绝对角  
- `speed`：速度模式  

示例：

```json
{"type":"command","req_id":"g1","payload":{"name":"gimbal_reset"}}

{"type":"command","req_id":"g2","payload":{
  "name":"gimbal_rotate","pitch":-20,"yaw":0,"roll":0,"time":0.5,"mode":"relative"
}}
```

挂载口默认 `PAYLOAD_PORT_NO1`（`WS_TELEMETRY_GIMBAL_MOUNT_POS=1`）。

#### 直播 Liveview + RTMP

| name | 字段 | 说明 |
|------|------|------|
| `live_start` | 可选 `rtmp` 或 `push_url` | 开始拉流并推流/落盘 |
| `live_stop` | — | 停止 |
| `live_status` | — | running / bytes / file / push |

示例（方案 B RTMP）：

```json
{"type":"command","req_id":"l1","payload":{
  "name":"live_start",
  "rtmp":"rtmp://121.40.203.74/live/manifold"
}}
```

或不带 `rtmp`（用配置默认同一地址）。

成功示例：

```text
live_start_ok file=/home/dji/live_out/live_m4t_....h264 push=rtmp://121.40.203.74/live/manifold
```

本机播放：

```bash
ffplay -fflags nobuffer -flags low_delay rtmp://121.40.203.74/live/manifold
```

仅落盘时 `push=none`；查 ffmpeg：

```bash
ssh dji@<IP> 'tail -50 /tmp/ws_live_ffmpeg.log'
```

局域网备选（同网段）：

```json
{"type":"command","req_id":"l2","payload":{
  "name":"live_start","push_url":"udp://192.168.31.10:5000"
}}
```

电脑：`ffplay -fflags nobuffer -f mpegts udp://0.0.0.0:5000`

#### 飞控 sample（默认 dry-run）

| name | 需要 `confirm:true` | dry-run=1 时 |
|------|---------------------|--------------|
| `set_get_param` | 是 | 只回 `dry_run_ok`，不飞 |
| `takeoff_landing` | 是 | 同上 |
| `position_ctrl` / `velocity_ctrl` | 是 | 同上 |
| `go_home_force_land` / `arrest_flying` | 是 | 同上 |

真飞：配置 `WS_TELEMETRY_FLIGHT_DRY_RUN 0`，室外 + RC 在旁，再重编安装。

---

## 5. 云端服务注意

| 实现 | 说明 |
|------|------|
| 自研 Java（当前） | 路径 `/manifold-ws`；须解析 query `token`/`device_id`；**必须能向 device 连接写 command** |
| `manifold-ws-hub`（Node 参考） | 默认 path `/v1/ws`；viewer `command` 自动转 device |

常见问题：

- `token is null`：服务端未从 query 读 token，或 path 不一致  
- 设备在线上行有、下行无：服务端没有 push 到 device 连接  
- 只搞了 WS、没有 HTTP `/health`：**设备不请求 health**，无影响  

---

## 6. 源码索引

| 文件 | 职责 |
|------|------|
| `ws_telemetry.c` | WS 连接、收帧、command 分发、telemetry 组包 |
| `ws_telemetry_config.h` | 地址 / token / 开关 |
| `ws_liveview.c` | Liveview 启停、写文件、ffmpeg 推 RTMP/UDP |
| `main.c` + `dji_sdk_config.h` | `CONFIG_MODULE_SAMPLE_WS_TELEMETRY_ON` 启动模块 |

开关：`CONFIG_MODULE_SAMPLE_WS_TELEMETRY_ON`（manifold3 `dji_sdk_config.h`）。

---

## 7. 端到端检查清单

1. 妙算能访问 `HOST:PORT`（模块/网卡/有线）  
2. `dji_app_ctl start PSDK`，日志有 `ws telemetry connected`  
3. 云端收到 `telemetry`，`aircraft_type` 为 `M4T`  
4. 发 `ping` → `command_result` / `pong`  
5. 发 `gimbal_rotate` → 云台动，telemetry `gimbal` 变  
6. 发 `live_start` → `push=rtmp://...`，ffplay 有画面  
7. 飞控仅在 dry-run 下测协议，不室内起飞  

---

## 8. 修订记录

| 日期 | 内容 |
|------|------|
| 2026-07-16 | 初版：遥测、双向 command、云台、Liveview/RTMP、dry-run 飞控 |
