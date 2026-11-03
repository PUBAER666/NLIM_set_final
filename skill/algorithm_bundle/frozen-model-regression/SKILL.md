---
name: frozen-model-regression
description: 验证冻结模型在多人协作、外围功能扩展或板端移植后是否保持不变。用于比较模型权重、Scaler、类别映射、Gate/配置文件和黄金样本输出，防止意外重训、重新拟合或推理逻辑漂移。
---

# 目标

证明“外围功能发生变化”不等于“原冻结模型被改变”。

# 何时使用

当项目发生以下变化时启用：
- 新增 Unknown/拒识层；
- 新增事件检测、多负荷 residual；
- 新增快速注册；
- Python -> C/C++/FPGA/PS-PL 移植；
- 队友提交新模型包；
- 发布候选版本；
- 准备最终验收。

# 核心原则

验证分两层：

1. **资产一致性**
   - 模型权重
   - Scaler
   - 类别映射
   - Gate/阈值配置
   - 输入契约

2. **行为一致性**
   - 同一黄金输入
   - 比较预测类别、路由结果、置信度和关键中间量
   - 允许的浮点误差必须显式声明

# 工作流

1. 明确冻结边界：哪些文件/参数必须不变。
2. 建立资产 manifest。
3. 运行 `scripts/compare_model_assets.py`。
4. 对相同黄金样本分别运行 baseline 与 candidate。
5. 运行 `scripts/run_golden_regression.py`。
6. 检查：
   - 类别预测是否一致；
   - Gate/路由是否一致；
   - 允许误差范围内的数值是否一致；
   - 是否出现新增 NaN/Inf；
   - 是否出现缺失样本或重复样本。
7. 输出 PASS/FAIL，并记录 hash 与版本。

# 硬性 FAIL 条件

除非变更本身已被批准，否则以下情况直接 FAIL：
- 权重文件 hash 改变；
- Scaler 改变；
- 类别索引映射改变；
- Gate 阈值改变；
- 输入长度/特征顺序改变；
- 同一黄金输入的最终预测改变；
- 使用了与冻结协议不一致的预处理。

# 注意

“文件名相同”不代表资产相同；必须比较内容。

“最终类别相同”也不能完全证明逻辑相同；关键项目应同时比较中间量和路由结果。

# 参考

- 检查清单：`references/FROZEN_ASSET_CHECKLIST.md`
- 黄金回归协议：`references/GOLDEN_REGRESSION_PROTOCOL.md`
- 本项目来源：`references/PROJECT_CASE.md`
