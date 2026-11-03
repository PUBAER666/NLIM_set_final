param([string]$AmdRoot='C:/AMDDesignTools/2025.2',[string]$Bsp='')
$ErrorActionPreference='Stop'
$root=(Split-Path $PSScriptRoot -Parent).Replace('\','/')
if(!$Bsp){$Bsp="$root/work/vitis_short/p/export/p/sw/standalone"}
$bsp=$Bsp.Replace('\','/')
if(!(Test-Path "$bsp/include/ff.h")){throw 'Run create_platform.py first'}
$p=Get-Content "$bsp/include/xparameters.h" -Raw
if($p -notmatch '(?i)NILM_CNN.*BASEADDR\s+0x43c10000'){throw 'CNN address mismatch'}
$env:PATH="$AmdRoot/Vitis/gnu/aarch32/nt/gcc-arm-none-eabi/bin;$AmdRoot/Vitis/bin;"+$env:PATH
$cmake="$AmdRoot/tps/win64/cmake-3.24.2/bin/cmake.exe"
$llvm="$AmdRoot/tps/llvm/7.0/win64/bin"
& $cmake -S "$root/src/ps" -B "$root/work/ps" -G Ninja -DCMAKE_NINJA_FORCE_RESPONSE_FILE=ON "-DCMAKE_TOOLCHAIN_FILE=$bsp/cortexa9_toolchain.cmake" "-DCMAKE_SPECS_FILE=$bsp/Xilinx.spec" "-DCMAKE_MODULE_PATH=$bsp" "-DCMAKE_INCLUDE_PATH=$bsp/include" "-DCMAKE_LIBRARY_PATH=$bsp/lib" -U CMAKE_C_FLAGS -U CMAKE_CXX_FLAGS -U CMAKE_ASM_FLAGS
if($LASTEXITCODE){throw 'CMake configuration failed'}
if(Test-Path "$llvm/llvm-size.exe"){
 & $cmake -S "$root/src/ps" -B "$root/work/ps" "-DNILM_SIZE_TOOL=$llvm/llvm-size.exe"
 if($LASTEXITCODE){throw 'Size tool configuration failed'}
}
& $cmake --build "$root/work/ps" -j 4
if($LASTEXITCODE){throw 'PS build failed'}
if(Test-Path "$llvm/llvm-objdump.exe"){
 $all=& "$llvm/llvm-objdump.exe" -d "$root/work/ps/NLIM_set_final.elf"
 if($LASTEXITCODE){throw 'ELF disassembly failed'}
 $irq=([regex]::Match(($all -join "`n"),'(?ms)^IRQHandler:.*?(?=^\S[^\n]*:\s*$)')).Value
}else{
 $irq=& arm-none-eabi-objdump.exe -d --disassemble=IRQHandler "$root/work/ps/NLIM_set_final.elf"
 if($LASTEXITCODE){throw 'ELF disassembly failed'}
}
foreach($pat in @('vpush.*d0(?:-d7|, d1, d2, d3, d4, d5, d6, d7)','vpush.*d16(?:-d31|, d17.*d31)','vpop.*d16(?:-d31|, d17.*d31)','vpop.*d0(?:-d7|, d1, d2, d3, d4, d5, d6, d7)')){
 if(($irq -join "`n") -notmatch $pat){throw "Missing interrupt FPU preservation: $pat"}
}
$irq | Set-Content "$root/work/ps/irq_disassembly.txt" -Encoding utf8
Write-Output 'PASS_PS_BUILD: NLIM_set_final firmware; NLIM_set_final final design package'

