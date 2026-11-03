param([string]$Gcc='gcc',[string]$Gxx='g++')
$ErrorActionPreference='Stop'
$root=(Split-Path $PSScriptRoot -Parent).Replace('\','/')
$src="$root/src/ps";$out="$root/work/cnn_test"
New-Item -ItemType Directory -Force $out | Out-Null
$objects=@()
$sources=@('nilm_infer','nilm_assets','nilm_dynamic','nilm_dorm_assets','nilm_open','nilm_open_assets','nilm_learn','nilm_legacy') | ForEach-Object {"$src/$_.c"}
$sources+="$root/sim/tests/host_store.c"
foreach($source in $sources){
 $obj="$out/$([IO.Path]::GetFileNameWithoutExtension($source)).o"
 & $Gcc -O2 -ffp-contract=off -I $src -c $source -o $obj
 if($LASTEXITCODE){throw "Compile failed: $source"};$objects+=$obj
}
& $Gxx -O2 -ffp-contract=off -I $src -I "$root/src/hls" "$root/src/hls/cnn.cpp" "$root/sim/tests/test_cnn.cpp" @objects -o "$out/test_cnn.exe"
if($LASTEXITCODE){throw 'CNN parity build failed'}
& "$out/test_cnn.exe" | Tee-Object "$root/sim/results/cnn_parity.log"
if($LASTEXITCODE){throw 'CNN numerical parity failed'}
