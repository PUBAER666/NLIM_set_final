@echo off
chcp 65001 >nul
pushd "%~dp0"
NLIM_set_fianl.exe %*
if errorlevel 1 pause
popd
