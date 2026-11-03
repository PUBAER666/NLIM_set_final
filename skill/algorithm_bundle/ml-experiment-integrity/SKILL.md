---
name: ml-experiment-integrity
description: 审查机器学习与时序识别实验的数据划分、Group/Session 泄漏、Train/Val/Test 误用、预处理泄漏和最终测试污染。用于检查 split manifest、训练/验证/测试协议、同源窗口分组和可复现性。
---

# 目标

判断一个实验是否具备可信、可复现且没有明显数据泄漏的 Train/Val/Test 结构。

# 何时使用

当用户：
- 给出 split/manifest CSV；
- 增加新 batch 或新设备；
- 修改 Train/Val/Test；
- 准备最终 Test；
- 怀疑同一 Session、Recording、Person、Device 等跨 Split；
- 需要证明实验结果没有被测试集反向调参污染。

# 不适用

不要用本 Skill 判断：
- 模型本身是否准确；
- 某个阈值是否最优；
- Unknown 算法是否有效。

这些属于模型/任务评估，不是数据完整性审查。

# 所需输入

优先获取：
1. 划分表 CSV；
2. Split 列；
3. 能代表独立采集单元的 Group 列，例如 `Device + Session`；
4. 样本唯一 ID；
5. 若存在，原始文件/窗口路径。

如果用户没有明确 Group 单位，先根据数据来源判断“哪些样本可能共享同一物理采集过程”。

# 工作流

1. **确认实验单位**
   - 区分“样本窗口”与“独立采集单元”。
   - 相邻窗口通常不能默认视为独立样本。

2. **审查 Group 泄漏**
   - 运行 `scripts/check_group_leakage.py`。
   - 同一 Group 出现在多个 Split 时，默认判为 FAIL，除非任务协议有明确理由。

3. **审查 Manifest 完整性**
   - 运行 `scripts/audit_split_manifest.py`。
   - 检查重复 Sample ID、重复路径、空 Split、非法 Split、类别分布异常。

4. **审查学习边界**
   - Scaler/PCA/特征选择/阈值搜索只能从允许的数据范围学习。
   - Train 用于拟合模型。
   - Val 用于模型选择和阈值选择。
   - Test 应在参数冻结后再打开。

5. **审查增量数据**
   - 已冻结的旧划分不应因为加入新 batch 而重新随机分配。
   - 新数据应按同一分组原则增量加入。

6. **输出结论**
   - `PASS`：无明显泄漏或协议违规。
   - `WARNING`：存在风险但需要领域解释。
   - `FAIL`：Group 跨 Split、Test 调参、预处理 fit 到 Test 等硬性问题。

# 停止条件

出现以下任一情况，应停止继续比较模型成绩，先修复实验协议：
- 同一物理 Session/Recording 同时出现在 Train 和 Test；
- Test 被用于调阈值、挑模型或反复开发；
- Scaler/PCA 等在 Train+Val+Test 上重新 fit；
- 最终 Test 打开后仍继续修改算法并重新汇报同一 Test 成绩。

# 参考

- 详细协议：`references/EXPERIMENT_PROTOCOL.md`
- 常见失败：`references/FAILURE_MODES.md`
- 本项目来源：`references/PROJECT_CASE.md`
