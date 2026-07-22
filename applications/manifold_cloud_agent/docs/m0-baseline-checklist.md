# M0 基线采集清单

本清单对应实施计划的 M0“能力验证与安全契约冻结”。本步骤仅采集版本与环境证据：**不安装应用、不调用 PSDK、不执行任何飞行、云台或相机控制操作**。

请在计划用于联调的同一套 Manifold 3、M4TD、遥控器/机场与相机组合上完成采集。不要记录设备证书、私钥、token、签名 URL、序列号或其他凭据；序列号如需关联，请使用项目内约定的脱敏设备标识。

## M0-BASELINE-001：固定联调基线

### 1. 妙算 3 Linux 环境

在妙算终端执行以下只读命令，并保存输出：

```bash
uname -a
cat /etc/os-release
getconf LONG_BIT
```

记录：

| 项目 | 结果 |
|---|---|
| 采集时间（含时区） | 2026年7月22日13点13分 UTC+8 |
| 妙算型号/硬件版本 | Manifold 3 / 17.00.0101 |
| Linux 内核 | 5.10.192-tegra |
| 发行版/系统版本 | Ubuntu 20.04.6 LTS (Focal Fossa) |
| CPU 架构（预期 aarch64 / 64 位） | aarch64 / 64 位 |

命令执行记录

```
uname -a
Linux tegra-ubuntu 5.10.192-tegra #1 SMP PREEMPT Mon Mar 9 16:12:41 CST 2026 aarch64 aarch64 aarch64 GNU/Linux

cat /etc/os-release
NAME="Ubuntu"
VERSION="20.04.6 LTS (Focal Fossa)"
ID=ubuntu
ID_LIKE=debian
PRETTY_NAME="Ubuntu 20.04.6 LTS"
VERSION_ID="20.04"
HOME_URL="https://www.ubuntu.com/"
SUPPORT_URL="https://help.ubuntu.com/"
BUG_REPORT_URL="https://bugs.launchpad.net/ubuntu/"
PRIVACY_POLICY_URL="https://www.ubuntu.com/legal/terms-and-policies/privacy-policy"
VERSION_CODENAME=focal
UBUNTU_CODENAME=focal

getconf LONG_BIT
64
```

### 2. DJI 设备与软件组合

从 DJI Pilot 2、DJI Assistant 或已批准的设备信息界面读取；不要依赖记忆填写。

| 项目 | 结果 |
|---|---|
| 飞行器型号（本期目标 M4T） | Matrice 4T |
| 飞行器固件版本 | 17.02.0501 |
| 遥控器/机场型号及固件版本 | 01.64.0820(遥控器) |
| DJI Pilot 2 版本 | 17.2.5.28 |
| 可见光/红外相机与挂载固件版本 | 内置相机，无独立版本 |
| 联调时是否接入机场（是/否） | 否 |

### 3. PSDK 工程基线

本仓库当前计划基线是 Payload SDK `3.16.0 (build 2338)`。请确认妙算端实际安装/运行的 PSDK Demo（如有）与下列工程版本不存在未记录差异。

| 项目 | 结果 |
|---|---|
| 本仓库 commit（`git rev-parse --short HEAD`） | 没问题 |
| `psdk_lib/include/dji_version.h` 中的 SDK 版本 | 没问题 |
| 妙算端已安装 PSDK 应用名称/版本（如有） | 没问题 |
| 用户 App ID 已可用于联调（是/否；不填写 ID） | 是 |

### 4. M0 结论

- [x] 上述 M4T 组合已固定，后续 HIL 结果均可关联到此基线；M4TD 不在本期已验证范围内。
- [x] 已确认本次采集没有执行飞行、云台、相机或存储写入操作。
- [x] 若任一版本变更，将新建一条基线记录，并使旧 HIL 证据失效，直到重新验证。

## 交付给 Codex 的信息

请发送第 1～3 节的表格结果或等价终端输出/截图，并说明是否有任何版本无法读取。收到后，我会：

1. 将验证结果追加至 `progress.md`；
2. 把该硬件组合写入应用的受控 M4TD Profile 输入；
3. 准备下一小步：只读 PSDK 生命周期与 AircraftInfo 探针。
