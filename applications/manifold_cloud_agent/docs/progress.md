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
