@echo off
chcp 65001 >nul
pushd "%~dp0"
NLIM_set_fianl.exe --check %*
pause
popd
