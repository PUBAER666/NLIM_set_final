"""Extract measurements from original tool reports; never substitute estimates for RTL."""
from pathlib import Path
import argparse, hashlib, json, re, xml.etree.ElementTree as ET

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--synthesis-only',action='store_true')
args=parser.parse_args()

def synth(folder, name='nilm_cnn'):
    e=ET.parse(folder/f'{name}_csynth.xml').getroot()
    def val(path):
        s=e.findtext(path)
        if s is None: raise ValueError(path)
        return float(s)
    cycles=int(val('PerformanceEstimates/SummaryOfOverallLatency/Worst-caseLatency'))
    ns=val('UserAssignments/TargetClockPeriod')
    resources={x.tag:int(x.text) for x in e.find('AreaEstimates/Resources')}
    return dict(worst_latency_cycles=cycles,worst_latency_ms=cycles*ns/1e6,
                target_clock_ns=ns,estimated_clock_ns=val('PerformanceEstimates/SummaryOfTimingAnalysis/EstimatedClockPeriod'),
                resources=resources)

def rtl(path):
    text=path.read_text(encoding='utf-8')
    line=next(x for x in text.splitlines() if '|   Verilog|' in x)
    cells=[x.strip() for x in line.split('|')[1:-1]]
    assert cells[1].lower()=='pass'
    # Two ABI/model rejection transactions and exactly one full inference.
    # The report average therefore is NOT inference latency.
    return dict(min_cycles=int(cells[2]),report_avg_cycles=int(cells[3]),valid_cycles=int(cells[4]),
                valid_ms=int(cells[4])/100000.0,passed=True)

four=synth(root/'evidence/four_fresh_synth')
single=synth(root/'evidence/single_fresh_synth')
assert four['target_clock_ns']==single['target_clock_ns']==10
archive=(root/'evidence/four_archived/cnn_csynth.rpt').read_text(encoding='utf-8')
old_area=re.search(r'\|Total\s*\|\s*(\d+)\|\s*(\d+)\|\s*(\d+)\|\s*(\d+)\|\s*(\d+)\|',archive)
assert old_area
assert list(map(int,old_area.groups())) == [four['resources'][k] for k in ['BRAM_18K','DSP','FF','LUT','URAM']]
assert four['worst_latency_cycles']==3002349
assert (root/'src/four/cnn.cpp').read_bytes()==(root/'evidence/four_archived/cnn.cpp').read_bytes()
assert (root/'tests/test_cnn.cpp').read_bytes()==(root/'evidence/four_archived/test_cnn.cpp').read_bytes()
assert (root/'src/four/weights.h').read_bytes()==(root/'src/single/weights.h').read_bytes()
assert hashlib.sha256((root/'src/four/weights.h').read_bytes()).hexdigest()==json.loads((root/'src/four/weights_manifest.json').read_text(encoding='utf-8'))['weights_sha256']
host=(root/'results/host_pair.log').read_text(encoding='utf-8').strip()
assert '24 inputs' in host and 'bit-identical' in host and 'worst_abs=0' in host
repro=json.loads((root/'results/four_rtl_reproduction.json').read_text(encoding='utf-8'))
assert repro['all_fresh_verilog_and_dat_files_identical'] and repro['file_count']==68
for row in repro['files']:
    archived=root/'evidence/four_archived/rtl'/row['name']
    assert hashlib.sha256(archived.read_bytes()).hexdigest()==row['sha256']
progress=[]
for case in range(4):
    log=(root/f'evidence/four_archived/cosim_case{case}.log').read_text(encoding='utf-8',errors='replace')
    times=re.findall(r'RTL Simulation : (\d+) / 3 \[[^\]]+\] @ "(\d+)"',log)
    assert len(times)==4 and 'C/RTL co-simulation finished: PASS' in log
    progress.append(times)
assert all(times==progress[0] for times in progress)
layers=[]
for name in ['layer_4096_1_8_9_true_s','layer_2048_8_16_7_true_s','layer_1024_16_32_5_false_s']:
    a=synth(root/'evidence/single_fresh_synth',name)
    b=synth(root/'evidence/four_fresh_synth',name)
    layers.append(dict(name=name,single_ms=a['worst_latency_ms'],four_ms=b['worst_latency_ms'],ratio=a['worst_latency_cycles']/b['worst_latency_cycles']))
data=dict(evidence_type='matched_hls_synthesis_and_rtl_cosimulation_not_board_measurement',
          part='xc7z020clg484-1',tool='Vitis HLS 2025.2',clock_mhz=100,
          single=single,four=four,layers=layers,
          synthesis_speedup=single['worst_latency_cycles']/four['worst_latency_cycles'],
          synthesis_latency_reduction_percent=(1-four['worst_latency_cycles']/single['worst_latency_cycles'])*100,
          host_validation=dict(cases=24,outputs_per_case=78,bit_identical_between_variants=True,worst_absolute_error_vs_ps=0),
          four_archived_cases_have_identical_transaction_times=True,
          four_fresh_rtl_files_identical_to_archived=68,
          four_rtl_provenance='Archived 2026-10-31; matching source/testbench, model weight hash, tool/version/clock, and fresh synthesis metrics.',
          board_measured=False,ps_vs_pl_speedup=None,end_to_end_speedup=None)
if not args.synthesis_only:
    assert json.loads((root/'results/run_status.json').read_text(encoding='utf-8'))['status']=='complete'
    log=(root/'results/single_cosim.log').read_text(encoding='utf-8',errors='replace')
    assert 'C/RTL co-simulation finished: PASS' in log and 'worst_abs=0' in log
    old=(root/'evidence/four_archived/cosim_case3.log').read_text(encoding='utf-8',errors='replace')
    assert 'C/RTL co-simulation finished: PASS' in old and 'worst_abs=0' in old
    data['single_rtl']=rtl(root/'evidence/single_fresh_cosim/nilm_cnn_cosim.rpt')
    data['four_rtl']=rtl(root/'evidence/four_archived/cnn_cosim.rpt')
    data['rtl_speedup']=data['single_rtl']['valid_cycles']/data['four_rtl']['valid_cycles']
    data['rtl_latency_reduction_percent']=(1-data['four_rtl']['valid_cycles']/data['single_rtl']['valid_cycles'])*100
    data['rtl_input']='case 3: deterministic fundamental + third harmonic + seeded noise; 4096 float32 samples; 2 invalid requests excluded'
name='synthesis_metrics.json' if args.synthesis_only else 'comparison_metrics.json'
(root/'results'/name).write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(data,ensure_ascii=False,indent=2))
