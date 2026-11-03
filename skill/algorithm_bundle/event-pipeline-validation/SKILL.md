---
name: event-pipeline-validation
description: 审查连续信号中的事件候选、重复/瞬态/微小变化过滤，以及多负荷 residual 输入契约。用于高召回事件检测后做物理一致性验证，并确保分类器不参与 residual 选择、Ground Truth 不参与事件决策。
---

# 目标

把“检测到一个变化峰”与“确认发生了可供分类的真实状态变化”区分开。

# 何时使用

当系统有：
- ON/OFF 或状态切换事件；
- 高召回候选检测器；
- 多时间尺度状态验证；
- 重复候选；
- 瞬态/回滚/内部循环；
- 多负荷 residual；
- 分类器前的固定长度事件输入。

# 核心原则

1. **事件检测与类别识别解耦**
   - 事件阶段不依赖类别标签。
2. **先高召回，再独立过滤假事件**
   - 不要把所有阈值都塞进第一阶段。
3. **Ground Truth 只用于事后评价**
   - 不得参与 KEEP/REJECT 决策。
4. **多负荷先物理分离，再分类**
   - 不允许用分类器结果反过来挑 residual。
5. **输入契约固定**
   - 长度、字段、数量、相位/对齐规则必须明确。

# 工作流

1. 读取候选事件表。
2. 检查前后状态是否有持续差异。
3. 检查微小变化。
4. 检查邻近重复候选。
5. 对近距离事件标记 transient episode，歧义时保守保留。
6. 多负荷情况下构造 residual。
7. 运行 `scripts/check_residual_contract.py`，确认输入契约。
8. 事件决策完成后，才允许用 GT 计算 TP/FP/FN。

# 辅助脚本

- `scripts/validate_event_candidates.py`
  - 用于对事件 manifest 做微小变化、邻近重复和 close-episode 的通用初筛。
  - 不是完整检测器，也不替代项目特有的多时间尺度波形验证。

- `scripts/check_residual_contract.py`
  - 检查 residual 数量、长度、字段、有限值、重复路径等。

# 停止条件

出现以下情况应停止进入分类器：
- residual 长度不符合契约；
- 当前/电压字段缺失；
- NaN/Inf；
- 同一事件 residual 数量错误；
- residual 选择使用了类别标签或 Ground Truth；
- 事件本身尚未确认是稳定状态变化。

# 参考

- 流程：`references/EVENT_VALIDATION_WORKFLOW.md`
- 失败模式：`references/EVENT_FAILURE_MODES.md`
- 项目来源：`references/PROJECT_CASE.md`
