# 编译、打包与安装 `.dpk`

## 工具链（Mac）

```bash
export PATH="/Volumes/disk_out/Users/lucifier/workspace/PSDK/tools/bin:$PATH"
export PATH="/Volumes/disk_out/Users/lucifier/workspace/PSDK/tools/cmake/bin:$PATH"
aarch64-linux-gnu-gcc --version
cmake --version
```

## 编译

```bash
cd /Volumes/disk_out/Users/lucifier/workspace/PSDK/Payload-SDK
# 若为软链，实际在 Desktop/Payload-SDK
mkdir -p build && cd build
cmake ..
make -j$(sysctl -n hw.ncpu) dji_sdk_demo_on_manifold3
```

产物：`build/bin/dji_sdk_demo_on_manifold3`

## 打包

```bash
cd Payload-SDK
bash tools/build_dpk/build_dpk.sh \
  -i samples/sample_c/platform/linux/manifold3/app_json/app.json \
  -o build/dpk
```

产物示例：`build/dpk/PSDK_v01.00.00.01.dpk`

注意：

- macOS 无 `dpkg-deb` 时用脚本内 portable 打包（已修 ar 绝对路径问题）
- `app.json` 的 `user_app_id` / `firmware_version` 须与 `dji_sdk_app_info.h`、代码内版本结构一致

## 上传妙算

```bash
# zsh 下远程通配符要加引号
scp build/dpk/PSDK_v01.00.00.01.dpk dji@192.168.31.186:~/
```

## 安装启动

```bash
ssh dji@192.168.31.186
dji_app_ctl stop PSDK
dji_app_ctl install -i ~/PSDK_v01.00.00.01.dpk
# 若版本相同装不上：dji_app_ctl uninstall PSDK 后再 install
dji_app_ctl start PSDK
dji_app_ctl status
```

**start 参数是应用名 `PSDK`，不是 `PSDK_v01.00.00.01.dpk`。**

也可经 Pilot 2 → 妙算 → 应用管理安装。

## 改 WS/RTMP 配置后

1. 编辑 `ws_telemetry_config.h`  
2. 重新 `make` + `build_dpk.sh`  
3. scp + install + start  

## 拉取直播录像文件

```bash
# zsh
scp 'dji@192.168.31.186:~/live_out/*.h264' .
ffplay -f h264 live_m4t_xxxx.h264
```
