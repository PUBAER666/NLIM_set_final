# 事件验证工作流

## Stage 1：高召回候选

第一阶段目标不是一次性做到完美 Precision，而是尽量找到潜在状态变化。

候选检测可以来自：
- 差分
- change score
- CUSUM
- 周期特征突变
- 功率/电流阈值
- 模型化状态变化

## Stage 2：持续状态确认

真正的设备状态变化应该在事件后形成新的相对稳定状态。

建议检查：
- 多个时间尺度
- 功率
- RMS 电流
- 波形形状
- 符号/方向一致性

## Stage 3：重复候选抑制

一次物理开关可能在短时间内产生多个相邻峰。

需要：
- suppression window
- 保留更强或更稳定的候选
- 防止同一事件重复计数

## Stage 4：微小变化与内部状态

小漂移、恒温控制、内部循环不一定等于新的主要负载事件。

可结合：
- |ΔP|
- |ΔI|
- 周期性
- 相似的波形 signature
- 正负交替变化

## Stage 5：Transient episode

短时间的 A -> B -> C 可能是：
- leading glitch
- startup overshoot
- rollback
- 两个真实事件

歧义情况下优先保留，避免为了 Precision 牺牲真实事件。

## Stage 6：多负荷 residual

多设备同时运行时：
- 先用事件前/后稳定状态构造物理 residual；
- 做相位/周期对齐；
- residual 选择不能借助类别标签；
- 再把 residual 交给冻结分类器。

## Stage 7：事后评价

所有 KEEP/REJECT 决策完成后，才读取 GT 计算：
- TP
- FP
- FN
- Precision
- Recall
- F1
