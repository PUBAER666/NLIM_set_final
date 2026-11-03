# 项目案例：NILM 多负荷事件管线

本 Skill 的项目来源包含四个阶段。

## 1. V5 候选检测

项目中的通用 detector：

- 采样率：25.6 kS/s
- 工频：50 Hz
- 使用电压上升过零点进行周期同步
- 每工频周期计算电气特征
- 鲁棒估计正常周期噪声
- 找局部 change-score 峰
- 确认前后状态持续不同
- 对邻近候选做 NMS

事件检测阶段：
- 不知道应该有几个事件；
- 不使用文件名中的电器标签；
- 不加载 KNN/CNN/Scaler/Gate；
- `True_Class` 留空。

参考文件：
- `detect_pair_events_v2.py`

## 2. V5.2 候选验证

以高召回候选为输入，再检查：

- 40 / 80 / 160 / 320 ms 多尺度状态
- P / Irms / PF / waveform shape
- duplicate suppression
- recurrent / thermostatic cycling

Ground Truth 不参与 A/B/C 决策，只在之后统计指标。

参考文件：
- `validate_v5_candidate_events_v2.py`

## 3. V5.4 微小变化与 episode

项目案例中加入：
- MICRO_CHANGE：同时满足 |ΔP| < 8 W 且 |ΔIrms| < 0.04 A 时抑制
- Close episode：相邻事件间隔约 0.08~0.60 s 时分析 A -> B -> C
- Leading glitch
- Overshoot rollback
- 歧义时保守 KEEP BOTH

这些阈值是项目经验，不应直接搬到其他采样系统。

参考文件：
- `validate_v5_candidate_events_v4_episode.py`

## 4. residual 输入契约

项目最终 residual 规则：
- 每条 residual 恰好 4096 点
- subtraction 前做电压相位对齐
- 只用物理规则选择 residual
- 每事件恰好 3 条 residual
- residual 选择不加载 KNN/CNN/Scaler/Gate
- 标签与 GT 不参与 residual 选择

随后 3 条 residual 才进入冻结 KNN-CNN-Gate，并做事件级投票。

参考文件：
- `prepare_pair_event_residuals_v5_4_final.py`
- `run_stable_v3_frozen_knn_cnn_gate.py`
