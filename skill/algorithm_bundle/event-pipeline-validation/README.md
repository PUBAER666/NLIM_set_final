# event-pipeline-validation

用于把事件算法从“找峰值”提升为“可验证的事件输入管线”。

## 快速使用

对事件 manifest 做通用初筛：

```bash
python scripts/validate_event_candidates.py \
  --input event_manifest.csv \
  --output reviewed_events.csv
```

检查 residual 契约：

```bash
python scripts/check_residual_contract.py \
  --manifest residual_manifest.csv \
  --event-col Event_ID \
  --path-col Residual_Path \
  --rank-col Residual_Rank \
  --expected-per-event 3 \
  --expected-rows 4096
```

## 重要说明

`validate_event_candidates.py` 只是通用的 manifest 级助手，不复制某个项目完整的 V5/V5.2/V5.4 波形算法。

真正的多时间尺度持续状态验证、波形 shape voting 等，应按具体数据和采样条件实现。

## 已验证项目经验

本 Skill 的项目案例包含：
- 50 Hz 周期同步候选检测
- 多窗口持续状态验证
- duplicate suppression
- micro-change
- transient episode
- residual 相位对齐
- 每事件固定多条 residual
- GT 只做事后指标

详见 `references/PROJECT_CASE.md`。
