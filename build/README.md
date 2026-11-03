# 构建与复现

适用 Windows、Vivado/Vitis 2025.2。建议将仓库克隆或下载到较短的纯英文目录，例如 `C:/NLIM_set_final`，避免厂商工具的路径长度限制。以下命令均从包根目录执行，不连接开发板。

## 1 本地功能与数值回归

准备 Python 3.12、MinGW-w64 GCC；将编译器加入 PATH。使用虚拟环境安装依赖，脚本不自动下载依赖。

```powershell
python -m pip install -r build/requirements.txt
$env:NILM_GCC_BIN='C:/mingw64/bin'
powershell -NoProfile -ExecutionPolicy Bypass -File build/test_host.ps1 -Gcc C:/mingw64/bin/gcc.exe -Python python
python skill/ps-pl-release-validation/scripts/test_tools.py
powershell -NoProfile -ExecutionPolicy Bypass -File build/test_cnn.ps1 -Gcc C:/mingw64/bin/gcc.exe -Gxx C:/mingw64/bin/g++.exe
```

输出为 `sim/results/`。C 回归涵盖事件、登记、持久保存、计量和状态报文；物理波形测试读取 `data/golden/`，不依赖原作者 Downloads 目录。支持库由本机安装提供，未将整个 Python 环境打包。

## 2 重建硬件工程

快速检查源码、IP 和连接是否齐全，不执行布局布线：

```powershell
& 'C:/AMDDesignTools/2025.2/Vivado/bin/vivado.bat' -mode batch -source build/create_hardware.tcl
powershell -NoProfile -ExecutionPolicy Bypass -File build/test_stream_rtl.ps1
```

完整重建 CNN IP 和 PL：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File build/build_hls.ps1
& 'C:/AMDDesignTools/2025.2/Vivado/bin/vivado.bat' -mode batch -source build/create_hardware.tcl -tclargs --implement
```

`src/hls/` 含 CNN 源码及完整权重；`src/hardware/cnn_ip/` 含已导出的 IP；其他 XCI 来自现有实现。若 `work/hls/hls/impl/ip/` 存在，会优先采用新综合 IP。修改 HLS、RTL 或约束后，必须重新运行相关仿真及实现检查，不能沿用旧报告作为新硬件的证据。

HLS 重建后，可依次执行 `build/cosim.ps1 -Case 0` 到 `-Case 3`，分别验证四组输入，结果在 sim/results。24 组输入的主机 C/C++ 数值比对由 test_cnn.ps1 执行；它不是 RTL 时序仿真。主机数值测试记录与四组 RTL 协同仿真通过日志归档于 sim/results 和 build/reports/performance。

## 3 生成 PS 平台与程序

```powershell
& 'C:/AMDDesignTools/2025.2/Vitis/bin/vitis.bat' -s build/create_platform.py
powershell -NoProfile -ExecutionPolicy Bypass -File build/build_ps.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File build/make_boot.ps1
```

平台优先使用完整硬件重建得到的 XSA；若没有，则使用包内 `board/hardware/nilm_board.xsa`。XSA、bitstream 必须成套。BSP 由脚本重新生成，保留 lwIP、xilffs、定时器和 FPU 中断现场配置。运行 `create_platform.py` 需新平台目录；已有结果应移到备份目录后再重新构建，脚本不删除旧工程。

新 BOOT 位于 `work/boot/BOOT.BIN`，仍为包内源码的 `NLIM_set_final`。

## 4 网页接收工具

```powershell
python src/web/dorm_demo.py --ip 192.168.1.10
powershell -NoProfile -ExecutionPolicy Bypass -File build/build_receiver.ps1
```

网页脚本只用 Python 标准库；独立 EXE 打包需要 PyInstaller。已有可直接运行的 EXE 在 `board/release/`，无需重新打包才能演示。

## 5 性能与复现范围

`build/reports/performance/theoretical_performance.md` 提供真实报告的推导，`build/recompute_performance.py` 可重新核对周期换算。

构建与回归记录见 `build/reports/reproduction/summary.json`。实现报告与包内 bitstream 对应，指标按理论分析、综合、仿真和实现报告分类整理。
