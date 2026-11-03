"""Build one optional demonstration HLS IP; no board connection/programming."""
import argparse
from pathlib import Path
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--vpp', required=True, help='Path to Vitis 2025.2 v++ executable')
    p.add_argument('--part', required=True, help='Exact FPGA device part for the target board')
    p.add_argument('--top', choices=['accel_add', 'accel_multiply'], required=True)
    p.add_argument('--clock-ns', type=float, default=10.0)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    if not (0 < a.clock_ns < 1000) or any(x in a.part for x in '\n\r='):
        p.error('Invalid clock or part')
    # Keep existing results intact; every build gets an explicit fresh directory.
    a.out.mkdir(parents=True, exist_ok=False)
    out = a.out.resolve()
    source = Path(__file__).resolve().parents[1]/'assets/example_hls.cpp'
    config = out/'hls.cfg'
    config.write_text(f'part={a.part}\n[hls]\nflow_target=vivado\nsyn.top={a.top}\nsyn.file={source.as_posix()}\nclock={a.clock_ns}ns\npackage.output.format=ip_catalog\npackage.output.file={(out/(a.top+".zip")).as_posix()}\n', encoding='utf-8')
    with (out/'build.log').open('w',encoding='utf-8') as log:
        result = subprocess.run([a.vpp, '-c', '--mode', 'hls', '--config', str(config), '--work_dir', str(out/'hls')], stdout=log, stderr=subprocess.STDOUT)
    print('HLS build exit:', result.returncode, '; log:', out/'build.log')
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
