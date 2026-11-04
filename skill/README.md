# 技能包：系统设计、算法验证与板端交付

本目录汇集小组在模拟前端与 PCB、离线模型与事件算法、FPGA 板端协同中形成的技能，并包含独立的通用 PYNQ Overlay 验证技能。每项均包含 `SKILL.md` 入口及对应资源，用于复用设计和检查方法。

## 选择与使用

| 技能与入口 | 适用任务 | 使用与输入 | 验证来源和已报告效果 | 边界 |
|---|---|---|---|---|
| [kicad-sch-codegen](kicad-sch-codegen/SKILL.md) | 程序化绘制模拟原理图、导入 EDA | 按入口选择 PNG、KiCad 旧版或 KiCad 6 路线；提供电路与目标工具 | 原理图绘制及连接自检；历史问题与检查见第 1 节 | 连线自检不能证明电路意图正确；不同路线依赖不同 |
| [llm-hw-debug-pitfalls](llm-hw-debug-pitfalls/SKILL.md) | 检查模拟调试中的模型假设 | 先算节点方程，核对仪器和已知答案对照；参考目录有两个完整案例 | 面包板测量与功率计对照中记录了假设撤回及反例 | 缺少现场测量时不能替代实测；不适用于纯数字时序评估 |
| [pcb-sch-cross-audit](pcb-sch-cross-audit/SKILL.md) | KiCad PCB 与原理图网络、封装、文字交叉审计 | 按入口使用 `reference/audit_pcb.py` 并显式提供 PCB、原理图和库文件 | 项目审计发现文字网络不符，缺陷对照与修复复查见第 3 节 | 原理图和 PCB 同错时可能通过；不代替布线、EMC 与机械检查 |
| [ml-experiment-integrity](algorithm_bundle/ml-experiment-integrity/SKILL.md) | 时序样本分组、训练/验证/测试泄漏 | 按目录说明提供含分组及 split 的清单，运行 `scripts/` | 同源 Session 分组问题；原包以合成数据运行基本测试 | 只能发现输入清单能表达的泄漏；不评价模型准确率 |
| [frozen-model-regression](algorithm_bundle/frozen-model-regression/SKILL.md) | 扩展功能或移植前后检查冻结模型 | 提供基准和候选资产、清单、黄金输入输出与容差 | 由资产漂移和黄金回归需求提炼；原包合成数据基本测试 | 不能以类别相同代替中间数值对照；批准重训后需更新基准 |
| [event-pipeline-validation](algorithm_bundle/event-pipeline-validation/SKILL.md) | 事件候选及残差信号输入契约 | 提供事件/残差信号清单；运行两个 `scripts/` 工具 | 由候选重复、短暂事件和残差契约问题提炼；原包合成数据基本测试 | 脚本是清单初筛，不是完整检测器；项目参数不得直接推广 |
| [ps-pl-release-validation](ps-pl-release-validation/SKILL.md) | PS–PL 接口、交付校验、数值对照与分阶段诊断 | 阅读入口及 `references/`；按说明运行 `scripts/`，提供交付根目录或对照数据 | 基于协议错配、无效仿真事务及旧日志；数值与交付校验工具有正反例 | 对裸机 Zynq 有项目相关假设；不是已验证通用 PYNQ Skill，也不代替板测 |
| [pynq-overlay-validate](pynq-overlay-validate/SKILL.md) | PYNQ Overlay、标量 AXI-Lite 接口和加速器黄金样例验证 | 准备接口契约与黄金样例，运行 mock 或 PYNQ 后端并查看 JSON/HTML 报告 | 27 项主机测试、两套 mock 算例及两个 HLS 示例综合与接口核对 | 仅覆盖 32 位标量寄存器和 `ap_ctrl_hs`；不支持 DMA 与外部内存；尚无 PYNQ 实板及跨板验证 |

## 运行环境及证据

- 各 Skill 的 `README.md` / `SKILL.md` 给出独立命令与依赖；硬件原理图绘制按路线可能需要 `schemdraw` 或 `matplotlib`。PCB 检查脚本有项目默认路径，跨项目必须显式指定输入。
- 硬件历史案例与验证情况见下方第 1～3 节；相关项目数据和原始轨迹并不全部包含在本技能包中。
- 算法包原文与合成测试：[algorithm_bundle/README.md](algorithm_bundle/README.md)、[VALIDATION_REPORT.md](algorithm_bundle/VALIDATION_REPORT.md)。
- PS–PL 交付技能的来源、验证案例与边界见下方第 7 节。
- 算法与 FPGA 协作记录合稿：[llm_collaboration.md](../report/llm_collaboration.md)。

本次合包目录仍包含 `README.md`、`SKILL.md`、部分大写参考文件等名称。

## 八项技能的使用说明

以下内容以本目录为技能包根目录。Windows PowerShell 命令使用本机的 `D:\Dev\Python314\python.exe`；在其他电脑使用时，请将该路径替换为实际 Python 路径。执行前应阅读每项的 `SKILL.md`，并准备相应输入。

### 1. 原理图程序化生成与自检

**入口：** [kicad-sch-codegen/SKILL.md](kicad-sch-codegen/SKILL.md)。

**适用与使用：** 需要生成 PNG 原理图或 KiCad 工程时，先根据交付对象选择路线：小型模拟图可使用 `schemdraw`；多引脚符号可使用 `reference/` 中的绘图工具和 `audit()`；交付 EDA 工程时应选择目标软件支持的 KiCad 版本。生成后执行连接检查，并在目标软件中检查导入结果。不同路线可能需要 `matplotlib` 或 `schemdraw`。

**来源与效果：** AFE 原理图生成过程中，符号引脚和导线端点曾出现 6 个绘图单位的坐标偏差，`audit()` 检查发现了脱接。原交接记录还记载了双通道原理图工程交付，以及电气规则检查（ERC）0 错误、0 警告。这些结果属于当时的设计检查，不能单凭 ERC 证明电路意图正确。

**失效条件：** 连线检查不能证明电路设计意图正确；示例中的项目专用元件和坐标需按新设计修改，KiCad 版本兼容性需在目标工具中验证。

### 2. 大模型辅助硬件调试的纠错

**入口：** [llm-hw-debug-pitfalls/SKILL.md](llm-hw-debug-pitfalls/SKILL.md)。

**适用与使用：** 模型对模拟电路、仪器读数或异常波形给出解释时，先核对接线与仪器设置，再用基本方程计算预期值；为假设设计可判定的排除实验，并用已知答案的对照组验证自造判据。

**来源与效果：** 一次面包板排查中，原交接记录记载模型先后给出 6 次错误解释，均由实测纠正，其中两次是错误前提下的推理。另一次检查中，ERC 没有报错，但独立数值仿真发现钳位二极管位置错误；充电器数据分析中，模型提出的判据也被对照数据推翻。历史案例见该技能的 `reference/`。

**失效条件：** 没有现场测量时不能把推断当作验证；纯数字设计的功能和时序需另用仿真和工具报告。案例中的数字属于历史台架，不直接代表当前 PCB 性能。

### 3. PCB 与原理图交叉审计

**入口：** [pcb-sch-cross-audit/SKILL.md](pcb-sch-cross-audit/SKILL.md)。

**适用与使用：** 检查 KiCad PCB 与原理图的焊盘网络、封装、引脚数和文字标注。

**来源与效果：** 原交接记录记载，一次双通道 AFE 审计涉及 49 个封装、101 个焊盘和 21 个网络：逐焊盘网络比较为 0 差异，但丝印 `V_VB` 对应的焊盘实际连接到 `V_OUT`，另发现占位封装需替换。文字检查的缺陷版输出 4 条警告，修复版为 0 条；警告条数不等于独立缺陷数量。这是历史板版次的检查记录。

**失效条件：** 原理图和 PCB 同时存在相同错误时可能通过；本工具不代替走线、电磁兼容和机械检查。脚本有项目默认路径，跨项目必须显式指定输入。

### 4. 时序数据划分与实验完整性

**入口：** [ml-experiment-integrity/SKILL.md](algorithm_bundle/ml-experiment-integrity/SKILL.md)。

**适用与使用：** 检查同一设备、模式或采集会话是否跨训练集、验证集和测试集。

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

**适用与使用：** 对 Zynq PS–PL 模型移植、识别链路及交付文件进行核验。先记录固件、模型、接口版本、数据单位与形状，再根据任务阅读 `references/` 并运行 `scripts/` 中的比较或校验工具。

**来源与效果：** 旧版 BOOT 与页面版本不匹配及中文路径编码问题促成了交付完整性检查；CNN 从 PS 迁移至 PL 时，同时比较分类输出（logits）与特征向量。数值比较工具覆盖相同值、容差内、超差、NaN 和长度错误用例，交付校验也包含篡改与路径检查的正反例。本次合包自测通过。

识别故障排查区分采样不稳定（`SOURCE_UNSTABLE`）和辅助未知暴露拒识（`AUXILIARY_UNKNOWN_EXPOSURE`）等不同阶段；旧日志复盘用于确定故障位置。历史仿真统计还曾混入两次参数拒绝事务，重新核算时应排除无效请求，并依据有效事务周期和时钟计算延迟。

**失效条件：** 哈希一致不能证明功能正确；输入预处理、单位和输出顺序不一致时不能直接比较数值。日志缺失阶段信息时无法可靠定位，低频快照不能还原原始波形；仿真或主机回归不能代替板端实测。该工作流含裸机 Zynq 项目假设，跨板使用需核对地址、缓存、数据精度、时钟及复位。

### 8. 通用 PYNQ Overlay 与加速器验证

**入口：** [pynq-overlay-validate/SKILL.md](pynq-overlay-validate/SKILL.md)。

**适用与使用：** 检查 PYNQ Overlay 元数据、标量寄存器接口、加速器输出与黄金结果，并生成 JSON 和 HTML 报告。接口契约与具体板卡的 bitstream（可编程逻辑配置文件）和 HWH（硬件接口描述文件）分开，工具不绑定本项目的电器类别或固定地址。

查看输出目录中的 `report.html` 或 `report.json`；重复运行时使用新的输出目录。首次实板运行前，按 [接口契约](pynq-overlay-validate/references/contract.md) 和 [板端操作说明](pynq-overlay-validate/references/board_run.md) 准备目标板对应的 `.bit`、`.hwh`、寄存器契约与黄金样例；`--download` 会编程可编程逻辑，具体命令见技能入口。

**来源与效果：** 新交接包提供 27 项主机行为测试、加法和乘法两套 mock 算例，以及两个示例 HLS 工程的综合与接口核对记录；证据见 [验证记录](pynq-overlay-validate/references/validation.md)。

**失效条件：** 当前支持 32 位标量 AXI-Lite 和 `ap_ctrl_hs`，不支持 DMA、外部内存指针等接口。mock 和 HLS 记录不能证明 PYNQ 实板或跨板运行；通用复用仍需目标板的 Overlay 与实测记录。报告的主机事务耗时含软件访问和轮询，不是 PL 纯计算延迟。本项目现有 PS–PL 程序为裸机流程，这项通用技能独立于该流程。

