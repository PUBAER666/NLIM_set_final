# 项目案例：FPGA NILM 数据划分

本 Skill 的一个真实来源是 FPGA NILM 项目的增量数据划分。

项目中形成的关键工程原则：

1. 已存在的 `split_manifest` 行冻结，不重新分配。
2. 新 batch 只做增量加入。
3. Group 单位使用：
   `Appliance + Device + Mode + Session`
4. 一个完整 run/session 只能属于一个 Split。
5. 模型输入窗口固定为 4096 点，但“4096 点窗口”不是独立采集单位，不能因此随机跨 Split。
6. 最终 Test 在参数冻结后使用。

对应项目文件：
- `split_dataset_v2.py`
- `split_dataset_new_batch_incremental.py`
- `train_knn_from_map.py`
- `train_cnn_from_map.py`

这个案例中的具体类别、路径和窗口长度不是通用规则；可复用的是“按独立采集单元分组、冻结旧划分、增量追加”的实验纪律。
