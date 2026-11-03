param([string]$Python='python')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
& $Python -m PyInstaller --noconfirm --onefile --name NLIM_set_final --add-data "$root/src/web/dorm_demo.html;." --distpath "$root/work/receiver" --workpath "$root/work/receiver_build" --specpath "$root/work/receiver_build" "$root/src/web/dorm_demo.py"
if($LASTEXITCODE){throw 'Receiver build failed'}
