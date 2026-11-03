# CNN parallel comparison

阅读 `comparison_report.md`；图片为 `comparison.png`（报告插图）及 `comparison.svg`（矢量版）。`results/comparison_metrics.json` 保存机器可读结果。

这是独立实验，不替换 NLIM_set_final 的 BOOT.BIN、模型或接收程序。

## Reproduce on Windows

需要 Python 3、MinGW GCC/G++、AMD Vitis 2025.2。工程与工作路径建议无空格、无中文。先备份现有证据，使用新的工作目录；运行会更新本实验目录中的日志。

```powershell
python scripts/run_comparison.py --work C:/nilm_cnn_compare_work --stage all
python scripts/summarize.py
python scripts/render_report.py
```

默认重跑 24 组主机测试、两路 HLS 综合、单路 case 3 RTL 仿真。四路 RTL 证据为逐字节匹配的既有设计，原始记录位于 `evidence/four_archived/`。如需另外重跑四路 RTL：

```powershell
python scripts/run_comparison.py --work C:/nilm_cnn_compare_work --stage cosim --rtl-lane four
```

`--amd` 和 `--gcc-dir` 可指定工具目录。生成图表额外需要 matplotlib 与 numpy。所有时延均由设计周期数与 10 ns 时钟换算；编译或仿真的电脑耗时不是硬件推理延迟。

## Layout

- `src/single/`：单路基线、冻结权重。
- `src/four/`：当前四路源码，保持原始字节。
- `src/reference/`：数值参考与链接所需依赖。
- `tests/`：原始 RTL 测试与单路／四路直接一致性测试。
- `scripts/`：运行、提取结果与生成报告。
- `evidence/`：原始综合报告、RTL 仿真报告和匹配的四路旧 RTL。
- `results/`：日志、哈希核对、源码差异与对比指标。

报告结果仅为硬件实现间的综合／RTL 仿真对比，不是板上实测或 PS-versus-PL 跑分。
