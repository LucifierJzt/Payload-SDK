# 妙算 3 / PSDK 工作区

面向 **Manifold 3 + M4T** 的 PSDK 应用开发与云端联调。

## 快速入口

| 文档 | 说明 |
|------|------|
| **[docs/](docs/README.md)** | **文档总索引（从这里开始）** |
| [docs/03-ws-cloud-integration.md](docs/03-ws-cloud-integration.md) | 云端 WS 协议、云台、直播 RTMP |
| [docs/04-deploy-dpk.md](docs/04-deploy-dpk.md) | 编译 / `.dpk` / 安装 |
| [AGENTS.md](AGENTS.md) | 开发轨道与边界（给 Agent / 协作者） |
| [Manifold3-M4TD-开发步骤.md](Manifold3-M4TD-开发步骤.md) | 过程记录与检查表 |

## 目录

```text
PSDK/
├── docs/                 # 整理后的项目文档
├── Payload-SDK/          # DJI PSDK（软链或克隆；业务 fork 见下）
├── manifold-ws-hub/      # 可选 Node WS 汇聚参考实现
├── tools/                # aarch64 交叉编译工具链等
└── README-manifold3-wifi-8852bu.zh-CN.md
```

## 代码仓库

- Fork：https://github.com/LucifierJzt/Payload-SDK  
- 上游参考：https://github.com/dji-sdk/Payload-SDK  
- 核心模块：`Payload-SDK/samples/sample_c/module_sample/ws_telemetry/`

## 当前默认云端（以配置文件为准）

以 `ws_telemetry_config.h` 为准，联调时常见：

| 项 | 示例 |
|----|------|
| WS | `ws://1.13.253.227:6791/manifold-ws` |
| RTMP | `rtmp://121.40.203.74/live/manifold` |
| SSH | `dji@<IP>` / `dji` |

修改配置后需重新编译打包安装，见 [docs/04-deploy-dpk.md](docs/04-deploy-dpk.md)。
