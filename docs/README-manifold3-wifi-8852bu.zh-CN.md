# 妙算 3（Manifold 3）外置 Wi‑Fi 配置说明

> 文档索引：[docs/README.md](docs/README.md) · 组网摘要：[docs/02-network.md](docs/02-network.md)

本文说明如何通过 **RTL8852BU / 8852BU1** 等 USB 无线网卡，在妙算 3 上配置并连接 Wi‑Fi。

适用场景：

- 开发调试时用无线网络 SSH 登录妙算
- 设备不方便常接有线网时的日常联网

---

## 1. 背景与兼容性

### 1.1 设备信息示例

从路由器客户端列表中可能看到类似信息：

| 项目 | 示例值 |
|------|--------|
| 主机名 | `tegra-ubuntu` |
| IP（有线/当前网） | `192.168.31.186` |
| MAC | `6C:1F:F7:C9:43:B7` |

实际 IP 以路由器 DHCP 分配为准。

### 1.2 官方支持的 USB Wi‑Fi 芯片

妙算 3 系统侧已适配：

- **RTL8852BU**（含常见成品 **8852BU1** 网卡）
- **RTL88X2BU**

一般插上即可出 `wlan` 接口，**通常不需要自行编译驱动**。请尽量使用较新的妙算系统固件（官方发布记录中已写明对该类网卡的支持）。

### 1.3 默认 SSH 账号

| 项目 | 默认值 |
|------|--------|
| 用户名 | `dji` |
| 密码 | `dji` |

文档依据：DJI Payload SDK / 妙算开发环境说明。若你已改过密码，以实际为准。

---

## 2. 先 SSH 登录妙算

确保电脑与妙算在同一局域网，然后：

```bash
# 连通性
ping 192.168.31.186

# SSH（IP 换成你设备当前地址）
ssh dji@192.168.31.186
```

首次连接确认 host key 时输入 `yes`，再输入密码。

可选：

```bash
# 免密登录
ssh-copy-id dji@192.168.31.186

# 拷贝文件
scp ./your_binary dji@192.168.31.186:~/
```

说明：

- USB 直连场景下，文档常见地址段为 `192.168.42.x`
- 经路由器 DHCP 时，使用路由器分配的地址（如 `192.168.31.186`）

图形桌面（若系统开启 VNC）可尝试：

```text
http://<妙算IP>:6080/vnc.html
```

---

## 3. 硬件连接

1. 将 **8852BU1** 插入妙算 USB‑C 口（可用扩展坞 / OTG；优先 USB3 口与线材）。
2. 确认天线已接好（若网卡带外置天线）。
3. **先保持一条已有网络通路**（有线或已能 SSH 的链路），再在板子上配置 Wi‑Fi，避免“只靠 Wi‑Fi、又还没配好”导致失联。

---

## 4. 确认网卡已被系统识别

SSH 进入妙算后执行：

```bash
# USB 设备列表（应能看到 Realtek 相关设备）
lsusb
lsusb | grep -i realtek

# 网络接口（期望出现 wlan0 或类似名）
ip link
iw dev

# 内核日志（驱动加载 / 报错）
dmesg | tail -50
dmesg | grep -iE '8852|rtw|wlan|usb'
```

期望结果：

- `lsusb` 能看到 Realtek 8852 一类设备
- `ip link` 或 `iw dev` 中存在 **`wlan0`**（也可能是 `wlan1`）

若没有无线接口：

1. 换 USB 口 / 换线再试  
2. 确认妙算固件已升级到支持 8852BU 的版本  
3. 重启：`sudo reboot` 后再次检查  

---

## 5. 使用 NetworkManager 连接 Wi‑Fi（推荐）

### 5.1 检查服务与射频

```bash
systemctl status NetworkManager

# 未运行时：
sudo systemctl enable --now NetworkManager

nmcli device status
nmcli radio wifi
sudo nmcli radio wifi on
```

### 5.2 扫描并连接

```bash
# 扫描附近热点
nmcli device wifi list

# 连接（替换 SSID 与密码）
sudo nmcli device wifi connect "你的WiFi名" password "你的WiFi密码"

# 若需指定接口
sudo nmcli device wifi connect "你的WiFi名" password "你的WiFi密码" ifname wlan0
```

### 5.3 验证

```bash
nmcli connection show
nmcli device status
ip addr show wlan0
ip route
ping -c 3 8.8.8.8
ping -c 3 www.baidu.com
hostname -I
```

成功时：

- `wlan0`（或对应接口）有 `inet` 地址  
- 能 ping 通网关/外网  

**重要：** 连上 Wi‑Fi 后，设备 IP 可能变化。请用 `hostname -I` 记录新地址，之后 SSH：

```bash
ssh dji@<WiFi获得的IP>
```

也可在路由器后台按主机名 `tegra-ubuntu` 或 MAC 地址查找客户端。

### 5.4 开机自动连接

`nmcli device wifi connect` 一般会写入连接配置。可再确认：

```bash
nmcli connection show

# 将连接名换成你的 SSID 或 nmcli 显示的名称
sudo nmcli connection modify "你的WiFi名" connection.autoconnect yes
```

重启验证：

```bash
sudo reboot
# 等待启动后，用 Wi-Fi IP 重新 SSH
```

### 5.5 DNS 异常时

```bash
sudo nmcli connection modify "你的WiFi名" ipv4.dns "8.8.8.8 223.5.5.5"
sudo nmcli connection modify "你的WiFi名" ipv4.ignore-auto-dns yes
sudo nmcli connection up "你的WiFi名"
cat /etc/resolv.conf
```

---

## 6. 备用方案：wpa_supplicant

仅在没有 NetworkManager / `nmcli` 不可用时使用。

```bash
which nmcli

sudo ip link set wlan0 up
sudo wpa_passphrase "你的WiFi名" "你的WiFi密码" \
  | sudo tee /etc/wpa_supplicant/wpa_supplicant-wlan0.conf

sudo wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant/wpa_supplicant-wlan0.conf
sudo dhclient wlan0
# 或：sudo dhcpcd wlan0

ip addr show wlan0
```

有 `nmcli` 时优先用第 5 节，不必走本节。

---

## 7. 推荐最短操作流程

在已能 SSH 的前提下，于妙算上依次执行：

```bash
lsusb
ip link
nmcli device status
sudo nmcli radio wifi on
nmcli device wifi list
sudo nmcli device wifi connect "你的WiFi名" password "你的密码"
ip addr show wlan0
hostname -I
```

记录 `hostname -I` 输出的地址，之后用该 IP SSH。

---

## 8. 排障表

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| `ping` 不通设备 | 不在同一网段 / AP 隔离 | 检查电脑 IP；关闭路由器客户端隔离 |
| 无 `wlan0` | 未识别网卡 / 固件过旧 | 换口换线；升级固件；查 `dmesg` |
| 扫描列表为空 | 天线、频段、距离 | 检查天线；确认 2.4G/5G 已开启 |
| 已连接但无外网 | 路由/DNS | 查 `ip route`、`resolv.conf`；手动设 DNS |
| SSH 突然断线 | 切换网络后 IP 变化 | 路由器查新 IP；或固定 DHCP 绑定 |
| `sudo` 要密码 | 默认账户 | 尝试 `dji` / `dji`（若已修改则用新密码） |

排障时可收集并保存下列输出：

```bash
lsusb
ip link
nmcli device status
nmcli device wifi list
dmesg | tail -80
```

---

## 9. 与本仓库开发路径的关系

本仓库（PSDK 工作区）默认面向：

- **Track A**：妙算 3 上的普通 Linux 用户态应用  
- **Track B**：需飞机/载荷能力时的 PSDK 应用  

Wi‑Fi 仅解决 **设备联网与 SSH/SCP 部署通道**，不替代：

- PSDK 的 `.dpk` 安装与 Pilot 应用管理  
- 系统镜像 / 内核级定制（本仓库不含 BSP 全量源码）

部署普通 Linux 程序时，典型流程仍是：本机交叉编译 → `scp`/`rsync` 到妙算 → 运行或挂 `systemd`。

相关文档：

- [Payload-SDK/README-manifold3-system-dev.zh-CN.md](Payload-SDK/README-manifold3-system-dev.zh-CN.md)
- [Payload-SDK/README-manifold3-linux-app-dev.zh-CN.md](Payload-SDK/README-manifold3-linux-app-dev.zh-CN.md)
- [Payload-SDK/README-manifold3-quickstart.zh-CN.md](Payload-SDK/README-manifold3-quickstart.zh-CN.md)

---

## 10. 参考

- DJI Payload SDK 教程：妙算开发环境（默认 SSH：`dji` / `dji`）
- DJI 妙算 3 发布记录：外置设备及网络配置，适配 **RTL8852BU**、**RTL88X2BU**
- 本仓库 `Payload-SDK/README.md` 中对 Manifold 3 外置网卡支持的说明

---

## 修订说明

| 日期 | 说明 |
|------|------|
| 2026-07-16 | 初版：SSH 登录 + 8852BU1 / NetworkManager 配网流程 |
