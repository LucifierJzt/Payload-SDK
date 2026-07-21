# Manifold Cloud Agent

`manifold_cloud_agent` 是面向 DJI Manifold 的生产级 PSDK 应用。

应用的预期职责如下：

- 与云服务端建立并保持连接，获取应用配置；
- 连接配置指定的 WebSocket 服务端，并收发消息；
- 校验和分发 WebSocket 控制指令，调用相应的无人机设备控制能力。

本应用与 SDK 示例工程保持隔离。实现、部署和运维相关说明位于相邻的 [`docs`](docs/README.md) 目录。
