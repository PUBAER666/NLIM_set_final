# 项目案例：冻结 KNN-CNN-Gate 回归

真实项目中的冻结推理协议包含：

- 不训练
- 不重新拟合 Scaler
- 不重新调 Gate
- residual 选择阶段不允许分类器参与
- 推理过程中不允许 Ground Truth 参与
- 每条 residual 固定 4096 行
- KNN 五维特征顺序：
  `P_W, Irms_A, Ipeak_A, Crest, PF`
- CNN 输入为 4096 点极性对齐电流波形
- 每个事件使用 3 条 residual 做事件级投票

项目中的 Gate V3 是在 Validation 上选定后冻结的，最终 Test 打开后禁止继续搜索阈值。

项目参考文件：
- `run_stable_v3_frozen_knn_cnn_gate.py`
- `evaluate_frozen_gate_v3_known_test.py`
- `freeze_gate_v3.py`
- `FROZEN_GATE_V3.json`
- `train_knn_from_map.py`
- `train_cnn_from_map.py`

14 类 KNN/CNN 的类别映射在冻结版本中一致。这个具体类别集合属于项目案例，不是 Skill 的通用限制。
