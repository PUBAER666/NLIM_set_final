# 技能包：NILM 系统设计、算法验证与 PS–PL 交付

本目录把小组在模拟前端与 PCB、离线模型与事件算法、FPGA 板端协同三个方向形成的技能集中交付。每项均包含 `SKILL.md` 入口和对应资源；技能用于复用检查方法。

## 选择与使用

| 技能与入口 | 适用任务 | 使用与输入 | 验证来源和已报告效果 | 边界 |
|---|---|---|---|---|
| [kicad-sch-codegen](kicad-sch-codegen/SKILL.md) | 程序化绘制模拟原理图、导入 EDA | 按入口选择 PNG、KiCad 旧版或 KiCad 6 路线；提供电路与目标工具 | 原理图绘制及连接自检；历史问题与检查见 [硬件说明](hardware_source_readme.md) | 连线自检不能证明电路意图正确；不同路线依赖不同 |
| [llm-hw-debug-pitfalls](llm-hw-debug-pitfalls/SKILL.md) | 检查模拟调试中的模型假设 | 先算节点方程，核对仪器和已知答案对照；参考目录有两个完整案例 | 面包板测量与功率计对照中记录了假设撤回及反例 | 缺少现场测量时不能替代实测；不适用于纯数字时序评估 |
| [pcb-sch-cross-audit](pcb-sch-cross-audit/SKILL.md) | KiCad PCB 与原理图网络、封装、文字交叉审计 | 按入口使用 `reference/audit_pcb.py` 并显式提供 PCB、原理图和库文件 | 项目审计发现文字网络不符，缺陷对照与修复复查见 [硬件说明](hardware_source_readme.md) | 原理图和 PCB 同错时可能通过；不代替布线、EMC 与机械检查 |
| [ml-experiment-integrity](algorithm_bundle/ml-experiment-integrity/SKILL.md) | 时序样本分组、训练/验证/测试泄漏 | 按目录说明提供含分组及 split 的清单，运行 `scripts/` | 同源 Session 分组问题；原包以合成数据运行基本测试 | 只能发现输入清单能表达的泄漏；不评价模型准确率 |
| [frozen-model-regression](algorithm_bundle/frozen-model-regression/SKILL.md) | 扩展功能或移植前后检查冻结模型 | 提供基准和候选资产、清单、黄金输入输出与容差 | 由资产漂移和黄金回归需求提炼；原包合成数据基本测试 | 不能以类别相同代替中间数值对照；批准重训后需更新基准 |
| [event-pipeline-validation](algorithm_bundle/event-pipeline-validation/SKILL.md) | 事件候选及残差信号输入契约 | 提供事件/残差信号清单；运行两个 `scripts/` 工具 | 由候选重复、短暂事件和残差契约问题提炼；原包合成数据基本测试 | 脚本是清单初筛，不是完整检测器；项目参数不得直接推广 |
| [ps-pl-release-validation](ps-pl-release-validation/SKILL.md) | PS–PL 接口、交付校验、数值对照与分阶段诊断 | 阅读入口及 `references/`；按说明运行 `scripts/`，提供交付根目录或对照数据 | 基于协议错配、无效仿真事务及旧日志；数值与交付校验工具有正反例 | 对裸机 Zynq 有项目相关假设；不是已验证通用 PYNQ Skill，也不代替板测 |

## 运行环境及证据

- 各 Skill 的 `README.md` / `SKILL.md` 给出独立命令与依赖；硬件原理图绘制按路线可能需要 `schemdraw` 或 `matplotlib`。PCB 检查脚本有项目默认路径，跨项目必须显式指定输入。
- 硬件历史来源说明：[hardware_source_readme.md](hardware_source_readme.md)；相关项目数据和原始轨迹并不全部包含在本技能包中。
- 算法包原文与合成测试：[algorithm_bundle/README.md](algorithm_bundle/README.md)、[VALIDATION_REPORT.md](algorithm_bundle/VALIDATION_REPORT.md)。
- FPGA 包原文：[ps_pl_source_readme.md](ps_pl_source_readme.md)。
- 算法与 FPGA 协作记录合稿：[llm_collaboration.md](../report/llm_collaboration.md)。

本次合包不修改各成员原始 Skill 和脚本；硬件案例中的测量数字属于当次历史台架，不直接等同当前 PCB 的标定结果。目录仍包含 `README.md`、`SKILL.md`、部分大写参考文件等名称；赛事文件命名约束与工具约定可能冲突，正式归档前需要团队和赛事要求核对，避免直接改名后破坏相对引用。

## 本次合包自检

- PS–PL 数值与交付校验工具的正反例自测通过。
- 算法分组检查使用包内 `split_manifest_example.csv`，7行、6组，无跨划分组，结果为 PASS；这是合成示例，并非项目真实数据验收。
- 注意：算法 Skill 的原 README 示例使用 `Appliance` 列，而自带示例 CSV 的对应列名是 `Class`。使用自带示例时，应把 `--group-cols Appliance,Device,Mode,Session` 改为 `--group-cols Class,Device,Mode,Session`；实际数据应按真实表头指定。
- 其他技能沿用原交接包记载的验证，不称为本次重新运行。

## 七项技能的使用说明

以下内容以本目录为技能包根目录。Windows PowerShell 命令使用本机的 `D:\Dev\Python314\python.exe`；在其他电脑使用时，请将该路径替换为实际 Python 路径。执行前应阅读每项的 `SKILL.md`，并准备相应输入。

### 1. 原理图程序化生成与自检

**入口：** [kicad-sch-codegen/SKILL.md](kicad-sch-codegen/SKILL.md)。

**适用与使用：** 需要生成 PNG 原理图或 KiCad 工程时，先根据交付对象选择路线：小型模拟图可使用 `schemdraw`；多引脚符号可使用 `reference/` 中的绘图工具和 `audit()`；交付 EDA 工程时应选择目标软件支持的 KiCad 版本。生成后执行连接检查，并在目标软件中检查导入结果。不同路线可能需要 `matplotlib` 或 `schemdraw`。

**来源与效果：** AFE 原理图生成过程中，符号引脚和导线端点曾出现偏差，引脚检查发现了脱接；原材料还记录了双通道原理图交付和电气规则检查。

**失效条件：** 连线检查不能证明电路设计意图正确；示例中的项目专用元件和坐标需按新设计修改，KiCad 版本兼容性需在目标工具中验证。

### 2. 大模型辅助硬件调试的纠错

**入口：** [llm-hw-debug-pitfalls/SKILL.md](llm-hw-debug-pitfalls/SKILL.md)。

**适用与使用：** 模型对模拟电路、仪器读数或异常波形给出解释时，先核对接线与仪器设置，再用基本方程计算预期值；为假设设计可判定的排除实验，并用已知答案的对照组验证自造判据。

**来源与效果：** 面包板调试中多次模型假设被测量纠正；充电器数据分析中，模型提出的判据被对照数据推翻。历史案例见该技能的 `reference/`。

**失效条件：** 没有现场测量时不能把推断当作验证；纯数字设计的功能和时序需另用仿真和工具报告。案例中的数字属于历史台架，不直接代表当前 PCB 性能。

### 3. PCB 与原理图交叉审计

**入口：** [pcb-sch-cross-audit/SKILL.md](pcb-sch-cross-audit/SKILL.md)。

**适用与使用：** 检查 KiCad PCB 与原理图的焊盘网络、封装、引脚数和文字标注。准备 PCB、原理图和缓存库，在本目录执行以下命令，并将尖括号内的占位路径替换为实际文件：

```powershell
& 'D:\Dev\Python314\python.exe' .\pcb-sch-cross-audit\reference\audit_pcb.py --pcb '<pcb.kicad_pcb>' --sch '<schematic.sch>' --lib '<schematic-cache.lib>' --out '<audit_output.txt>'
```

**来源与效果：** 项目审计曾发现文字标注与实际网络不一致，原技能说明记录了缺陷版和修复版的对照检查。

**失效条件：** 原理图和 PCB 同时存在相同错误时可能通过；本工具不代替走线、电磁兼容和机械检查。脚本有项目默认路径，跨项目必须显式指定输入。

### 4. 时序数据划分与实验完整性

**入口：** [ml-experiment-integrity/SKILL.md](algorithm_bundle/ml-experiment-integrity/SKILL.md)。

**适用与使用：** 检查同一设备、模式或采集会话是否跨训练集、验证集和测试集。提供含样本 ID、划分、分组字段及路径的 CSV；以下命令使用包内示例：

```powershell
& 'D:\Dev\Python314\python.exe' .\algorithm_bundle\ml-experiment-integrity\scripts\check_group_leakage.py --csv .\algorithm_bundle\ml-experiment-integrity\assets\split_manifest_example.csv --split-col Split --group-cols Class,Device,Mode,Session --sample-col Sample_ID --path-col Relative_Path
```

**来源与效果：** 同一 Session 名在不同工作模式下可能代表不同采集过程，促使分组按真实采集语义定义。原算法包记录了合成数据测试；本次使用7行示例检查，无跨划分组。

**失效条件：** 只能发现清单字段能够表达的泄漏；设备或波形改名后仍需额外溯源。该技能不评价模型准确率。

### 5. 冻结模型与黄金样本回归

**入口：** [frozen-model-regression/SKILL.md](algorithm_bundle/frozen-model-regression/SKILL.md)。

**适用与使用：** 扩展功能或移植后，准备基准与候选资产、资产清单、相同黄金输入以及允许误差；按 [技能说明](algorithm_bundle/frozen-model-regression/README.md) 分别运行资产比较和黄金输出回归脚本。

**来源与效果：** 来源于功能扩展后确认冻结权重、缩放器、类别映射及推理结果没有意外漂移的需求；原算法包报告使用合成数据完成脚本基本测试。

**失效条件：** 仅比较类别无法证明中间量一致；输入预处理和输出顺序必须先对齐。若已批准重新训练或更改阈值，应先更新基准。

### 6. 事件候选与残差信号输入检查

**入口：** [event-pipeline-validation/SKILL.md](algorithm_bundle/event-pipeline-validation/SKILL.md)。

**适用与使用：** 对连续信号的事件候选进行初筛，检查送入分类器的残差信号数量、长度和字段。准备事件或残差信号清单，按 [技能说明](algorithm_bundle/event-pipeline-validation/README.md) 运行相应脚本，并根据自己的采样条件指定参数。

**来源与效果：** 来源于项目中的重复候选、启动瞬态及残差信号选择问题；原算法包报告使用合成数据完成脚本基本测试。

**失效条件：** 通用脚本只做清单级检查，不是完整事件检测器；多尺度波形验证需按具体系统实现。项目中的窗口长度和每事件样本数不是通用要求。

### 7. PS–PL 接口、数值与交付检查

**入口：** [ps-pl-release-validation/SKILL.md](ps-pl-release-validation/SKILL.md)。

**适用与使用：** 对 Zynq PS–PL 模型移植、识别链路及交付文件进行核验。先记录固件、模型、接口版本、数据单位与形状，再根据任务阅读 `references/` 并运行 `scripts/` 中的比较或校验工具。工具自测命令：

```powershell
& 'D:\Dev\Python314\python.exe' .\ps-pl-release-validation\scripts\test_tools.py
```

**来源与效果：** 来源包括启动文件与页面版本不匹配、识别故障阶段混淆，以及快速拒绝请求混入仿真延迟统计。本次合包运行数值与交付校验的正反例，自测通过。

**失效条件：** 哈希一致不能证明功能正确；仿真或主机回归不能代替板端实测。该工作流含裸机 Zynq 项目假设，不能称为已验证的通用 PYNQ 方案。
