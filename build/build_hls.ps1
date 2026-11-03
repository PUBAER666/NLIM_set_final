param([string]$AmdRoot='C:/AMDDesignTools/2025.2')
$ErrorActionPreference='Stop'
$root=(Split-Path $PSScriptRoot -Parent).Replace('\','/')
New-Item -ItemType Directory -Force "$root/work" | Out-Null
@"
part=xc7z020clg484-1
[hls]
flow_target=vivado
syn.top=nilm_cnn
syn.file=$root/src/hls/cnn.cpp
syn.cflags=-I$root/src/hls -ffp-contract=off
syn.compile.pipeline_loops=0
clock=10ns
package.output.format=ip_catalog
package.output.file=$root/work/nilm_cnn.zip
"@ | Set-Content "$root/work/hls.cfg" -Encoding ascii
& "$AmdRoot/Vitis/bin/v++.bat" -c --mode hls --config "$root/work/hls.cfg" --work_dir "$root/work/hls"
if($LASTEXITCODE){throw 'HLS synthesis failed'}
