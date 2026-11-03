# VALIDATION REPORT

本报告记录打包时对新增脚本做的最小可执行测试。

## check_group_leakage.py
- exit code: `0`
- result: `PASS`

## audit_split_manifest.py
- exit code: `0`
- result: `PASS`

## compare_model_assets.py
- exit code: `0`
- result: `PASS`

## run_golden_regression.py
- exit code: `0`
- result: `PASS`

## validate_event_candidates.py
- exit code: `0`
- result: `PASS`

## check_residual_contract.py
- exit code: `0`
- result: `PASS`

## 总结
- 所有新增脚本最小执行测试：`PASS`
- 测试使用合成数据，只证明脚本接口和基本逻辑可运行；不替代项目真实数据验收。
