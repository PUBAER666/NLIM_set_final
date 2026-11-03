# FPGA/NILM 算法 Skill 包

本目录按照当前常见 Agent Skill 结构整理：

- 每个 Skill 的核心入口为 `SKILL.md`
- 可重复、需要确定性执行的检查放在 `scripts/`
- 详细方法、失败模式与项目来源放在 `references/`
- 小型模板与示例放在 `assets/`

本包不复制完整项目源码、模型权重或大规模数据集。目标是把已经验证过的算法开发经验提炼成可复用的方法、校验脚本与检查清单。

## 本包包含的三个 Skill

### 1. `ml-experiment-integrity`
用于审查机器学习/时序识别实验的数据划分、Group/Session 泄漏、Train/Val/Test 职责、预处理泄漏与最终测试污染。

适合：
- NILM
- 振动故障诊断
- ECG/生理信号
- 声音分类
- 时序分类
- 同一物理对象会产生多个相邻窗口的任务

### 2. `frozen-model-regression`
用于多人协作或外围功能扩展后，确认冻结模型、Scaler、类别映射、Gate 和黄金样本输出没有被意外修改。

适合：
- 在冻结分类器外新增 Unknown、事件检测、快速注册、PC 功能
- Python 参考模型迁移到板端
- 模型交接与版本验收
- 回归测试与 CI

### 3. `event-pipeline-validation`
用于“高召回事件候选 -> 物理一致性验证 -> 假事件过滤 -> residual 输入契约检查”的事件算法开发。

适合：
- NILM ON/OFF 事件
- 其他连续信号中的状态切换检测
- 多负荷/混合信号先分离再分类的流程

## 与赛题 Skill 要求的对应关系

本包同时覆盖：

- **提示词/工作流**：每个 `SKILL.md` 都给出可直接执行的审查流程
- **案例模板**：`assets/` 中提供最小输入模板
- **校验脚本**：`scripts/` 中提供可直接运行的 Python 工具
- **实践清单**：`references/` 中记录常见失败、停止条件与排查方法

## 推荐提交顺序

如果时间有限，优先级建议：

1. `ml-experiment-integrity`
2. `frozen-model-regression`
3. `event-pipeline-validation`

前两项更通用，也最容易说明“换模型、换题目仍可复用”。

## 重要边界

本包中的 `PROJECT_CASE.md` 只用于说明这些 Skill 从真实工程中如何产生。

项目特有内容，例如：
- 4096 点窗口
- 25.6 kS/s
- 50 Hz
- KNN 五维特征
- 具体 Gate 阈值
- 每事件 3 条 residual

都放在“项目案例”中，不应被理解为所有项目的通用默认值。

## 验证

见根目录 `VALIDATION_REPORT.md`。本包新增的确定性脚本均已用合成数据实际运行检查。
