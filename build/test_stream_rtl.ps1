param([string]$AmdRoot='C:/AMDDesignTools/2025.2')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root 'work/stream_rtl';$bin="$AmdRoot/Vivado/bin"
New-Item -ItemType Directory -Force $out | Out-Null
Push-Location $out
try{
 $files=@(Get-ChildItem "$root/src/rtl" -Filter '*.v' | ForEach-Object FullName)
 $files+=@(Get-ChildItem "$root/sim/tests" -Filter 'tb_nilm_stream*.sv' | ForEach-Object FullName)
 $files+="$root/sim/tests/tb_nilm_sample_mailbox.sv"
 & "$bin/xvlog.bat" --sv @files
 if($LASTEXITCODE){throw 'RTL compile failed'}
 foreach($top in @('tb_nilm_stream_engine','tb_nilm_sample_mailbox','tb_nilm_stream_axi')){
  & "$bin/xelab.bat" $top -s $top
  if($LASTEXITCODE){throw "Elaboration failed: $top"}
  & "$bin/xsim.bat" $top -runall -log "$top.log"
  $exit=$LASTEXITCODE;$log=Get-Content "$top.log" -Raw
  Copy-Item "$top.log" "$root/sim/results/$top.log" -Force
  if($exit -or $log -notmatch 'PASS:' -or $log -match 'Fatal:|Error:'){throw "RTL test failed: $top"}
 }
}finally{Pop-Location}
Write-Output 'PASS_STREAM_RTL: engine, mailbox, AXI'
