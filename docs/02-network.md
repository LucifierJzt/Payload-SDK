# 组网：SSH、Wi‑Fi、外置模块

## SSH 默认账号

| 项 | 值 |
|----|-----|
| 用户 | `dji` |
| 密码 | `dji` |
| 主机名 | 常见 `tegra-ubuntu` |

```bash
ssh dji@<妙算IP>
```

IP 以路由器客户端列表或 `hostname -I` 为准（示例曾用 `192.168.31.186`）。

## 组网方式对比

| 方式 | 适用 | 备注 |
|------|------|------|
| 有线 `eth0` | 台架开发 | 最稳；上飞机后通常断开 |
| USB 网卡 8852BU1 | 无线上网 | 官方适配 RTL8852BU；`wlan0` 需 NM 写 PSK，**dji 用户可能无 network-control 权限** |
| 外置 RNDIS / 路由 SoC | 机载旁路 | 模组自己连 Wi‑Fi，妙算当有线 |
| 自有 5G 模块 | 机载公网 | 优先 RNDIS/网卡形态，避免需 root 拨号 |
| DJI 机载蜂窝 | 官方链路 | PSDK「机载网络」文档 |

8852 详细踩坑（扫描可以、psk 写不进、sudo 白名单等）：

→ [README-manifold3-wifi-8852bu.zh-CN.md](../README-manifold3-wifi-8852bu.zh-CN.md)

## 官方配网脚本

文档：`netctl.sh`（scan_wifi / connect_wifi）。  
若复用旧连接且 `psk: --`，会 `Secrets were required, but not provided`。  
`dji` 常无法 `nmcli connection delete`。

## 机载断网

台架有线通、上飞机断：正常（无实验室网线）。机载需 4G/外置模块/预装离线任务。

## 应用管理

```bash
dji_app_ctl install -i xxx.dpk   # 用包文件
dji_app_ctl list                 # 看应用名
dji_app_ctl start PSDK           # 用 name，不是 dpk 文件名
dji_app_ctl stop PSDK
dji_app_ctl status
```
