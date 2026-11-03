param([string]$Gcc="gcc",[string]$Python="python")
$ErrorActionPreference='Stop'
$root=(Split-Path $PSScriptRoot -Parent).Replace('\','/')
$src="$root/src/ps";$out="$root/sim/results";New-Item -ItemType Directory -Force $out | Out-Null
$model=@("$src/nilm_assets.c","$src/nilm_open.c","$src/nilm_open_assets.c","$src/nilm_learn.c","$src/nilm_legacy.c")
& $gcc -O2 -D__USE_MINGW_ANSI_STDIO=1 -ffp-contract=off -I $src -I "$root/sim/stubs" "$root/sim/tests/test_store.c" "$src/nilm_store.c" @model -lm -o "$out/test_store.exe"
if($LASTEXITCODE){throw 'New SD store compile failed'}
& "$out/test_store.exe" | Tee-Object "$out/store_test.log"
if($LASTEXITCODE){throw 'New SD store failed'}
$names=@('nilm_dynamic','nilm_dorm_assets','nilm_dorm','nilm_board','nilm_event','nilm_phase','nilm_lag','nilm_lag_assets','nilm_infer')
$core=$names|ForEach-Object {"$src/$_.c"}
foreach($case in @('accounting','event_pipeline','diagnostics','registration')){
 $test="$root/sim/tests/test_$case.c"
 & $gcc -O2 -D__USE_MINGW_ANSI_STDIO=1 -ffp-contract=off -I $src $test "$root/sim/tests/host_store.c" @model @core -lm -o "$out/test_$case.exe"
 if($LASTEXITCODE){throw "$case compile failed"}
 & "$out/test_$case.exe" | Tee-Object "$out/${case}_test.log"
 if($LASTEXITCODE){throw "$case regression failed"}
}
& $gcc -O2 -D__USE_MINGW_ANSI_STDIO=1 -ffp-contract=off -I $src -I "$root/sim/stubs" "$root/sim/tests/test_status.c" "$src/nilm_model.c" @model @core -lm -o "$out/test_status.exe"
if($LASTEXITCODE){throw 'Status compile failed'}
& "$out/test_status.exe" | Set-Content "$out/status_test.log" -Encoding utf8
if($LASTEXITCODE){throw 'Status test failed'}
$rows=@(Get-Content "$out/status_test.log" | Where-Object {$_ -like '{*'} | ForEach-Object {$_|ConvertFrom-Json})
if($rows[0].firmware_revision -ne 'NLIM_set_final' -or $rows[0].inference.unknown_method -ne 'EMBEDDING64_AUX_FOREST_V3'){throw 'New model status mismatch'}
'PASS new model status, registration, maximum status size' | Set-Content "$out/status_summary.log"
& $gcc -O2 -ffp-contract=off -shared -I $src "$root/sim/tests/integration_wrapper.c" "$root/sim/tests/host_store.c" "$src/nilm_infer.c" "$src/nilm_dynamic.c" @model -lm -o "$out/integrated.dll"
if($LASTEXITCODE){throw 'Integrated numeric build failed'}
$env:PYTHONDONTWRITEBYTECODE='1'
& $Python -B "$root/sim/tests/test_integrated_numeric.py"
if($LASTEXITCODE){throw 'Integrated numeric test failed'}

