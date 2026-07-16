# PSDK 妙算 3 启动说明

这份 README 面向「把 Payload SDK 应用跑在妙算 3（Manifold 3）上」的场景。

如果你说的“`一盒启动`”是指：

- 应用不是在 PC 上手工执行
- 而是作为妙算 3 应用被安装到设备里
- 由 DJI 的应用管理机制启动和管理

那么正确路径不是自己写 `systemd` 服务，而是：

1. 基于 PSDK 的 `manifold3` 平台样例编译可执行文件。
2. 用 `app.json` 把可执行文件打成 `.dpk` 安装包。
3. 通过 DJI Pilot 2 / Manifold 3 的应用管理安装该 `.dpk`。
4. 由系统侧应用框架负责启动、停止、升级和版本校验。

## 1. 先理解目录

这个仓库里和妙算 3 直接相关的文件主要在这里：

- [CMakeLists.txt](/Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK/CMakeLists.txt)
- [samples/sample_c/platform/linux/manifold3/CMakeLists.txt](/Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK/samples/sample_c/platform/linux/manifold3/CMakeLists.txt)
- [samples/sample_c/platform/linux/manifold3/application/dji_sdk_app_info.h](/Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK/samples/sample_c/platform/linux/manifold3/application/dji_sdk_app_info.h)
- [samples/sample_c/platform/linux/manifold3/application/main.c](/Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK/samples/sample_c/platform/linux/manifold3/application/main.c)
- [samples/sample_c/platform/linux/manifold3/app_json/app.json](/Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK/samples/sample_c/platform/linux/manifold3/app_json/app.json)
- [samples/sample_c/platform/linux/manifold3/app_json/README.md](/Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK/samples/sample_c/platform/linux/manifold3/app_json/README.md)
- [tools/build_dpk/build_dpk.sh](/Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK/tools/build_dpk/build_dpk.sh)

其中：

- `dji_sdk_app_info.h` 放 DJI 开发者后台申请到的应用身份信息。
- `main.c` 里设置运行时版本号、别名、序列号，并启动 PSDK 应用。
- `app.json` 定义妙算 3 应用安装包元数据。
- `build_dpk.sh` 负责把 `app.json + bin + userconfig` 打成 `.dpk`。

## 2. “启动”在妙算 3 上到底是什么意思

在妙算 3 上，推荐的上线形态不是“拷一个 ELF 到板子上然后手动运行”，而是“生成 `.dpk` 应用包并安装到设备”。

这条链路有几个关键点：

- `app.json` 是生成 `.dpk` 的必要配置文件。
- `app.json` 里的 `bin` 指向你的可执行文件相对路径。
- `user_app_id` 必须和代码里的 `USER_APP_ID` 一致，否则应用无法安装。
- `firmware_version` 必须和代码里设置的版本一致，否则应用无法安装。

这也是为什么“妙算开机启动”通常应该理解成“由 DJI 的应用管理框架启动应用”，而不是额外再做一套 Linux 自启动脚本。

## 3. 开发前要改的内容

### 3.1 填写应用身份

编辑 [samples/sample_c/platform/linux/manifold3/application/dji_sdk_app_info.h](/Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK/samples/sample_c/platform/linux/manifold3/application/dji_sdk_app_info.h)：

```c
#define USER_APP_NAME               "your_app_name"
#define USER_APP_ID                 "your_app_id"
#define USER_APP_KEY                "your_app_key"
#define USER_APP_LICENSE            "your_app_license"
#define USER_DEVELOPER_ACCOUNT      "your_developer_account"
#define USER_BAUD_RATE              "460800"
```

这些值要替换成你在 DJI Developer 用户中心创建应用后拿到的真实参数。

### 3.2 同步 `app.json`

编辑 [samples/sample_c/platform/linux/manifold3/app_json/app.json](/Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK/samples/sample_c/platform/linux/manifold3/app_json/app.json)：

- `user_app_id` 要和 `USER_APP_ID` 一致
- `firmware_version` 要和程序里设置的版本一致
- `name` / `description` / `maintainer` 改成你自己的应用信息
- `bin` 保持指向编译产物
- `userconfig` 放运行时需要读取但未静态打进可执行文件的目录

### 3.3 同步程序内版本号

编辑 [samples/sample_c/platform/linux/manifold3/application/main.c](/Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK/samples/sample_c/platform/linux/manifold3/application/main.c)，这里有一段：

```c
T_DjiFirmwareVersion firmwareVersion = {
    .majorVersion = 1,
    .minorVersion = 0,
    .modifyVersion = 0,
    .debugVersion = 0,
};
```

它要和 `app.json` 的：

```json
"firmware_version": "01.00.00.00"
```

保持一致。

## 4. 编译方式

仓库根目录默认会把 Linux 目标加入构建，并包含 `sample_c/platform/linux/manifold3` 和 `sample_c++/platform/linux/manifold3`。

妙算 3 C 样例的构建脚本里写死了交叉编译器：

```cmake
set(CMAKE_C_COMPILER "aarch64-linux-gnu-gcc")
set(CMAKE_CXX_COMPILER "aarch64-linux-gnu-g++")
```

所以你的开发机上至少要有：

- `cmake`
- `make`
- `python3`
- `aarch64-linux-gnu-gcc`
- `aarch64-linux-gnu-g++`

推荐在仓库根目录这样构建：

```bash
cd /Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK
mkdir -p build
cd build
cmake ..
make -j$(sysctl -n hw.ncpu)
```

构建完成后，通常会生成：

- `build/bin/dji_sdk_demo_on_manifold3`
- `build/dpk/*.dpk`

原因是 [samples/sample_c/platform/linux/manifold3/CMakeLists.txt](/Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK/samples/sample_c/platform/linux/manifold3/CMakeLists.txt) 在 `POST_BUILD` 里自动调用了：

```bash
bash tools/build_dpk/build_dpk.sh -i samples/sample_c/platform/linux/manifold3/app_json/app.json -o build/dpk
```

## 5. `.dpk` 才是妙算 3 的正式启动入口

这里是最容易混淆的地方。

### 调试态

你可以只关心二进制：

- `build/bin/dji_sdk_demo_on_manifold3`

这种方式适合本地联调、日志确认、功能验证。

### 交付态

你应该关心 `.dpk`：

- `build/dpk/*.dpk`

因为妙算 3 的应用安装、版本识别、启动控制，都依赖 `app.json` 定义出来的安装包元数据。

换句话说：

- 手动运行 ELF 是开发手段
- 安装 `.dpk` 才是设备侧正式运行手段

## 6. 上妙算 3 的建议流程

推荐按这个顺序走：

1. 在 DJI Developer 后台创建应用，拿到 `App ID / Key / License`。
2. 修改 `dji_sdk_app_info.h`。
3. 修改 `app.json`，确保 `user_app_id` 和 `firmware_version` 与代码一致。
4. 本地交叉编译。
5. 确认 `build/dpk/` 下已经产出 `.dpk`。
6. 将 `.dpk` 安装到妙算 3。
7. 在 Pilot 2 / Manifold 3 的应用管理界面验证安装、启动、停止和版本信息。

## 7. 如果你要“开机自启动”，建议怎么做

我的建议是分两种情况理解：

### 情况 A：你只是想“设备上的应用能被正常拉起”

那就按官方应用包机制走，也就是 `.dpk + app.json`。
这是最稳的路径，也是和 DJI 平台约束一致的路径。

### 情况 B：你明确要“板子一上电就自动运行我的业务”

优先先确认妙算 3 当前固件和应用管理是否已经提供该应用的自动启动能力。
如果平台侧已经有应用托管入口，就不要再额外叠 `systemd`。

只有当你确认：

- 这是纯 Linux 用户态自管进程
- 不走 DJI 应用管理链路
- 且你接受后续升级、版本、停止、日志都自己维护

才建议单独补 Linux 自启动方案。

对于 PSDK 正式产品化，我不建议把 `systemd` 作为第一方案。

## 8. 常见对不上的地方

### 安装失败

先检查：

- `app.json.user_app_id` 和 `USER_APP_ID` 是否一致
- `app.json.firmware_version` 和代码里的版本号是否一致
- `app.json.bin` 指向的可执行文件是否真实存在
- `app.json.userconfig` 指向的目录是否真实存在

### 编译失败

先检查：

- 是否安装了 `aarch64-linux-gnu-gcc`
- 是否在单独的 `build/` 目录构建
- 是否误做了 in-source build

### 运行起来但功能异常

先检查：

- 串口/端口配置是否和当前挂载方式一致
- 波特率 `USER_BAUD_RATE` 是否符合机型和端口要求
- 机型是否支持你启用的模块

## 9. 这份仓库下一步建议

如果你准备正式做妙算开发，建议下一步直接做这三件事：

1. 把 `dji_sdk_app_info.h` 改成你的真实应用参数。
2. 把 `app_json/app.json` 改成你自己的应用名称、版本和描述。
3. 先把官方 `manifold3` sample 编译出第一个 `.dpk`，确认安装链路打通，再开始改业务逻辑。

## 10. 参考资料

- DJI PSDK 开发准备说明：
  [https://developer.dji.com/doc/payload-sdk-tutorial/cn/development-preparation/psdk-development-introduction.html](https://developer.dji.com/doc/payload-sdk-tutorial/cn/development-preparation/psdk-development-introduction.html)
- DJI Manifold 3 工具链与应用打包说明：
  [https://developer.dji.com/doc/payload-sdk-tutorial/cn/quick-start/manifold-psdk-app/tools.html](https://developer.dji.com/doc/payload-sdk-tutorial/cn/quick-start/manifold-psdk-app/tools.html)
- 本仓库的 `app.json` 说明：
  [samples/sample_c/platform/linux/manifold3/app_json/README.md](/Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK/samples/sample_c/platform/linux/manifold3/app_json/README.md)

## 11. 一句话结论

妙算 3 上的 PSDK 应用，正确的“启动方式”是：

`源码 -> 交叉编译 -> 生成 bin -> 生成 .dpk -> 安装到妙算 3 -> 由应用管理框架启动`

不是：

`源码 -> 编译 -> 手写 systemd -> 直接当普通 Linux 守护进程跑`
