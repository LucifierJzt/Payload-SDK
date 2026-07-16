# 妙算 3 + M4TD 开发步骤记录

> 目标：在 **妙算 3（Manifold 3）** 上开发 PSDK App，用于控制 **M4TD（Matrice 4TD）**。  
> 工作区：`/Volumes/disk_out/Users/lucifier/workspace/PSDK`  
> SDK 路径：`Payload-SDK/`（当前约 **V3.16.0**）  
> 官方文档入口：  
> https://developer.dji.com/doc/payload-sdk-tutorial/cn/manifold-quick-start/manifold-development-introduction/manifold-product-overview.html

---

## 0. 先定边界

本项目属于 **Track B：PSDK 应用开发**，不是改妙算系统镜像，也不是纯 Linux 守护进程。

| 路径 | 是否适合 | 说明 |
|------|----------|------|
| **PSDK App（`.dpk`）** | ✅ 适合 | 要控制飞机、走 Pilot 应用管理、安装/版本/启动 |
| 普通 Linux 服务（`systemd`） | ❌ 不作为主交付 | 只适合与飞机无关的本地服务 |
| 改妙算系统镜像/内核/BSP | ❌ 本仓库做不了 | 需要系统源码与官方系统开发支持 |

**结论：**

- 正式交付走：`app.json` → 编译 → `build_dpk.sh` → `.dpk` → Pilot / 应用管理安装启动
- 不要默认用 `systemd` 托管正式 PSDK App
- 长期业务代码不要硬塞进官方 sample，建议独立工程目录

推荐长期结构：

```text
workspace/
├── AGENTS.md
├── Manifold3-M4TD-开发步骤.md   # 本文档
├── Payload-SDK/                # 上游依赖，尽量保持接近官方
└── m4td-control-app/           # 后续业务工程（待建）
```

本地参考文档（推荐阅读顺序）：

1. **[docs/README.md](docs/README.md)**（项目文档总索引）
2. **[docs/03-ws-cloud-integration.md](docs/03-ws-cloud-integration.md)**（云端 WS / 云台 / 直播）
3. [docs/04-deploy-dpk.md](docs/04-deploy-dpk.md)
4. `Payload-SDK/README-manifold3-system-dev.zh-CN.md` 等上游说明

代码仓库 fork：https://github.com/LucifierJzt/Payload-SDK

---

## 1. 当前状态盘点

| 状态 | 项 | 说明 |
|------|----|------|
| ✅ 已完成 | 下载 PSDK | `Payload-SDK/`（可软链至 Desktop） |
| ✅ 已完成 | 开发者后台创建 App | App Name=PSDK，APP ID=190335 |
| ✅ 已完成 | 填写应用身份 | `dji_sdk_app_info.h` |
| ✅ 已完成 | 交叉编译工具链 | `tools/` aarch64 + cmake |
| ✅ 已完成 | 编译与 `.dpk` | `PSDK_v01.00.00.01.dpk` 等 |
| ✅ 已完成 | 上机安装启动 | `dji_app_ctl install/start PSDK` |
| ✅ 已完成 | 云端 WS 遥测 | `ws_telemetry`，机型 M4T，1Hz |
| ✅ 已完成 | WS 双向指令 | ping / 云台 / live_*；飞控 dry-run |
| ✅ 已完成 | 云台控制 | `gimbal_rotate/reset/mode` + 遥测角度 |
| ✅ 已完成 | Liveview + RTMP | `live_start` → `rtmp://121.40.203.74/live/manifold` |
| ⏳ 可选 | 真飞控 | 设 `WS_TELEMETRY_FLIGHT_DRY_RUN=0` 后室外测 |
| ⏳ 可选 | 业务独立工程 | 长期宜迁出 sample 进 `your-manifold-app/` |

关键文件：

| 文件 | 作用 |
|------|------|
| `docs/03-ws-cloud-integration.md` | **WS 协议与指令完整说明** |
| `module_sample/ws_telemetry/*` | WS 客户端 + Liveview |
| `platform/linux/manifold3/application/*` | App 身份、config、main |
| `platform/linux/manifold3/app_json/app.json` | 打包元数据 |
| `tools/build_dpk/build_dpk.sh` | 打 `.dpk`（含 macOS portable） |
| `psdk_lib/include/dji_flight_controller.h` | 飞控 API |
| `module_sample/flight_control/` | 官方飞控 sample |
| `module_sample/fc_subscription/` | 飞控状态订阅 sample |

---

## 2. 开发步骤（按顺序，不要跳）

### 第 1 步：DJI 开发者后台创建应用

1. 打开：https://developer.dji.com/user/apps/#all
2. 创建 **Payload SDK** 应用
3. 拿到并妥善保存：
   - App Name
   - App ID
   - App Key
   - App License
   - Developer Account
4. 填入：

```text
Payload-SDK/samples/sample_c/platform/linux/manifold3/application/dji_sdk_app_info.h
```

模板字段：

```c
#define USER_APP_NAME               "your_app_name"
#define USER_APP_ID                 "your_app_id"
#define USER_APP_KEY                "your_app_key"
#define USER_APP_LICENSE            "your_app_license"
#define USER_DEVELOPER_ACCOUNT      "your_developer_account"
#define USER_BAUD_RATE              "460800"
```

5. 同步修改：

```text
Payload-SDK/samples/sample_c/platform/linux/manifold3/app_json/app.json
```

**必须保持一致（否则装不上/启不来）：**

| 代码侧 | `app.json` 侧 |
|--------|----------------|
| `USER_APP_ID` | `user_app_id` |
| `main.c` 中 `firmwareVersion`（如 1.0.0.0） | `firmware_version`（如 `"01.00.00.00"`） |

`main.c` 版本示例：

```c
T_DjiFirmwareVersion firmwareVersion = {
    .majorVersion = 1,
    .minorVersion = 0,
    .modifyVersion = 0,
    .debugVersion = 0,
};
```

对应：

```json
"firmware_version": "01.00.00.00"
```

**完成标准：**

- [x] 密钥已填入 `dji_sdk_app_info.h`（C + C++ manifold3）
- [x] `user_app_id` 与 `USER_APP_ID` 一致（`190335`）
- [x] `firmware_version` 与代码版本一致（`01.00.00.00`）
- [x] `name` / `description` / `maintainer` 已改成自己的信息
- [x] `USER_DEVELOPER_ACCOUNT` 已填写

---

### 第 2 步：准备本机交叉编译环境

妙算 3 sample 写死了：

```cmake
set(CMAKE_C_COMPILER "aarch64-linux-gnu-gcc")
set(CMAKE_CXX_COMPILER "aarch64-linux-gnu-g++")
```

至少需要：

- `cmake`
- `make`
- `python3`
- `aarch64-linux-gnu-gcc`
- `aarch64-linux-gnu-g++`

macOS 上建议：

```bash
# cmake
brew install cmake

# aarch64 交叉工具链：更推荐 Ubuntu 虚拟机 / Docker（最稳）
# 本机硬配交叉链也可，但维护成本更高
```

验证：

```bash
which aarch64-linux-gnu-gcc aarch64-linux-gnu-g++ cmake
aarch64-linux-gnu-gcc --version
cmake --version
```

**完成标准：**

- [ ] `cmake` 可用
- [ ] `aarch64-linux-gnu-gcc` / `g++` 可用
- [ ] 能成功交叉编译一个最小 `hello`（可选但推荐）

---

### 第 3 步：先编译官方 manifold3 sample

**不要先写业务。** 先证明“能编、能出包”。

在 `Payload-SDK` 根目录：

```bash
cd /Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK
mkdir -p build
cd build
cmake ..
make -j$(nproc)   # macOS 可用: make -j$(sysctl -n hw.ncpu)
```

预期产物：

| 产物 | 用途 |
|------|------|
| `build/bin/dji_sdk_demo_on_manifold3` | 调试态：手动运行、看日志 |
| `build/dpk/*.dpk` | 交付态：安装到妙算 3 |

说明：`manifold3` 的 CMake 通常会在 POST_BUILD 里调用 `tools/build_dpk/build_dpk.sh` 自动打 dpk。

**完成标准：**

- [ ] 编译成功，无链接错误
- [ ] 二进制已生成
- [ ] `.dpk` 已生成

---

### 第 4 步：硬件与飞机侧准备（M4TD）

1. 准备 **M4TD + 妙算 3 + 遥控器（Pilot 2）**
2. 按官方文档完成 **E-Port / 线缆** 连接（见官方“硬件环境搭建”）
3. 确认飞机、遥控器、妙算固件版本满足官方兼容表
4. 确认电源、通信链路正常
5. 确认 Pilot 2 能进入应用管理 / 识别妙算相关能力

官方文档路径（同系列）：

- 妙算产品概览
- 硬件环境搭建
- 软件环境搭建
- Quick Demo

**完成标准：**

- [ ] 妙算上电正常
- [ ] 与 M4TD 连接正常
- [ ] Pilot 可操作/可见应用管理入口

---

### 第 5 步：安装并跑通官方 demo

1. 将 `.dpk` 拷到遥控器 / 妙算可访问位置
2. 用 **Pilot 2 → 应用管理** 安装
3. 启动应用，观察日志与行为

关键成功信号：

- `DjiCore_Init` 成功
- 能读到机型 / 版本信息
- 飞机类型识别到 **M4TD**（sample 中已有 `DJI_AIRCRAFT_TYPE_M4TD` 相关分支）

两种运行形态：

| 形态 | 方式 | 用途 |
|------|------|------|
| 调试态 | SSH 上妙算直接跑 ELF | 联调、日志、快速验证 |
| 交付态 | 安装 `.dpk` | 正式运行、版本管理、应用管理 |

**完成标准：**

- [ ] `.dpk` 安装成功
- [ ] 应用可启动 / 可停止
- [ ] Core 初始化成功
- [ ] 能识别 M4TD

---

### 第 6 步：再实现“控制 M4TD”

demo 链路通之后，再加控制能力。建议按最小闭环推进，不要一次全做。

| 优先级 | 能力 | 参考位置 |
|--------|------|----------|
| 1 | 飞控状态订阅（姿态、GPS、电量等） | `samples/sample_c/module_sample/fc_subscription/` |
| 2 | 基础飞控（起飞 / 悬停 / 降落 / 速度控制等） | `samples/sample_c/module_sample/flight_control/` + `dji_flight_controller.h` |
| 3 | 相机 / 云台 | `camera_manager/`、`gimbal_manager/` |
| 4 | 图传 / 视觉 | `liveview/`（C++ sample 更全） |
| 5 | 航点任务 | `waypoint_v3/` |

**建议的第一个控制目标：**

1. 订阅飞行状态，确认数据真的在流
2. 再做起飞 / 悬停 / 降落，或简单速度控制
3. 再扩展相机、云台、任务编排

**安全要求（必须）：**

- 开阔 / 合规试飞区域
- 遥控器随时可接管
- 先低空、低速、单功能验证
- 明确紧急停止与返航路径

**完成标准（最小控制闭环）：**

- [ ] 能稳定订阅关键飞行状态
- [ ] 能安全执行至少一组基础控制指令
- [ ] 有日志可复盘失败原因

---

### 第 7 步：拆出独立业务工程（建议）

官方 sample 适合“先跑通”，不适合长期堆业务。

建议后续结构：

```text
m4td-control-app/
├── app/
│   ├── main.cpp
│   ├── flight/
│   ├── camera/
│   └── services/
├── config/
├── scripts/
│   ├── build.sh
│   └── deploy.sh
├── packaging/
│   └── dpk/
│       └── app.json
└── vendor/
    └── Payload-SDK/   # 或通过路径引用工作区里的 Payload-SDK
```

原则：

- `Payload-SDK/` 当上游依赖
- 业务代码、配置、部署脚本放自己工程
- 需要升级官方 SDK 时，尽量少改上游

---

## 3. 本周可执行清单

| 顺序 | 任务 | 完成 |
|------|------|------|
| 1 | 开发者后台建 App，填写密钥 | [x] |
| 2 | 对齐 `dji_sdk_app_info.h` 与 `app.json` | [x] |
| 3 | 安装 `cmake` + aarch64 工具链（本机 macOS，非 Docker） | [x] |
| 4 | 编译出 `dji_sdk_demo_on_manifold3` 和 `.dpk` | [x] |
| 5 | 硬件接线 + 安装 dpk + 确认 init / 识别 M4TD | [~] 已安装，待确认 init |
| 6 | 基于 `fc_subscription` + `flight_control` 做最小控制 | [ ] |

---

## 4. 常见踩坑

1. **`USER_APP_ID` 与 `app.json.user_app_id` 不一致** → 安装失败  
2. **代码固件版本与 `firmware_version` 不一致** → 安装失败  
3. **未填真实 License / Key** → `DjiCore_Init` 失败  
4. **业务全堆在官方 sample** → 后续难升级、难维护  
5. **没确认固件兼容就联调** → 功能偶发不可用，难排查  
6. **控制飞机前缺少安全接管预案** → 试飞风险高  

---

## 5. 调试态 vs 交付态（别混淆）

| | 调试态 | 交付态 |
|--|--------|--------|
| 产物 | ELF 二进制 | `.dpk` |
| 典型路径 | `build/bin/dji_sdk_demo_on_manifold3` | `build/dpk/*.dpk` |
| 启动方式 | 手动运行 / SSH | Pilot 应用管理 |
| 适用阶段 | 联调、日志、功能验证 | 产品化安装与版本管理 |

一句话：

- 手动跑 ELF 是开发手段  
- 安装 `.dpk` 才是设备侧正式运行手段  

---

## 6. 官方与本地资料索引

### 官方

- 妙算产品概览：  
  https://developer.dji.com/doc/payload-sdk-tutorial/cn/manifold-quick-start/manifold-development-introduction/manifold-product-overview.html
- 开发者应用中心：  
  https://developer.dji.com/user/apps/#all
- PSDK 教程总入口：  
  https://developer.dji.com/doc/payload-sdk-tutorial/cn/

### 本地

- `Payload-SDK/README.md`
- `Payload-SDK/README-manifold3-system-dev.zh-CN.md`
- `Payload-SDK/README-manifold3-linux-app-dev.zh-CN.md`
- `Payload-SDK/README-manifold3-quickstart.zh-CN.md`
- `Payload-SDK/samples/sample_c/platform/linux/manifold3/`
- `Payload-SDK/tools/build_dpk/`

---

## 7. 进度记录（手工更新）

| 日期 | 进度 | 备注 |
|------|------|------|
| 2026-07-13 | 文档建立；SDK 已下载；工具链与 App 身份未配置 | 下一步：后台建 App + 装交叉编译环境 |
| 2026-07-14 | 已填 App 身份（ID 190335，Status accepted） | 仍缺 `USER_DEVELOPER_ACCOUNT` 邮箱；下一步：补邮箱 + 交叉编译环境 |
| 2026-07-14 | 已补 `USER_DEVELOPER_ACCOUNT` | 应用身份完整；下一步：交叉编译环境 + 编译 `.dpk` |
| 2026-07-14 | 本机交叉编译环境就绪并编过 C demo | 产物见 `Payload-SDK/build/`；下一步：上机安装联调 |
| 2026-07-14 | `.dpk` 已成功安装到妙算 3 | 下一步：启动应用，确认 DjiCore_Init / 机型识别 |

---

## 8. 已写入的应用身份（来自开发者后台截图，2026-07-14）

| 字段 | 值 |
|------|-----|
| SDK Type | Payload SDK-Manifold 3 |
| App Name | PSDK |
| APP ID | 190335 |
| App Key | `da67517ebe2dcbf37c0aac5da87ccfe` |
| App Advanced License | `SSobvmgORjT3OQIEHSjTWm2N2VjpmmzWw1dbHig/pdbRGDaUfVHT3CG0COfP/Tt+PxdhsdJH87Hg9s82z8I4KA==` |
| Apply Status | accepted |
| Developer Account | `jinzhentao2000@icloud.com` |

已同步修改：

- `Payload-SDK/samples/sample_c/platform/linux/manifold3/application/dji_sdk_app_info.h`
- `Payload-SDK/samples/sample_c++/platform/linux/manifold3/application/dji_sdk_app_info.h`
- `Payload-SDK/samples/sample_c/platform/linux/manifold3/app_json/app.json`（`user_app_id` = `190335`）

安全提醒：App Key / License 属于密钥，不要提交到公开仓库；若仓库会 push 到远端，考虑用本地未跟踪文件或环境变量注入。

---

## 9. 下一步行动（当前）

按优先级立刻做：

1. 硬件接线（M4TD + 妙算 3）  
2. 将 `PSDK_v01.00.00.00.dpk` 安装到妙算并启动验证  
3. 再做飞控/订阅最小闭环  

### macOS 本机重新编译命令

```bash
source /Volumes/disk_out/Users/lucifier/workspace/PSDK/tools/env.sh
cd /Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK
mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE="$PSDK_TOOLS/cmake-toolchains/aarch64-linux-gnu.cmake"
make -j$(sysctl -n hw.ncpu) dji_sdk_demo_on_manifold3
```

产物：

- `Payload-SDK/build/bin/dji_sdk_demo_on_manifold3`（aarch64 Linux ELF）
- `Payload-SDK/build/dpk/PSDK_v01.00.00.00.dpk`

说明：

- Homebrew bottle 在本机不可用，工具链改为手动下载到 `tools/`
- `build_dpk.sh` 已适配 macOS（无 `mapfile` / 无 `dpkg-deb` 时的可移植打包）
- 根 `CMakeLists.txt` 已兼容 macOS `uname -m=arm64`

后续可选：

- C：整理「M4TD 最小飞控 demo」API 与 sample 裁剪清单
- D：编译 C++ sample（可能依赖 OpenCV/FFmpeg）

### 公网遥测汇聚（已实现）

目录：`manifold-ws-hub/`

- 妙算 / 模拟器：`role=device` 上报 `telemetry`
- 观察端：`role=viewer` 订阅
- 看板：`http://host:8080/`
- 部署说明：见 `manifold-ws-hub/README.md`

