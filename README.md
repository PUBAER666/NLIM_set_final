# NLIM_set_fianl

基于 Zynq PS–PL 协同的电器识别与用电监测系统。

An Appliance Recognition and Electricity Usage Monitoring System Based on Zynq PS–PL Co-processing.

## 最终交付包

- [下载 NLIM_set_fianl.zip](https://github.com/PUBAER666/NLIM_set_fianl/raw/refs/heads/main/NLIM_set_fianl.zip)
- [SHA256 校验文件](NLIM_set_fianl.zip.sha256.txt)

包内包含配套 BOOT.BIN、Windows 接收工具、PS/PL 工程源码、模型资产、设计报告、九张软硬件框图、测试与性能材料，以及构建和校验说明。

硬件平台：EES331 / XC7Z020CLG484-1，V2 PCB 模拟前端。固件标识和接收工具版本统一为 `NLIM_set_fianl`。

## 使用

1. 下载并解压 `NLIM_set_fianl.zip`，先阅读包内 `README.md`。
2. 开发板断电，将 `board/release/sd_card/BOOT.BIN` 复制到 SD 卡根目录。保留已有登记文件 `NILM430.BIN` 和 `NILM431.BIN`。
3. 重新上电，用网线连接电脑。板卡默认地址为 `192.168.1.10`，电脑有线网卡设置到同一网段。
4. 使用本包 `board/release/check_connection.cmd` 检查连接，再运行 `start_capture.cmd` 打开实时页面。

更新时同时使用本包的启动文件和接收工具。接收工具无需安装 Vitis 或 Python；日志保存在工具旁的 `recordings/` 目录。

## 完整性

发布包 SHA256：

```text
6da9fdeecb5e4cdf39bfb199ca94b6087d925184e0c3d5e0230816941bc3bffd
```

包内附文件校验清单、构建记录和本地回归结果。具体工程结构及验证材料见包内说明。
