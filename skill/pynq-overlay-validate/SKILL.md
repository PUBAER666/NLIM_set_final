---
name: pynq-overlay-validate
description: Validate PYNQ Overlay metadata and scalar AXI-Lite ap_ctrl_hs accelerators using configurable register contracts, golden cases, bounded polling, and evidence reports. Use for reusable PYNQ IP bring-up and regression across project-specific overlays; not for bare-metal firmware, DMA, or external-memory accelerator execution.
---

# PYNQ Overlay 验证

把板卡专用 `.bit/.hwh` 与软件接口契约分离，复用同一验证器完成接口核对、标量输入、加速器调用、黄金结果比对和 JSON/HTML 报告。工具不绑定特定电器模型、类别或物理基地址。

## 使用范围

- PYNQ Python 环境，`Overlay` / `ip_dict` / `is_loaded()` / `register_map` 可用。
- AXI-Lite 32 位标量寄存器；`uint32`、`int32`、`float32`；标准 HLS `ap_ctrl_hs`。
- 支持顶层或层级内的 IP 路径、多个标量输入/输出；物理地址来自 HWH。
- `mock` 是标准库算术模型；`pynq` 才会调用 PYNQ。实际板卡验证必须有其专用 bitstream、匹配 HWH 和运行记录。
- 不用于 `m_axi` 指针、AXI-Stream/DMA、`ap_ctrl_none`、`ap_ctrl_chain`、64 位寄存器或部分重配置。当前版本不分配 PL 访问的外部内存。

## 工作流程

1. 阅读 [接口契约](references/contract.md)，核对硬件导出的寄存器名称、偏移、类型和控制协议；不要把一个板卡的 bitstream 换名后用于另一板卡。
2. 准备契约 JSON 与独立黄金样例。完整字段与实际 HWH 对齐，所有样例在访问 IP 前预校验。
3. 运行 `inspect` 获取 `ip_dict` 中的地址与寄存器元数据，不编程 PL、不调用加速器。元数据不匹配时，先修正契约或硬件。
4. 确定板卡、当前 Overlay 和调用权限。已有明确操作授权时直接执行；`--download` 表示本次将重编程 PL，不由 `inspect` 或 mock 隐式触发。
5. 执行 `run`：确认 idle 且未启用 auto-restart → 写输入 → AP_START → 有限轮询 AP_DONE → 按输出名称读取 `register_map` → 与黄金结果比较。AP_DONE 可能读清，禁止先打印整个 `register_map` 再等待完成。
6. 检查报告；首次数值失败或运行错误就停止，不自动重试或反复重编程。超时保留阶段和控制值，按 [实机操作与诊断](references/board_run.md) 排查。

## 无板快速运行

以下命令在本 Skill 目录执行，Python 3.9+，无须安装 PYNQ 或 NumPy：

```sh
python scripts/validate_overlay.py run --backend mock --contract assets/add_contract.json --cases assets/add_cases.json --out runs/add
python scripts/validate_overlay.py run --backend mock --contract assets/multiply_contract.json --cases assets/multiply_cases.json --out runs/multiply
python scripts/test_validator.py --report runs/host_tests.json
```

每次使用新的输出目录。查看 `report.json` 或打开 `report.html`；mock 报告始终标明 `hardware_validation_passed=false`。模拟器使用独立加法/乘法运算，不从黄金结果复制输出。

## PYNQ 实机运行

在已安装 PYNQ 的板卡 Linux/Python 环境中，放入板卡专用 `adder.bit` 和同名 `adder.hwh`，修改契约中的路径和 IP 实例名。示例不附通用 bitstream。

```sh
python scripts/validate_overlay.py inspect --backend pynq --contract assets/add_contract.json --out runs/inspect
python scripts/validate_overlay.py run --backend pynq --download --board-id lab_board_a --contract assets/add_contract.json --cases assets/add_cases.json --out runs/board_add
```

若正确 Overlay 已加载，可省略 `--download`；若未加载则报错。`--board-id` 是操作者填写的标签，报告另外记录系统架构、PYNQ 版本及可读取的设备树信息，不将标签当作自动识别的板卡型号。

## 证据与复用

验证状态及现有结果见 [验证记录](references/validation.md)。无板测试只证明软件流程和注入接口行为；HLS/主机算例测试不是 PYNQ 板测。跨板卡复用要求分别提供对应 Overlay 和报告，不能用两个 mock 地址宣称已跨板验证。

计时是 Python 主机侧一次寄存器事务的观测耗时，包含 I/O 与轮询，不是 PL 纯计算时延或加速倍数。资源数据应引用对应综合/实现报告，工具不凭空生成资源利用率。

独立示例硬件与接入步骤见 [示例 Overlay](references/example_overlay.md)。API 依据见 [官方资料](references/sources.md)。
