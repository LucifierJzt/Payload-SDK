# 妙算 3 Linux 应用开发指南

这份文档面向的目标是：

- 你要在妙算 3 上开发应用
- 你的主战场是板载 Linux 用户态
- 你需要一份比 “PSDK quickstart” 更贴近系统工程的落地说明

它不讨论妙算底层固件、内核和系统镜像定制，而是聚焦：

- 本机怎么准备开发环境
- 应用怎么编译成 `aarch64` 可执行文件
- 应用怎么部署到妙算 3
- 什么时候该用 `systemd`
- 什么时候该接入 PSDK
- 你的项目目录应该怎么摆

## 1. 先定边界

妙算 3 开发，至少有三层：

### Linux 应用层

这是你的普通业务程序，负责：

- AI 推理
- 视频处理
- 任务编排
- 网络通信
- 数据落盘
- 本地服务

### PSDK 接入层

如果你的程序要和飞机生态打通，才需要这一层。它负责：

- 和飞机通信
- 负载身份认证
- 负载能力接入
- Pilot / 应用管理链路
- `.dpk` 打包和安装

### 系统层

这是妙算设备自身提供的运行环境，负责：

- Linux 用户空间
- 驱动和设备节点
- 网络
- 存储
- 进程管理
- 启动流程

一句话：

先把你的业务当成“Linux 应用”做干净，再决定是否需要“PSDK 接入”。

## 2. 建议的开发路径

我建议按这个顺序，而不是一上来就改官方 sample：

1. 先做一个最小的 `aarch64` Linux 应用。
2. 先把本地交叉编译链路跑通。
3. 先把部署和日志链路跑通。
4. 再决定这个程序是不是要接入飞机能力。
5. 只有在确实需要和 DJI 生态联动时，再引入 PSDK。

这条路径更稳，因为你先解决“妙算上能跑”，再解决“和飞机怎么连”。

## 3. 本机开发环境

从当前仓库看，妙算 3 的 sample 直接假设你有下面这些工具：

- `cmake`
- `make`
- `python3`
- `aarch64-linux-gnu-gcc`
- `aarch64-linux-gnu-g++`

仓库中的相关位置：

- [CMakeLists.txt](/Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK/CMakeLists.txt)
- [samples/sample_c/platform/linux/manifold3/CMakeLists.txt](/Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK/samples/sample_c/platform/linux/manifold3/CMakeLists.txt)
- [samples/sample_c++/platform/linux/manifold3/CMakeLists.txt](/Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK/samples/sample_c++/platform/linux/manifold3/CMakeLists.txt)

这两个 `manifold3` CMake 文件都写死了：

```cmake
set(CMAKE_C_COMPILER "aarch64-linux-gnu-gcc")
set(CMAKE_CXX_COMPILER "aarch64-linux-gnu-g++")
```

所以你如果连普通 `aarch64` 程序都还没开始写，先别急着进 PSDK sample，先确认交叉编译器能正常工作。

## 4. 推荐的项目结构

如果是长期项目，不建议把业务逻辑直接堆进 DJI 官方 sample。

推荐结构：

```text
manifold-app/
├── app/
│   ├── main.cpp
│   ├── services/
│   ├── pipeline/
│   ├── inference/
│   └── transport/
├── config/
│   ├── app.yaml
│   └── logging.yaml
├── scripts/
│   ├── deploy.sh
│   ├── run.sh
│   └── install-service.sh
├── packaging/
│   ├── systemd/
│   │   └── manifold-app.service
│   └── dpk/
│       └── app.json
├── third_party/
└── vendor/
    └── Payload-SDK/
```

这套结构的好处是：

- Linux 应用和 PSDK 集成边界清楚
- `systemd` 包装和 `.dpk` 包装都能共存
- 以后迁移到别的板子也不会被 DJI sample 目录结构绑死

## 5. 第一阶段：先做普通 Linux 应用

先定目标：

- 编译出一个 `aarch64` ELF
- 部署到妙算
- 在设备上跑起来
- 能看日志
- 能重启恢复

这个阶段不要混入太多 PSDK 逻辑。

### 一个最小 `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.16)
project(manifold_app CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(manifold_app
    app/main.cpp
)
```

### 一个最小 `toolchain` 思路

如果你自己的项目不想把编译器写死在主 `CMakeLists.txt`，更建议独立一个 toolchain 文件，例如：

```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
```

然后构建时这样用：

```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake
cmake --build build -j
```

这比把编译器硬编码进主工程更容易维护。

## 6. 部署方式

对妙算上的普通 Linux 应用，我建议优先准备两条部署路径。

### 路径 A：手动部署

适合最早期调试：

1. 本机交叉编译出二进制。
2. 把二进制和配置目录拷到妙算。
3. 登录妙算，直接运行。

建议部署目录类似这样：

```text
/opt/manifold-app/
├── bin/
│   └── manifold_app
├── config/
├── logs/
└── data/
```

### 路径 B：脚本化部署

适合持续开发：

- 一键上传新二进制
- 一键同步配置
- 一键重启服务

建议你自己项目里最少有一个 `scripts/deploy.sh`，做这几件事：

- 创建目录
- 上传 `bin/`
- 上传 `config/`
- 设置执行权限
- 可选重启服务

## 7. `systemd` 什么时候该上

如果你的程序满足这些条件，应该尽早上 `systemd`：

- 这是普通 Linux 后台服务
- 希望设备重启后自动恢复
- 希望进程崩溃后自动拉起
- 希望把日志纳入系统服务管理
- 不打算把它作为 DJI 应用包来安装分发

如果同时满足下面这些，优先考虑 `.dpk`，不要先上 `systemd`：

- 需要接 PSDK
- 需要通过 DJI 应用管理安装
- 需要版本校验和应用身份
- 需要在 Pilot / 应用管理界面被识别

## 8. 一个可直接改的 `systemd` 模板

下面这个模板适合“普通妙算 Linux 服务”：

```ini
[Unit]
Description=Manifold App
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
WorkingDirectory=/opt/manifold-app
ExecStart=/opt/manifold-app/bin/manifold_app --config /opt/manifold-app/config/app.yaml
Restart=always
RestartSec=2
User=root
Environment=APP_ENV=prod

[Install]
WantedBy=multi-user.target
```

建议文件名：

- `/etc/systemd/system/manifold-app.service`

安装步骤通常是：

```bash
sudo systemctl daemon-reload
sudo systemctl enable manifold-app.service
sudo systemctl start manifold-app.service
sudo systemctl status manifold-app.service
```

查看日志：

```bash
journalctl -u manifold-app.service -f
```

## 9. `systemd` 和应用内日志的分工

别把所有日志策略都扔给 `systemd`。

更合理的做法：

- `systemd` 负责进程拉起、保活、启动顺序
- 应用自己负责结构化日志、业务日志级别、日志切分

最差的方案是：

- 既没有应用日志目录
- 也没有 `journalctl` 可读输出
- 崩了之后只能靠猜

## 10. 第二阶段：什么时候接入 PSDK

只有当你确实需要下面这些能力时，再把 `Payload-SDK` 接进来：

- 飞机状态获取
- 负载通信
- 数据透传
- 云台/相机/负载控制
- Pilot 侧交互
- 应用包交付

如果你只是做：

- 本地图像处理
- 本地 AI 推理
- 普通网络服务
- 一般 IPC/串口/USB 外设管理

那一开始并不需要 PSDK。

## 11. 接入 PSDK 后，角色会发生什么变化

一旦接入 PSDK，你的应用就不再只是“普通 Linux 进程”。

你还要处理这些额外要求：

- 应用身份信息
- `USER_APP_ID / KEY / LICENSE`
- `app.json`
- `.dpk` 打包
- 与 DJI 应用管理的一致性

当前仓库里已经把这条链路准备好了，关键文件是：

- [samples/sample_c/platform/linux/manifold3/application/dji_sdk_app_info.h](/Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK/samples/sample_c/platform/linux/manifold3/application/dji_sdk_app_info.h)
- [samples/sample_c/platform/linux/manifold3/application/main.c](/Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK/samples/sample_c/platform/linux/manifold3/application/main.c)
- [samples/sample_c/platform/linux/manifold3/app_json/app.json](/Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK/samples/sample_c/platform/linux/manifold3/app_json/app.json)
- [tools/build_dpk/build_dpk.sh](/Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK/tools/build_dpk/build_dpk.sh)

## 12. 普通 Linux 服务和 PSDK 应用，怎么选

可以用这个判断：

### 选普通 Linux 服务

满足大多数这些条件：

- 不依赖 DJI 应用管理
- 不需要 Pilot 展示
- 不需要 `.dpk` 安装包
- 只关心系统启动、自恢复、日志

### 选 PSDK 应用

满足大多数这些条件：

- 需要接飞机
- 需要接负载能力
- 需要 DJI 应用身份
- 需要应用包安装和版本控制
- 需要被平台正式识别

### 两者都要

这是最现实的情况。

你可以这样拆：

- 一个普通 Linux 守护进程做 AI / 管道 / 数据服务
- 一个薄的 PSDK 接入层进程做 DJI 生态对接

两者通过：

- Unix socket
- TCP localhost
- 共享内存
- 文件队列

来通信。

这种拆法通常比“把所有事情都塞进 PSDK sample 主进程”更稳。

## 13. 和当前仓库的关系

当前仓库适合你做三件事：

1. 验证妙算 3 的 `aarch64` 交叉编译链路。
2. 学 PSDK 应用的接入方式。
3. 学 `.dpk` 打包结构和应用元数据约束。

当前仓库不适合直接承担：

1. 你的完整业务工程结构。
2. 你的系统服务交付规范。
3. 你的长期部署脚本和运维策略。

所以更合理的姿势是：

- 把它当 `vendor/` 或参考实现
- 不把它直接当最终产品代码仓库

## 14. 推荐的实际落地顺序

如果我是现在开始做这个项目，我会按下面顺序推进：

1. 在你自己的仓库里建一个最小 `aarch64` 应用。
2. 用 toolchain 文件把交叉编译链路跑通。
3. 写一个最小 `deploy.sh`。
4. 在妙算上手动运行成功。
5. 补 `systemd` 服务，做到掉电重启可恢复。
6. 再评估是否需要接 PSDK。
7. 如果需要，再把当前这个 `Payload-SDK` 以 `vendor/` 形式接进去。
8. 最后再决定是不是要产出 `.dpk`。

## 15. 你当前最该先做的事

如果你还没有自己的妙算应用仓库，最该先做的是：

1. 新建一个独立项目，不要直接改 DJI sample 作为主仓库。
2. 先做最小 `hello + config + log + service`。
3. 先打通 `build -> deploy -> run -> restart -> log`。
4. 再决定是否接入 PSDK。

如果你已经明确要接飞机生态，那再回头看这两份文档：

- [README-manifold3-system-dev.zh-CN.md](/Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK/README-manifold3-system-dev.zh-CN.md)
- [README-manifold3-quickstart.zh-CN.md](/Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK/README-manifold3-quickstart.zh-CN.md)

## 16. 参考资料

- DJI PSDK 开发准备：
  [https://developer.dji.com/doc/payload-sdk-tutorial/cn/development-preparation/psdk-development-introduction.html](https://developer.dji.com/doc/payload-sdk-tutorial/cn/development-preparation/psdk-development-introduction.html)
- DJI Manifold 3 PSDK 工具说明：
  [https://developer.dji.com/doc/payload-sdk-tutorial/cn/quick-start/manifold-psdk-app/tools.html](https://developer.dji.com/doc/payload-sdk-tutorial/cn/quick-start/manifold-psdk-app/tools.html)

## 17. 一句话结论

做妙算开发时，先把它当“`aarch64 Linux 应用平台`”来建设工程和部署链路；只有在确实需要接入 DJI 飞机生态时，再把 `Payload-SDK` 和 `.dpk` 这套东西接进来。
