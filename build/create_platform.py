"""Run with Vitis 2025.2: vitis -s build/create_platform.py. No hardware access."""
from pathlib import Path
import json
import vitis
root=Path(__file__).resolve().parents[1]
xsa=root/'work/hardware/nilm_board.xsa'
if not xsa.exists():xsa=root/'board/hardware/nilm_board.xsa'
assert xsa.is_file()
workspace=root/'work/vitis_short'
assert not (workspace/'p').exists(), 'Use a fresh work/vitis directory; preserve any previous build separately.'
client=vitis.create_client()
try:
    client.set_workspace(str(workspace))
    platform=client.create_platform_component(name='p',hw_design=str(xsa),os='standalone',cpu='ps7_cortexa9_0',domain_name='standalone',compiler='gcc')
    domain=platform.get_domain(name='standalone')
    for lib in ('lwip220','xilffs'):domain.set_lib(lib_name=lib)
    domain.set_config(option='proc',param='proc_extra_compiler_flags',value=' -O2 -g -Wall -Wextra -fno-tree-loop-distribute-patterns -DFPU_HARD_FLOAT_ABI_ENABLED=1')
    for row in json.loads((root/'build/bsp_options.json').read_text()):
        domain.set_config(option='lib',param=row['param'],value=row['value'],lib_name=row['lib'])
    fsbl=platform.get_domain(name='zynq_fsbl')
    fsbl.set_config(option='lib',param='XILTIMER_sleep_timer',value='ps7_scutimer_0',lib_name='xiltimer')
    platform.build()
    export=workspace/'p/export/p/sw'
    required=[export/'standalone/lib'/n for n in ('libxil.a','libxilstandalone.a','libxiltimer.a','liblwip220.a','libxilffs.a')]+[export/'boot/fsbl.elf']
    assert all(p.is_file() and p.stat().st_size>0 for p in required), 'Platform build did not produce required libraries and FSBL'
    (root/'build/reports/reproduction/platform_build.json').write_text(json.dumps({'passed':True,'xsa':str(xsa),'artifacts':[str(p) for p in required]},indent=2))
    print('PASS_PLATFORM_BUILD')
finally:vitis.dispose()
