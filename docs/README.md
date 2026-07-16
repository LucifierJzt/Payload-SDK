# 妙算 3 / PSDK 项目文档索引

本目录在 **Payload-SDK 仓库内**（`docs/`），是 Manifold 3 联调文档入口。

## 阅读顺序（新人）

1. [项目总览与目录](01-overview.md)
2. [组网：SSH / Wi‑Fi / 5G 模块](02-network.md)
3. [云端 WebSocket 联调（遥测 / 云台 / 直播）](03-ws-cloud-integration.md) ← **主线**
4. [编译打包与安装 `.dpk`](04-deploy-dpk.md)
5. [开发轨迹记录](Manifold3-M4TD-开发步骤.md)

## 按主题

| 主题 | 文档 |
|------|------|
| Agent 开发边界（Track A/B/C） | [AGENTS.md](AGENTS.md) |
| 8852BU1 Wi‑Fi 配网踩坑 | [README-manifold3-wifi-8852bu.zh-CN.md](README-manifold3-wifi-8852bu.zh-CN.md) |
| WS 云端协议与指令表 | [03-ws-cloud-integration.md](03-ws-cloud-integration.md) |
| 模块内简要说明（代码旁） | [../samples/sample_c/module_sample/ws_telemetry/README.zh-CN.md](../samples/sample_c/module_sample/ws_telemetry/README.zh-CN.md) |
| 上游 Manifold 说明 | [../README-manifold3-system-dev.zh-CN.md](../README-manifold3-system-dev.zh-CN.md) 等 |

## 代码仓库

- 业务 fork：https://github.com/LucifierJzt/Payload-SDK  
- 勿直接 push 到官方 `dji-sdk/Payload-SDK`
