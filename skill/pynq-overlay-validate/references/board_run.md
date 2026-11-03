# PYNQ 板卡运行与诊断

## 准备

需要板卡支持的 PYNQ Linux 环境、可用 Python、该板专用 bitstream 及匹配 HWH。此工具基于 PYNQ 公开 Overlay / register_map API 编写；目标环境仍应以实际运行结果确认兼容性。不要把当前裸机 SD 启动包当成 PYNQ 镜像。

将本 Skill 文件夹复制到板卡。选择已有兼容标量 IP，或按 [示例接入](example_overlay.md) 生成演示 Overlay。先确认 IP 没有 m_axi/DMA/AXI-Stream 接口需要本工具管理。

1. 停止其他使用该 IP 的程序，核对板卡型号与 bitstream 构建目标。
2. 在契约中填写实际 bitfile 路径、IP 实例路径、HWH 寄存器名称/偏移，可添加期望 SHA256。
3. 执行 SKILL.md 中的 `inspect`。它不编程 PL；若契约不符，报告保留已获取的元数据供修正。
4. 在具有板端设备访问权限的 Python 环境执行 `run --backend pynq --download --board-id ...`。`--download` 会替换当前 PL 设计。
5. 打开报告，核对 `backend=pynq`、实际 PYNQ 版本、设备树信息、Overlay 哈希和每条黄金结果。保存原始报告及硬件构建来源。

## 常见问题

| 现象 | 核查方向 |
|---|---|
| 找不到 pynq | 是否在 PYNQ 板卡的 Python 环境，而非 Windows 主机 |
| `.hwh` 缺失/寄存器为空 | 导出配套硬件元数据；确认与 bit 同目录同 stem |
| IP/寄存器名或偏移不符 | 看 report.json 的 ip_metadata 和导出 HWH/驱动头文件，不用模型名称推测地址 |
| Overlay 未加载 | 确认已有 Overlay 是否一致；需要时显式加 --download |
| AUTO_RESTART 或 idle 超时 | 停止当前持有 IP 的应用；检查控制协议与复位，不强制覆盖正在运行的 IP |
| Completion timeout | 检查 AXI 时钟、复位、ap_ctrl_hs、地址映射及实际 IP；不重复发送 start |
| 浮点数完全不合理 | 检查是否采用32位 IEEE754 位模式而非整数数值写入 |
| 金值差一个或仅大数失败 | 检查有符号性、溢出语义；整数比较不转为浮点，不放宽容差掩盖问题 |

轮询期限依赖单次 MMIO 读能返回；AXI 总线硬挂起导致的读阻塞无法用 Python 轮询期限保证终止，需要板端硬件复位或重新上电。标量流程不分配 PL 可访问的外部内存，因此不会在超时后释放仍被 DMA 使用的缓冲区。

## 跨项目与跨板卡

跨项目：替换契约、样例和对应 Overlay，不修改验证器。跨板卡：为每块板生成自己的匹配 Overlay，记录板卡与 PYNQ 镜像版本，分别跑同类样例。

两个 mock 配置仅展示 IP 路径、地址和数据类型可配置；不等于两块板实测。单板 PASS 也不能外推所有型号。具体可复用条件是接口协议、寄存器类型与 PYNQ API 满足本 Skill 的范围。
