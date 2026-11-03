# 验证记录

本目录中的记录是本地软件验证与示例验证，不能作为真实 PYNQ 板卡或跨板卡验证结论。

| 项目 | 证据 | 范围 |
|---|---|---|
| 27项行为测试 | [host_tests.json](../validation/host_tests.json) | 主机上运行；PYNQ API 使用替身 |
| 无符号加法样例 | [加法报告](../validation/mock_add/report.json) | 独立算术 mock，含32位回绕与高位数值 |
| 浮点乘法样例 | [乘法报告](../validation/mock_multiply/report.json) | 独立算术 mock，使用另一 IP 层级、地址与 float32 类型 |
| 加法 IP 综合及接口核对 | [HLS 加法结果](../validation/hls_add/result.json) | Vitis HLS 2025.2，目标 xc7z020clg484-1；并非 PYNQ 板测 |
| 乘法 IP 综合及接口核对 | [HLS 乘法结果](../validation/hls_multiply/result.json) | 同上；逐项核对真实导出寄存器与契约 |
| 示例 C++ 主机测试 | [example_source_test.json](../validation/example_source_test.json) | 测试源码算术行为，不证明硬件运行 |

实际 PYNQ 板卡：本次未连接，尚无实机报告。跨板卡硬件验证：尚无记录。工具已提供实际 PYNQ API 路径和板测操作步骤，参见 [board_run.md](board_run.md)。

行为测试覆盖：独立黄金误差、整数精确性、float容差/非有限值、输入范围、接口元数据和读写方向、层级 IP、文件配套与哈希、显式编程开关、idle/done 超时、旧 AP_DONE、auto-restart、报告覆盖及证据标签。

27项测试通过仅代表这些软件行为通过。申请加分时应提交本 Skill、可执行示例、适用条件及已有验证材料；是否满足通用 PYNQ Skill 的竞赛要求由赛事评审判断，不用模拟结果替代缺少的实机证据。

HLS 目录中的 component_metadata.xml 仅作为寄存器元数据证据，不能作为完整可安装 IP 使用。完整 IP 可由构建脚本重新导出；未将综合结果写成 RTL 协同仿真或板上结果。
