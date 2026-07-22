# Manifold Cloud Agent 实施进度

本文件按 session 追加记录 `manifold_cloud_agent` 的实施过程、验证结果和后续线索。

## 2026-07-22 13:14  PROGRESS-001  initialized
- 意图: 为妙算 3 + PSDK 云控代理的后续实施建立可追溯的进度记录。
- 动作: 新增 `applications/manifold_cloud_agent/docs/progress.md`，定义并写入首条实施日志。
- 产出: 新增本进度文档；尚无提交 hash 或迁移脚本。
- 验证: 已核对架构计划 `applications/manifold_cloud_agent/docs/2026-07-21-初步计划.md`，并确认本文档位于应用专属文档目录。
- 下一步: 依据架构计划确定首个可编译的应用骨架、交叉编译配置及最小 PSDK 生命周期验证。

## 2026-07-22 15:27  M0-BASELINE-001  baseline-captured-with-m4t-deviation
- 意图: 固定首套妙算/飞行器/PSDK 联调基线，作为后续 HIL 证据的关联输入。
- 动作: 新增并由现场填写 `applications/manifold_cloud_agent/docs/m0-baseline-checklist.md`；本地核对仓库提交和 `psdk_lib/include/dji_version.h`。
- 产出: 联调环境为 Ubuntu 20.04.6 LTS、Linux `5.10.192-tegra`、aarch64/64 位；飞行器为 **Matrice 4T**、飞机固件 `17.02.0501`、遥控器固件 `01.64.0820`、DJI Pilot 2 `17.2.5.28`、不接入机场。工程基线为 commit `9134c8a`、PSDK `3.16.0 (build 2338)`，且 App ID 可用于联调；无迁移脚本。
- 验证: 现场已执行 `uname -a`、`cat /etc/os-release`、`getconf LONG_BIT`，结果保存在 M0 清单；本地读取 `dji_version.h` 确认 SDK 宏为 `3.16.0 (build 2338)`。本步骤未调用 PSDK 或任何飞行、云台、相机控制。
- 下一步: 架构计划原定首版目标机型为 M4TD，但实际联调机为 M4T；不得共用生产 capability/Profile。下一步制作仅初始化、读取 AircraftInfo 后退出的 M4T PSDK 生命周期探针，并由现场验证其启动顺序和只读机型/固件信息；M4TD 继续标记为未验证。

## 2026-07-22 15:27  M0-BASELINE-001  accepted-for-m4t
- 意图: 关闭首套联调基线的缺失项，并明确本期机型范围。
- 动作: 更新 `applications/manifold_cloud_agent/docs/m0-baseline-checklist.md`：补充 Manifold 3 硬件版本和内置相机说明；将 M0 结论勾选为完成。
- 产出: 首期联调与实现目标正式确认为 Matrice 4T；Manifold 3 硬件版本为 `17.00.0101`；相机为内置挂载、无独立固件版本。M4TD 暂不属于已验证机型，无迁移脚本。
- 验证: 现场确认机型范围及上述版本信息；M0 清单的三项结论均已满足，且采集阶段未调用 PSDK 或硬件控制。
- 下一步: 实现只读 PSDK 生命周期与 `AircraftInfo` 探针，现场验证 Core 初始化、ApplicationStart、机型/飞机固件读取及安全退出的实际顺序；在探针通过前不初始化飞控、相机、云台或直播模块。

## 2026-07-22 17:26  M0-AIRCRAFT-INFO-001  blocked-by-device-app-verification
- 意图: 在 M4T 上以独立、只读的 PSDK 探针验证 Core 生命周期和 AircraftInfo。
- 动作: 构建并尝试通过 `dji_app_ctl install` 安装多个探针 DPK；核对包内 `Id` 与设备凭据 App ID 一致、包版本与 manifest 一致；最后以历史完整 Demo 包 `PSDK_v01.00.00.01.dpk` 作为对照安装。
- 产出: 探针包和历史 Demo 包均在安装校验阶段失败，错误均为 `verify app user_app_id or version info error`；未形成可安装的 M0 探针，也未执行 AircraftInfo 读取。设备端 `dji_app_ctl list` 仅显示 DJI 内置 `Smart3DExplore`，失败包已回滚。
- 验证: 本机 aarch64 探针构建成功；历史 Demo DPK（`Id=190335`、`Package=psdk`、`Version=01.00.00.01`、`Binary=bin/dji_sdk_demo_on_manifold3`）也复现同一安装失败，排除本应用二进制名、包名和探针实现作为首要原因。应用管理器日志同时出现 DUSS ACK timeout，但未给出更具体的应用校验原因。
- 下一步: 先恢复/确认妙算的 PSDK 应用安装校验链路（DJI App Manager 与飞行器/Pilot 通信、当前机型/固件对该 PSDK App ID 的许可状态）；在任一官方/历史 Demo 能成功安装前，不继续修改或测试 `manifold_cloud_agent` 的 PSDK 代码。M4T 保持停桨、在地，不进行飞行控制测试。

## 2026-07-22 17:37  M0-AIRCRAFT-INFO-001  device-blocker-reconfirmed
- 意图: 用当前工作区重新交叉构建的官方 Manifold 3 Demo 排除历史 DPK 损坏或旧构建路径影响。
- 动作: 在仓库根目录 `build/` 重新配置 aarch64 工具链并构建 `dji_sdk_demo_on_manifold3`，自动生成 `build/dpk/PSDK_v01.00.00.01.dpk`；现场用 `dji_app_ctl install` 安装该新包。
- 产出: 新 Demo DPK 为 `Id=190335`、`Package=psdk`、`Version=01.00.00.01`、`Binary=bin/dji_sdk_demo_on_manifold3`；仍未能安装成功，无新增设备应用。
- 验证: 交叉编译和 DPK 打包成功；设备安装在进度 92 后复现 `verify app user_app_id or version info error`。这与历史 Demo 和所有 M0 探针的失败一致，排除本地构建目录、Demo 二进制名及本应用实现。
- 下一步: 对妙算、M4T、遥控器/Pilot 进行完整断电重启并重新建立连接，确认 Pilot 中妙算状态正常后，再用此官方 Demo DPK 做一次安装对照；若仍失败，带 App Manager/DUSS timeout 证据升级至 DJI 支持或项目设备维护渠道，暂停应用侧 PSDK 开发。

## 2026-07-22 18:31  M0-AIRCRAFT-INFO-001  device-app-verification-recovered
- 意图: 恢复妙算 PSDK 应用安装校验链路，并用官方 Demo 验证设备环境。
- 动作: 依据设备侧排障信息，完整重启妙算 3 与 M4T 后重新建立连接；使用根目录 `build/` 新构建的官方 `PSDK_v01.00.00.01.dpk` 再次安装。
- 产出: 官方 Demo 安装成功；此前阻断 M0 的 App Manager/飞行器通信问题已恢复。应用源码、DPK 元数据和 App 身份配置无需因先前失败而继续变更。
- 验证: 同一个官方 Demo DPK 在重启前稳定复现 `verify app user_app_id or version info error`，重启并重新连接后安装成功，证明该故障属于设备侧安装时通信/启动环境，而非构建产物。
- 下一步: 仅启动官方 Demo 并确认 `dji_app_ctl status PSDK`、应用日志与 PSDK Core 初始化正常；确认后回到 `manifold_cloud_agent` 的只读 AircraftInfo 探针，先以 DPK 方式验证生命周期再继续 M1。

## 2026-07-22 18:31  M0-AIRCRAFT-INFO-001  official-demo-installed
- 意图: 确认重启后的官方 Demo 不仅完成安装命令，而且被 App Manager 持久化注册。
- 动作: 现场执行 `dji_app_ctl list PSDK`，只读取应用元数据，不启动 Demo。
- 产出: `PSDK` 已注册为第三方 Manifold 3 应用，版本 `01.00.00.01`、`is_dji_app=no`、`app_size=152.29MB`、`data_size=0.10MB`；无新增代码或迁移脚本。
- 验证: `dji_app_ctl list PSDK` 返回单个应用且元数据完整，证明官方 DPK 已通过安装校验并持久化。
- 下一步: 不启动包含 WS/云台/Liveview 样例的完整 Demo；构建并安装仅含 Core/AircraftInfo/DeInit 的 M0 探针作为 `PSDK` 的更高版本升级包，再验证只读生命周期输出。

## 2026-07-22 18:46  M0-AIRCRAFT-INFO-001  probe-installed-and-lifecycle-verified
- 意图: 用最小、只读 PSDK 应用验证 M4T 上的 Core 生命周期，并区分安装校验与应用长期运行要求。
- 动作: 将探针调整为与官方 Demo 相同的 DPK package/binary identity；增加无凭据阶段追踪；在 `ApplicationStart` 后保持运行，直到 App Manager 请求停止。现场安装 `PSDK_v01.00.00.05.dpk` 并读取 `/home/dji/m0_probe_install_trace.log`。
- 产出: M0 探针成功安装；追踪记录 `platform_registration`、凭据加载、`DjiCore_Init`、alias、firmware version、serial、`DjiCore_ApplicationStart` 和 `running` 全部返回 `0x00000000`。安装校验结束后 App Manager 请求停止，`DjiCore_DeInit` 返回 `0x000000EC`。
- 验证: 先前立即退出的 `.04` 探针安装失败；保持运行的 `.05` 探针安装成功，证明 App Manager 要求应用在安装校验后存活。`dji_app_ctl status PSDK` 显示 Not Running 属于安装校验结束后的 stop，不代表 Core 初始化失败。
- 下一步: 用 `dji_app_ctl start PSDK` 显式启动探针并检查 Running 状态；采集 ApplicationStart 前后 AircraftInfo/飞机版本的持久化输出。注意实测 App Manager 将代码 `major=1, debug=5` 显示为 `05.00.00.01`，与 SDK 头文件声明的字段显示顺序相反；在发布版本策略冻结前需用下一版探针验证并记录这一平台行为。

## 2026-07-22 19:02  M0-AIRCRAFT-INFO-001  m4t-identity-read-and-version-contract-corrected
- 意图: 在真实 M4T 上读取 AircraftInfo，并确认 DPK manifest 与 PSDK 运行时应用版本的安装校验约束。
- 动作: 现场安装并启动探针，读取 `/home/dji/m0_probe_install_trace.log`；本地对照 `psdk_lib/include/dji_typedef.h` 中的飞行器枚举。一次 `.06` 包尝试将运行时版本设为 `06.00.00.01`、manifest 保持 `01.00.00.06`，安装校验失败；随后将下一包恢复为相同字段顺序的 `01.00.00.07`。
- 产出: M4T 实机返回 `series=8`（`DJI_AIRCRAFT_SERIES_M4`）、`type=99`（`DJI_AIRCRAFT_TYPE_M4T`）、`adapter=3`、`mount_type=5`、`mount_position=8`，飞机版本为 `17.2.5.1`；ApplicationStart 前后结果一致。新包 `build/dpk/PSDK_v01.00.00.07.dpk` 已生成，元数据为 `Id=190335`、`Version=01.00.00.07`、`Binary=bin/dji_sdk_demo_on_manifold3`。
- 验证: trace 中 Core 初始化、版本设置、AircraftInfo 读取、ApplicationStart 和运行态均为成功；SDK 枚举将 type `99` 明确映射为 Matrice 4T。`.06` 在安装进度 83% 失败，证明 manifest 与 `DjiCore_SetFirmwareVersion` 不能因 App Manager 的倒序显示而改成不同数值。
- 下一步: 安装 `.07` 并确认其可安装、可启动和 `AircraftInfo` 输出不变；版本策略采用运行时代码与 manifest 都为 `01.00.00.N`，App Manager 的倒序状态显示仅作为设备侧显示现象记录，不作为版本编码依据。M0 完成后再按计划实现 M1 的最小进程骨架，不触发飞控、相机、云台或直播控制。

## 2026-07-22 19:17  M0-AIRCRAFT-INFO-001  accepted-on-m4t
- 意图: 最终确认修正版本编码后的只读探针能在 M4T + 妙算 3 上安装并作为受管应用运行。
- 动作: 现场安装 `PSDK_v01.00.00.07.dpk`、启动 `PSDK`、等待 3 秒后查询应用状态，并读取探针 trace。
- 产出: `.07` 安装成功，`dji_app_ctl status PSDK` 返回 `Running`；设备侧显示版本为 `07.00.00.01`。M0 只读探针与其构建/打包资源保留在工作树中，尚未创建本轮实现提交。
- 验证: 安装流程在 53% 后以 `APP INSTALL SUCCESS` 结束；状态为 `Running`。trace 再次记录 M4/M4T（`series=8`、`type=99`）与飞机版本 `17.2.5.1`，所有 Core 初始化及 `ApplicationStart` 阶段返回成功。trace 中的 `stop_requested`/`core_deinit=0x000000EC` 来自安装校验完成后的受管停止周期；其后 `status=Running` 是手动启动后的权威状态。
- 下一步: **M0 完成（M4T）**。进入 M1 前先将当前 M0 探针改造成最小 `manifold_cloud_agent` 服务骨架：保留 Core 生命周期和 M4T capability guard，新增明确的进程状态/健康日志；仍不初始化或调用飞控、相机、云台、Liveview 和公网 WebSocket。
