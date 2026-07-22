# M0 PSDK AircraftInfo 探针

本材料验证 M4T 上最小 PSDK 生命周期与只读机型信息读取。它不初始化 FlightController、CameraManager、Gimbal、Liveview、HMS、Waypoint 或任何控制模块；不发送飞行、云台、相机、存储或网络控制请求。

探针顺序固定为：注册基础平台端口 → `DjiCore_Init` → `AircraftInfo` 读取（ApplicationStart 前）→ `DjiCore_ApplicationStart` → `AircraftInfo` 读取（ApplicationStart 后）→ `DjiCore_DeInit`。

## 设备侧准备

1. 使用与 M0 基线一致的 M4T、Manifold 3、遥控器和 Pilot 2；飞行器保持停桨、在地、遥控器可随时接管。
2. 在开发机使用 aarch64 工具链构建：

   ```bash
   cmake -S applications/manifold_cloud_agent -B applications/manifold_cloud_agent/build \
     -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc
   cmake --build applications/manifold_cloud_agent/build --target m0_aircraft_info_probe
   ```

3. PSDK 应用必须打成 `.dpk` 并由 DJI Pilot 2 / Manifold 3 应用管理器安装启动；**不要**以 SSH 手动执行 ELF 代替安装。构建时提供已注册应用的 App ID（App ID 不是 App Key/License）：

   ```bash
   cmake -S applications/manifold_cloud_agent -B applications/manifold_cloud_agent/build \
     -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
     -DMANIFOLD_AGENT_USER_APP_ID='你的已注册AppID' \
     -DMANIFOLD_AGENT_DPK_NAME='PSDK' \
     -DMANIFOLD_AGENT_EMBED_CREDENTIALS_FILE="$PWD/applications/manifold_cloud_agent/config/credentials.env"
   cmake --build applications/manifold_cloud_agent/build --target m0_aircraft_info_probe_dpk
   ```

   产物为 `applications/manifold_cloud_agent/build/dpk/PSDK_v01.00.00.07.dpk`。此处 `PSDK` 必须是凭据所属的已注册 DJI 应用名；它只是 M0 探针的临时 DPK 身份，不是正式 `manifold_cloud_agent` 的最终注册名。只将该 `.dpk` 复制到妙算并通过 Pilot 2 安装；妙算端不需要仓库、源码、CMake 文件或 PSDK 静态库。

4. `credentials.env` 只用于 M0 构建期生成私有头文件，原始文件不进入 Git 或 DPK；但生成的测试二进制会包含这些 PSDK 应用凭据。因此该方式只能用于 M0 临时探针，生成的 `.dpk` 不得作为生产交付物，也不要把文件、其内容、App Key 或 License 发送给 Codex。

## 运行与预期输出

在 Pilot 2 安装并启动 `.dpk` 后，读取应用日志；不要在 SSH 中手动运行二进制。探针不读取妙算上的 `credentials.env`，且不打印任何凭据值。

成功时，保存下列不含凭据的输出行：

```text
M0_PROBE stage=core_init result=0x00000000
AIRCRAFT_INFO phase=before_application_start ...
M0_PROBE stage=application_start result=0x00000000
AIRCRAFT_INFO phase=after_application_start ...
AIRCRAFT_VERSION phase=after_application_start version=...
M0_PROBE stage=core_deinit result=0x00000000
```

若任何阶段失败，请只发送 `M0_PROBE`、`AIRCRAFT_INFO`、`AIRCRAFT_VERSION` 和相关 PSDK 错误行；不要发送环境变量、配置文件或完整调试日志。

如果 DPK 在安装校验阶段失败，读取妙算上的 `/home/dji/m0_probe_install_trace.log`。它只记录阶段名和返回码，不记录 App Key、License 或其他凭据。

## 验收与回传

- [ ] `core_init`、`application_start`、`core_deinit` 均为 `0x00000000`。
- [ ] 两次 `AIRCRAFT_INFO` 的枚举值均已采集；若前后不同，必须保留两组结果。
- [ ] 已采集 ApplicationStart 后的 `AIRCRAFT_VERSION`，并与 M0 基线飞机固件 `17.02.0501` 对照记录。
- [ ] 现场确认全过程未出现飞行、云台、相机、存储或视频行为。

将勾选结果和上述脱敏输出发回后，我会记录 `M0-AIRCRAFT-INFO-001` 的验证结果，再决定 M4T Profile 的枚举映射和下一步模块初始化顺序。
