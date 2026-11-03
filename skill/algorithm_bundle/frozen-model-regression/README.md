# frozen-model-regression

一个用于“冻结模型不被外围修改破坏”的回归验证 Skill。

## 典型问题

> 我们给原模型增加了事件检测、Unknown、快速学习或板端接口，怎么证明老模型没被改？

本 Skill 不要求重新训练模型，而是比较：

- 文件级资产
- JSON/CSV 配置
- 黄金输入输出
- 数值容差

## 1. 比较冻结资产

先编辑：

`assets/frozen_manifest_template.json`

然后运行：

```bash
python scripts/compare_model_assets.py \
  --baseline-root /path/to/baseline \
  --candidate-root /path/to/candidate \
  --manifest assets/frozen_manifest_template.json
```

## 2. 比较黄金样本输出

```bash
python scripts/run_golden_regression.py \
  --baseline baseline_predictions.csv \
  --candidate candidate_predictions.csv \
  --key-cols Sample_ID \
  --exact-cols Final_Pred,CNN_Called \
  --numeric-cols KNN_D1,KNN_Ratio,CNN_Confidence \
  --atol 1e-6 \
  --rtol 1e-6
```

## 已验证项目经验

本 Skill 来自一个冻结 KNN-CNN-Gate 系统在后续事件、多负荷和扩展模块开发中的回归需求。

详见 `references/PROJECT_CASE.md`。

## 失效条件

如果变更目标本身就是“重新训练模型”或“批准修改类别/阈值”，应先更新冻结基准，再运行回归，而不是要求新版本与旧版本完全一致。
