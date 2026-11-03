param([string]$AmdRoot='C:/AMDDesignTools/2025.2',[ValidateRange(0,3)][int]$Case=0)
$ErrorActionPreference='Stop'
$root=(Split-Path $PSScriptRoot -Parent).Replace('\','/')
if(!(Test-Path "$root/work/hls/hls/syn/report/nilm_cnn_csynth.rpt")){throw 'Run build_hls.ps1 first'}
$cfg=Get-Content "$root/work/hls.cfg" -Raw
$cfg+="`ntb.file=$root/sim/tests/test_cnn.cpp`ntb.file=$root/sim/tests/host_store.c`n"
foreach($name in @('nilm_infer','nilm_assets','nilm_dynamic','nilm_dorm_assets','nilm_open','nilm_open_assets','nilm_learn','nilm_legacy')){$cfg+="tb.file=$root/src/ps/$name.c`n"}
$cfg+="tb.cflags=-I$root/src/ps -I$root/src/hls -DPL_COSIM -DPL_CASE=$Case -ffp-contract=off`ncosim.rtl=verilog`ncosim.tool=xsim`ncosim.trace_level=none`n"
$cfg | Set-Content "$root/work/cosim.cfg" -Encoding ascii
& "$AmdRoot/Vitis/bin/vitis-run.bat" --mode hls --cosim --config "$root/work/cosim.cfg" --work_dir "$root/work/hls" *> "$root/sim/results/cosim_case$Case.log"
if($LASTEXITCODE -or (Get-Content "$root/sim/results/cosim_case$Case.log" -Raw) -notmatch 'C/RTL co-simulation finished: PASS'){throw "RTL case $Case failed; inspect log"}
Write-Output "PASS_COSIM case $Case"
