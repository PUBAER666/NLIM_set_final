# 常见失败模式

## 1. Window-level random split

**触发条件**  
同一连续记录被切成很多相邻窗口，再随机打散到 Train/Val/Test。

**风险**  
模型在 Test 中看到与训练样本极相似的波形片段。

**处理**  
改为 Session/Recording/Device 级 Group split。

## 2. 新 batch 重新洗牌旧数据

**触发条件**  
每次新增数据都重新运行随机划分。

**风险**  
旧 Test 样本可能进入 Train，历史成绩失去可比性。

**处理**  
冻结旧 manifest，只增量添加新行。

## 3. Test 调参

**触发条件**  
根据 Test 准确率反复修改阈值、特征、模型结构。

**风险**  
Test 退化为开发集。

**处理**  
重新保留一个真正独立的封存 Test。

## 4. Scaler/PCA 泄漏

**触发条件**  
在完整数据集上 `fit_transform` 后再切 Train/Test。

**风险**  
Test 分布信息进入训练过程。

**处理**  
只在 Train 上 fit，在 Val/Test 上 transform。

## 5. 文件名不同但内容重复

**触发条件**  
复制文件后改名，manifest 看不出重复。

**风险**  
Sample ID 检查会漏掉真实重复。

**处理**  
高风险项目增加内容 hash 或来源 recording ID。
