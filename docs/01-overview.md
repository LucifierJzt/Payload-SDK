# 项目总览与目录

## 目标

在 **DJI 妙算 3（Manifold 3）** 上跑 PSDK 应用，对接 **M4T** 等机型：

- 飞控状态订阅与云端遥测
- WebSocket 双向指令（云台、直播启停；飞控可 dry-run）
- Liveview 取机载相机 H.264，可选 RTMP 实时推流

## 工作区布局

```text
PSDK/                              # 工作区根
├── AGENTS.md                      # Agent 规则：Track A/B/C
├── README.md                      # 入口
├── docs/                          # 整理后的项目文档（本目录）
├── Manifold3-M4TD-开发步骤.md     # 过程记录（会随进度更新）
├── README-manifold3-wifi-8852bu.zh-CN.md
├── manifold-ws-hub/               # 可选：Node 简易 WS 汇聚（参考）
├── tools/                         # aarch64 交叉工具链、cmake 等
└── Payload-SDK/                   # PSDK 源码（多为软链）
    └── samples/sample_c/
        ├── platform/linux/manifold3/   # 妙算 3 入口、app.json
        └── module_sample/ws_telemetry/ # 云端 WS + Liveview 模块
```

## 开发轨道（摘要）

| Track | 内容 | 本项目 |
|-------|------|--------|
| **A** | 普通 Linux 用户态 | 工具链、组网脚本 |
| **B** | PSDK `.dpk` 应用 | **主路径** |
| **C** | 系统镜像/内核 | 不做 |

正式交付：`编译 → build_dpk → scp/Pilot 安装 → dji_app_ctl start`。

## 当前能力一览

| 能力 | 状态 | 说明 |
|------|------|------|
| SSH / 组网 | ✅ | 有线或外置网卡/模块；8852 配网权限受限时可旁路 |
| WS 遥测上行 | ✅ | FC 姿态/速度/GPS/电池/云台角等 1Hz |
| WS 指令下行 | ✅ | 云台、直播、ping；飞控 dry-run |
| 云台控制 | ✅ | `gimbal_rotate` / `reset` / `mode` |
| 实时画面 | ✅ | Liveview + ffmpeg → RTMP（需服务端） |
| 真飞控起飞 | ⚠️ | 代码有，默认 **DRY_RUN=1** 室内不飞 |

## 关键账号与路径

| 项 | 典型值 |
|----|--------|
| SSH | `dji@<妙算IP>`，密码 `dji` |
| 应用名 | `PSDK`（`dji_app_ctl start PSDK`） |
| 安装包 | `PSDK_v01.00.00.01.dpk`（版本以 app.json 为准） |
| 直播落盘 | `/home/dji/live_out/*.h264` |
| 代码配置 | `ws_telemetry_config.h` |
