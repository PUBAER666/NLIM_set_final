param([string]$AmdRoot='C:/AMDDesignTools/2025.2')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root work/boot
New-Item -ItemType Directory -Force $out | Out-Null
$hw=Join-Path $root board/hardware
if(Test-Path "$root/work/hardware/nilm_board.xsa"){$hw="$root/work/hardware"}
Copy-Item "$hw/system_wrapper.bit" "$out/system_wrapper.bit" -Force
Copy-Item "$root/work/ps/NLIM_set_fianl.elf" "$out/NLIM_set_fianl.elf" -Force
Copy-Item "$root/work/vitis_short/p/export/p/sw/boot/fsbl.elf" "$out/fsbl.elf" -Force
@'
the_ROM_image:
{
 [bootloader] fsbl.elf
 system_wrapper.bit
 NLIM_set_fianl.elf
}
'@ | Set-Content "$out/boot.bif" -Encoding ascii
Push-Location $out
try{& "$AmdRoot/Vitis/bin/bootgen.bat" -arch zynq -image boot.bif -o BOOT.BIN -w on;if($LASTEXITCODE){throw 'Bootgen failed'}}finally{Pop-Location}
Write-Output 'BOOT generated: NLIM_set_fianl'
