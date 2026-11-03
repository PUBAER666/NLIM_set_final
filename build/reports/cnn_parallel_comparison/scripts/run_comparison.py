"""Reproduce the isolated CNN comparison; requires Vitis 2025.2 and MinGW."""
from pathlib import Path
import argparse, json, subprocess, sys, time, shutil

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--amd', default='C:/AMDDesignTools/2025.2')
parser.add_argument('--gcc-dir', default='C:/mingw64/bin')
parser.add_argument('--work', required=True, help='Fresh, short work directory')
parser.add_argument('--stage', choices=['all','host','synth','cosim'], default='all')
parser.add_argument('--rtl-lane', choices=['single','four'], default='single')
args = parser.parse_args()
work = Path(args.work).resolve()
work.mkdir(parents=True, exist_ok=True)
results = root/'results'
ps = root/'src/reference'
names = ['nilm_infer','nilm_assets','nilm_dynamic','nilm_dorm_assets','nilm_open','nilm_open_assets','nilm_learn','nilm_legacy']

def state(stage, status, **extra):
    record = dict(stage=stage,status=status,time=time.strftime('%Y-%m-%d %H:%M:%S'),**extra)
    (results/'run_status.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(record),flush=True)

def run(command, stage, cwd=work):
    state(stage,'running',command=[str(c) for c in command])
    start = time.monotonic()
    with (results/f'{stage}.log').open('w',encoding='utf-8') as log:
        result = subprocess.run([str(c) for c in command],cwd=cwd,stdout=log,stderr=subprocess.STDOUT)
    state(stage,'pass' if result.returncode==0 else 'failed',exit_code=result.returncode,elapsed_seconds=round(time.monotonic()-start,3))
    if result.returncode:
        raise RuntimeError(f'{stage} failed; inspect {results/stage}.log')

def write_config(lane):
    lane_work=work/lane
    lane_work.mkdir(exist_ok=True)
    cfg = f'''part=xc7z020clg484-1
[hls]
flow_target=vivado
syn.top=nilm_cnn
syn.file={(root/'src'/lane/'cnn.cpp').as_posix()}
syn.cflags=-I{(root/'src'/lane).as_posix()} -ffp-contract=off
syn.compile.pipeline_loops=0
clock=10ns
package.output.format=ip_catalog
package.output.file={(lane_work/'nilm_cnn.zip').as_posix()}
'''
    config = lane_work/'hls.cfg'
    config.write_text(cfg,encoding='ascii')
    shutil.copy2(config,results/f'{lane}_hls.cfg')
    return config

try:
    if args.stage in ('all','host'):
        host=work/'host';host.mkdir(exist_ok=True)
        objects=[]
        for i,src in enumerate([ps/f'{n}.c' for n in names]+[root/'tests/host_store.c']):
            obj=host/f'{src.stem}.o';objects.append(obj)
            run([Path(args.gcc_dir)/'gcc.exe','-O2','-ffp-contract=off','-I',ps,'-c',src,'-o',obj],f'host_compile_{i}')
        single=host/'single.o'
        run([Path(args.gcc_dir)/'g++.exe','-O2','-ffp-contract=off','-Dnilm_cnn=nilm_cnn_single','-I',root/'src/single','-c',root/'src/single/cnn.cpp','-o',single],'host_single')
        exe=host/'test_pair.exe'
        run([Path(args.gcc_dir)/'g++.exe','-O2','-ffp-contract=off','-I',ps,'-I',root/'src/four',root/'src/four/cnn.cpp',root/'tests/test_pair.cpp',single,*objects,'-o',exe],'host_link')
        run([exe],'host_pair')
        assert 'PASS single/four bit-identical: 24 inputs' in (results/'host_pair.log').read_text(encoding='utf-8')
    if args.stage in ('all','synth'):
        for lane in ['single','four']:
            cfg=write_config(lane)
            run(['cmd.exe','/d','/c',Path(args.amd)/'Vitis/bin/v++.bat','-c','--mode','hls','--config',cfg,'--work_dir',work/lane/'hls'],f'{lane}_synth')
            reports=work/lane/'hls/hls/syn/report'
            out=root/'evidence'/f'{lane}_fresh_synth'
            shutil.copytree(reports,out,dirs_exist_ok=True)
            assert (out/'nilm_cnn_csynth.xml').exists()
    if args.stage in ('all','cosim'):
        lane=args.rtl_lane
        cfg=write_config(lane).read_text(encoding='ascii')
        for p in [root/'tests/test_cnn.cpp', root/'tests/host_store.c']+[ps/f'{n}.c' for n in names]:
            cfg+=f'tb.file={p.as_posix()}\n'
        cfg+=f'''tb.cflags=-I{ps.as_posix()} -I{(root/'src'/lane).as_posix()} -DPL_COSIM -DPL_CASE=3 -ffp-contract=off
cosim.rtl=verilog
cosim.tool=xsim
cosim.trace_level=none
'''
        config=work/lane/'cosim.cfg';config.write_text(cfg,encoding='ascii')
        shutil.copy2(config,results/f'{lane}_cosim.cfg')
        run(['cmd.exe','/d','/c',Path(args.amd)/'Vitis/bin/vitis-run.bat','--mode','hls','--cosim','--config',config,'--work_dir',work/lane/'hls'],f'{lane}_cosim')
        assert 'C/RTL co-simulation finished: PASS' in (results/f'{lane}_cosim.log').read_text(encoding='utf-8',errors='replace')
        sim=work/lane/'hls/hls/sim'
        out=root/'evidence'/f'{lane}_fresh_cosim';out.mkdir(exist_ok=True)
        for src in (sim/'report').rglob('*'):
            if src.is_file():
                dst=out/src.relative_to(sim/'report');dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
    state(args.stage,'complete')
except Exception as exc:
    state(args.stage,'failed',error=str(exc))
    raise
