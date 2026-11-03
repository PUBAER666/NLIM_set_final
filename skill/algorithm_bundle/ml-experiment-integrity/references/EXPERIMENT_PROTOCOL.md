# 实验可信性协议

## 1. 先定义独立样本单位

时序数据常见误区是把“窗口”当成“独立实验对象”。

例如一个连续 10 秒采集被切成很多重叠或相邻窗口，这些窗口高度相关。若随机分到 Train 和 Test，会造成信息泄漏。

优先使用更高层分组：
- Device
- Session
- Recording
- Subject
- Site
- Run
- Trial

## 2. Train / Val / Test 的职责

### Train
允许：
- 拟合模型参数
- 拟合 StandardScaler/PCA
- 学习特征归一化统计量

### Val
允许：
- 选择超参数
- 选择阈值
- 比较候选方法
- 早停

### Test
只用于：
- 参数冻结后的最终评估

不应：
- 根据 Test 结果继续调阈值
- 用 Test 挑模型
- 用 Test 选择预处理版本

## 3. 最终测试封存

建议记录：
- 冻结时间
- 模型/配置 hash
- Val 上确定的阈值
- Test 是否已经打开
- 打开 Test 后是否发生代码变更

## 4. 增量数据

新增 batch 时：
- 旧 manifest 行保持不变；
- 新样本只增量追加；
- 新样本沿用同样的 Group 划分原则；
- 不因“数据更多了”就重新随机所有旧样本。

## 5. 预处理泄漏

以下对象如果会从数据估计参数，都要明确 fit 范围：
- StandardScaler
- MinMaxScaler
- PCA
- 特征选择
- 缺失值填充统计量
- 数据驱动阈值
- prototype/centroid
- calibration model

默认原则：能从数据“学到参数”的步骤都不能偷看最终 Test。
