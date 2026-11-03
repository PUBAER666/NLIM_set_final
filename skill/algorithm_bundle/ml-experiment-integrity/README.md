# ml-experiment-integrity

一个面向机器学习/时序数据的实验完整性审查 Skill。

## 能解决什么

它不负责提高准确率，而是回答：

> “这个准确率可信吗？”

重点检查：
- 同源 Group/Session 泄漏
- 重复样本
- Train/Val/Test 职责
- 最终 Test 污染
- 新 batch 导致旧划分被重洗
- Manifest 可复现性

## 快速使用

检查同一 `Appliance + Device + Mode + Session` 是否跨 Split：

```bash
python scripts/check_group_leakage.py \
  --csv split_manifest.csv \
  --split-col Split \
  --group-cols Appliance,Device,Mode,Session \
  --sample-col Sample_ID \
  --path-col Relative_Path
```

审查整体 manifest：

```bash
python scripts/audit_split_manifest.py \
  --csv split_manifest.csv \
  --split-col Split \
  --class-col Appliance \
  --sample-col Sample_ID \
  --path-col Relative_Path \
  --allowed-splits Train,Val,Test
```

## 输出解释

- `PASS`：未发现硬性问题
- `WARNING`：发现需要人工解释的风险
- `FAIL`：存在明确泄漏/重复/非法划分

## 已验证项目经验

本 Skill 来自一个 FPGA NILM 项目的真实数据增量、冻结划分和最终测试流程。项目案例见 `references/PROJECT_CASE.md`。

## 失效条件

脚本只能发现 manifest 中“能被字段表达出来”的泄漏。
如果同一物理设备被换了名字、Session ID 被重建、重复波形被复制后改名，仍需要更上层的数据溯源检查。
