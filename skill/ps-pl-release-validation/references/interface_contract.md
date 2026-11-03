# Interface contract

Record input shape, units, endianness, alignment, cache ownership, output order, model tag, ABI and completion/error protocol. In the NILM reference, RAW1 contains paired ADC codes; CNN receives a preprocessed 4096-float current vector, and returns 14 logits plus 64 features. These are different interfaces and must not be substituted.

DMA or HLS AXI master memory accesses require CPU cache handling appropriate to the mapped region. Flush input before device access; invalidate completed output before CPU reads. Cancelling a caller does not prove the hardware transaction ended: drain or reset safely before buffer reuse. The NILM driver locks faulted staging memory until board reset on timeout.

Check interrupt preservation of floating-point registers when PS calculations use hard float. A numerical bug that disappears when interrupts stop can be platform state corruption, not trained weights. Golden vectors validate covered numerical paths, not field recognition accuracy.
