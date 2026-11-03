# 官方 API 依据

资料核对日期：2026-11-03。这里只记录具体 API 依据，目标板仍需实测。

- [PYNQ Overlay API](https://pynq.readthedocs.io/en/latest/pynq_package/pynq.overlay.html)：Overlay 构造、download、is_loaded、ip_dict。
- [PYNQ Overlay 实现](https://pynq.readthedocs.io/en/latest/_modules/pynq/overlay.html)：基于 HWH 的驱动、层级 IP 访问及 register_map。
- [PYNQ RegisterMap 元数据结构](https://pynq.readthedocs.io/en/latest/_modules/pynq/registers.html)：address_offset、access、size、fields 与属性映射。
- [PYNQ MMIO](https://pynq.readthedocs.io/en/latest/pynq_libraries/mmio.html)：32位寄存器读写。
- [AMD Vitis HLS 命令](https://docs.amd.com/r/2025.1-English/ug1399-vitis-hls/vitis-v-and-vitis-run-Commands)：v++ --mode hls 与工具流程；实际构建记录注明所用版本。

未来若扩展外部内存或 DMA，应另外实现分配、缓存同步、完成/超时和缓冲区生命周期协议，不能把本标量验证器的释放策略直接套用。参考 [PYNQ allocate](https://pynq.readthedocs.io/en/latest/pynq_libraries/allocate.html) 与 [PYNQ Buffer](https://pynq.readthedocs.io/en/latest/pynq_package/pynq.buffer.html)。
