$ErrorActionPreference='Stop'
try {
 $base=[IO.Path]::GetFullPath($PSScriptRoot)+[IO.Path]::DirectorySeparatorChar
 $rows=Get-Content (Join-Path $PSScriptRoot 'checksums.json') -Raw -Encoding UTF8 | ConvertFrom-Json
 foreach($row in $rows){
  $target=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot $row.path))
  if(!$target.StartsWith($base,[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid package path'}
  if(!(Test-Path -LiteralPath $target -PathType Leaf)){throw "Missing: $($row.path)"}
  if((Get-FileHash -LiteralPath $target).Hash -ne $row.sha256){throw "Changed: $($row.path)"}
 }
 Write-Host 'PASS: NLIM_set_final package matches SHA256 checksums; firmware NLIM_set_final.' -ForegroundColor Green
 Write-Host 'Use included sd_card/BOOT.BIN. Final design package: NLIM_set_final.'
 exit 0
}catch{Write-Host ("FAILED: "+$_.Exception.Message) -ForegroundColor Red;exit 1}
