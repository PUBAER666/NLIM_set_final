# 可复用技能包

| Skill | 用途 | 适用环境与证据 |
|---|---|---|
| [ps-pl-release-validation](ps-pl-release-validation/SKILL.md) | 交付完整性、黄金数值、事件诊断和性能证据检查 | 现有裸机 PS–PL 工作流；通用脚本在主机运行 |
| [pynq-overlay-validate](pynq-overlay-validate/SKILL.md) | Overlay 元数据、标量 AXI-Lite 接口契约、加速器黄金样例及报告 | 独立 PYNQ 工具；27项主机行为测试、两套 mock、两个示例 HLS 综合/接口核对 |

Skill 可由支持该格式的智能体读取，也可直接运行其中脚本。正文与示例自包含；在线资料用于查阅 API，不要求运行时联网。

## 通用 PYNQ 验证

进入 `pynq-overlay-validate` 后，在没有开发板的电脑上可执行：

```sh
python scripts/validate_overlay.py run --backend mock --contract assets/add_contract.json --cases assets/add_cases.json --out runs/add
```

打开输出目录中的 `report.html` 即可查看结果。切换实际 PYNQ 环境时，提供目标板专用 `.bit/.hwh`、配置寄存器契约并使用 `--backend pynq`；`--download` 会显式编程 PL。详细步骤与适用边界见 [SKILL.md](pynq-overlay-validate/SKILL.md)。

这份 Skill 不绑定电器模型和某个板卡地址；当前支持32位标量 AXI-Lite/ap_ctrl_hs，不覆盖 DMA、外部内存指针或所有 PYNQ 加速器。两个 mock 算例不是两块板的实测。跨板复用条件与已有证据见 [验证记录](pynq-overlay-validate/references/validation.md)。

