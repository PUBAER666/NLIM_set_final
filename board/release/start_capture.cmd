@echo off
chcp 65001 >nul
pushd "%~dp0"
NLIM_set_final.exe %*
if errorlevel 1 pause
popd
