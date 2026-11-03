@echo off
chcp 65001 >nul
pushd "%~dp0"
NLIM_set_final.exe --check %*
pause
popd
