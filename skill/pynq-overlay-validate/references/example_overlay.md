# 两个独立示例

示例与电器识别无关，源码见 [example_hls.cpp](../assets/example_hls.cpp)。

- `accel_add`：两个 uint32 标量相加，结果按32位无符号数回绕。
- `accel_multiply`：两个 float32 标量相乘。
- 两者均只使用 AXI-Lite 控制/数据寄存器和 ap_ctrl_hs，不使用 DMA 或 m_axi。

## 主机源码测试

```sh
g++ -std=c++11 -Wno-unknown-pragmas assets/example_hls.cpp assets/example_hls_test.cpp -o example_test
./example_test
```

这是 C++ 主机测试，不是 HLS 综合、RTL 仿真或板测。

## 可选 HLS 导出

在装有 Vitis 2025.2 的开发机执行；将器件参数换成实际目标板器件：

```sh
python scripts/build_example.py --vpp /opt/amd/2025.2/Vitis/bin/v++ --part xc7z020clg484-1 --top accel_add --out work/add_ip
```

Windows 可将 `--vpp` 换成安装目录下 `Vitis/bin/v++.bat`。输出目录须不存在；脚本保留日志和配置，不删除旧结果。另一个算例使用 `--top accel_multiply` 和新的输出目录。仅导出 IP，未生成完整板卡 Overlay。

## 在 Vivado 中接入板卡专用设计

1. 按目标板及其 PYNQ 镜像要求创建 PS 工程，配置正确的 DDR/MIO/时钟。
2. 加入导出的 HLS IP；把 `s_axi_control` 接到 PS 可访问的 AXI 主端口，经互连和复位模块连接时钟/复位，分配地址。
3. 示例无外部存储端口，不需要 AXI DMA。加法例可命名 `accel_add_0`；乘法例配置使用 `compute/accel_multiply_0`，也可改配置匹配实际实例路径。
4. 验证 Block Design、综合和实现，生成该板的 bitstream，并导出同一设计的 HWH。
5. 配置示例期望输入偏移0x10、0x18，输出0x20；实际寄存器名称和控制字段以 HWH/导出头文件为准。若导出工具改变名称，应更新契约，不能跳过元数据检查。
6. 将 `.bit/.hwh` 作为同名文件放到 `assets/overlay/`，运行 inspect 后再执行板测。

本 Skill 不提供跨板通用 bitstream；硬件设计仍需符合各板平台。当前主项目的 BOOT.BIN 不包含这两个演示 IP，不能直接用本示例驱动它。
