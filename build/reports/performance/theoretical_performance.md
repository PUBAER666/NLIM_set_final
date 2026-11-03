# NILM：FPGA 理论性能与工具报告结果

适用方案：NLIM_set_final；固件 NLIM_set_final；板卡 EES331 / XC7Z020CLG484-1；工具 Vivado/Vitis 2025.2。

## 计算结构与理论吞吐

CNN 卷积采用四输出通道并行、八位置交错累加，三层 MAC 主循环达到 II=1。在同频、相同精度、充分供数且无停顿条件下，单通路 II=1 基线每周期启动1次 MAC，当前四通路结构每周期启动4次 MAC，核心理想吞吐倍率为4。100 MHz下理想峰值为400 MMAC/s。

该倍率的口径是卷积核心与假设单通路硬件结构的理论吞吐比较。八位置交错用于缓解累加依赖；完整CNN还包含数据访问、初始化、激活、池化、特征汇总和输出写回。

## 延迟与时间预算

| 指标 | 数值 | 来源与口径 |
|---|---:|---|
| PL工作时钟 | 100 MHz | 当前实现时钟约束 |
| CNN最大延迟估计 | 3,002,349周期 / 30.02349 ms | HLS综合 |
| 有效CNN事务延迟 | 2,924,558周期 / 29.24558 ms | C/RTL协同仿真 |
| 4096点窗口名义时长 | 160 ms | 4096 / 25,600 Hz |
| 窗口时长 / 单次综合估计 | 5.33 | 单次CNN调用时间预算比 |

延迟按100 MHz、每周期10 ns换算。协同仿真中的错误ABI和错误模型编号请求走快速拒绝路径，推理时间取有效计算事务。5.33表示单次CNN的时间预算关系；事件确认还包含多窗口及PS处理等步骤。

## 完整PL资源与时序

| 资源 | 使用量 | 器件占用率 |
|---|---:|---:|
| LUT | 14,919 | 28.04% |
| FF | 19,985 | 18.78% |
| BRAM（36 Kb Tile） | 60 | 42.86% |
| DSP | 20 | 9.09% |

以上为包含采样及互连的完整PL设计资源。布局布线后WNS为+0.369 ns，TNS为0，满足当前100 MHz用户时序约束。四组协同仿真用例通过14维logits与64维特征比较，验证对应输入的软硬件数值一致性。

## 证据文件

- `build/reports/cnn.cpp`：计算结构。
- `build/reports/conv1_pipeline.rpt`、`conv2_pipeline.rpt`、`conv3_pipeline.rpt`：II=1调度结果。
- `build/reports/cnn_csynth.rpt`：HLS延迟与资源估计。
- `build/reports/cnn_cosim.rpt`、`test_cnn.cpp`、`cosim_case0.log`至`cosim_case3.log`：有效事务及输出比较。
- `build/reports/timing_summary.rpt`、`utilization.rpt`：实现时序与完整PL资源。
- `build/reports/release_manifest.json`、`weights_manifest.json`：版本及模型标识。
- `metrics.json`、`sources.json`：结构化指标、来源及SHA256。
