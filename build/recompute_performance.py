"""Recheck arithmetic against the saved tool-report metrics, not a benchmark."""
import json, math
from pathlib import Path
root=Path(__file__).resolve().parents[1]
m=json.loads((root/'build/reports/performance/metrics.json').read_text(encoding='utf-8-sig'))
assert m['clock_hz']>0
assert m['parallel_output_channels']==4 and m['core_pipeline_ii']==1
for cycles,ms in [('hls_latency_max_cycles','hls_latency_estimate_ms'),('rtl_valid_transaction_cycles','rtl_valid_latency_ms')]:
    assert math.isclose(m[cycles]/m['clock_hz']*1000,m[ms],rel_tol=1e-12)
assert math.isclose(m['nominal_input_window_ms']/m['hls_latency_estimate_ms'],m['single_cnn_budget_ratio'],rel_tol=1e-12)
assert m['ps_speedup'] is None and m['end_to_end_speedup'] is None
assert not m['hardware_benchmarked']
print(json.dumps({'passed':True,'kind':'arithmetic_check_not_measurement','hls_ms':m['hls_latency_estimate_ms'],'rtl_ms':m['rtl_valid_latency_ms'],'ideal_core_ratio':4,'ps_speedup':None},indent=2))
