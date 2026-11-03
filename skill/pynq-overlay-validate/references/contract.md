# 配置与报告契约

两个 JSON 文件分别描述硬件接口与黄金样例。路径相对于契约文件目录解析；不执行配置内的 Python/Tcl 表达式。

## 接口文件

参见 [整数加法示例](../assets/add_contract.json) 与 [浮点乘法示例](../assets/multiply_contract.json)。

| 字段 | 规则 |
|---|---|
| `schema_version` | 1 |
| `interface` | `axi_lite_ap_ctrl_hs` |
| `name` | 项目自定义标签 |
| `overlay.bitfile` | `.bit` 路径；真实运行必须存在同目录、同 stem 的 `.hwh` |
| `overlay.bit_sha256`, `overlay.hwh_sha256` | 可选的64位十六进制期望校验值；提供时在加载前核对 |
| `ip` | `ip_dict` 中的实例完整路径，如 `accel_add_0` 或 `compute/accel_multiply_0` |
| `vlnv` | 可选；提供时与 HWH 类型严格匹配 |
| `control` | `register` 为 HWH 控制寄存器名称，`offset` 为32位对齐偏移 |
| `inputs`, `outputs` | 非空列表；每项有 `name`、`register`、`offset`、`dtype` |
| `dtype` | `uint32` / `int32` / `float32`；数值按小端32位字编码 |
| `atol`, `rtol` | 浮点输出容差，非负且有限；默认0；整数只能精确比较 |
| `timeout_s` | 单次 idle 或 done 轮询上限，默认2秒，范围 (0,60] |
| `poll_interval_s` | 默认0.001秒，至少0.0001且不大于 timeout |
| `mock.operation` | 无板模式使用 `u32_add` 或 `f32_multiply`，两输入一输出 |
| `mock.base_address` | 只构造模拟元数据；真实地址始终来自 HWH |

`name` 是样例中的逻辑键，`register` 是 HWH/register_map 属性名，两者不必相同。当前寄存器名限定为以英文字母开头的字母、数字和下划线，避免隐式猜测 PYNQ 属性名转换。

严格检查：偏移重叠/未对齐、名称重复、缺少寄存器、HWH 大小非32位、访问方向错误或超出地址范围都会终止。整数禁止隐式截断、越界和布尔值；输入及黄金值拒绝 NaN/Inf。

控制寄存器必须含 `AP_START=bit0`、`AP_DONE=bit1`、`AP_IDLE=bit2`、`AUTO_RESTART=bit7`，每项宽度1。适配其他控制协议应编写专门后端，不能只改掩码绕过检查。

同 stem 并不自动证明两个文件来自同一次构建。交付者应保证 `.bit/.hwh` 配套，并建议配置期望 SHA256；报告保存实际哈希用于追溯。

## 黄金样例

参见 [add_cases.json](../assets/add_cases.json)。结构：

```json
{"schema_version":1,"cases":[{"id":"case_1","inputs":{"a":2,"b":3},"expected":{"sum":5}}]}
```

ID 唯一，输入与期望键必须与契约完全一致。黄金结果来自独立计算或受控参考，不是先读回板端输出再写成期望。

整数逐值完全相等；浮点条件为 `abs(actual-expected) <= atol + rtol*abs(expected)`。输出 NaN/Inf 判失败。第一个失败后停止，报告保留此前成功结果及实际值，不混入上次运行的字段。

## 报告

每次输出 `report.json` 与 `report.html`。已有 report.json 的目录禁止覆盖。

- `backend` / `operation`：mock、pynq；inspect、run。
- `status`：PASS / FAIL / ERROR / INSPECTED。
- `hardware_executed`：是否进入 PYNQ 模式的事务调用路径；不单独代表成功完成。
- `hardware_validation_passed`：仅 PYNQ 模式全部黄金样例通过时为 true；mock 和 inspect 恒为 false。
- `contract_sha256` / `cases_sha256` / `artifacts`：契约、样例及实际 bit/hwh 指纹。
- `ip_metadata`：IP 地址、范围、类型、寄存器元数据；不读取全部寄存器值。
- `cases`：实际输出、期望值、误差、比较结果和主机观测耗时。
- `error` / `active_case`：异常类型、阶段原因和当时正在处理的样例。

PYNQ 报告记录 Python/PYNQ 环境、操作者板卡标签与可获取的设备树型号。状态、版本和哈希由运行时生成；测试替身不构成真实硬件证据。
