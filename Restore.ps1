#Requires -Version 5.1
[CmdletBinding()]
param([string]$GamePath,[string]$ProfilePath)
Set-StrictMode -Version 2
$ErrorActionPreference='Stop'
if(-not $GamePath){$GamePath=(Read-Host 'Heat game folder / Heat 游戏目录').Trim('"')}
$GamePath=(Resolve-Path -LiteralPath $GamePath).Path.TrimEnd('\')
if(Get-Process NeedForSpeedHeat -ErrorAction SilentlyContinue | Where-Object Path -EQ (Join-Path $GamePath 'NeedForSpeedHeat.exe')){throw 'Exit Heat first / 请先退出游戏。'}
$plugin=Join-Path $GamePath 'HeatTemporalBridge';$statePath=Join-Path $plugin 'install-state.json'
$state=Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
if($state.owner -ne 'nfs-heat-fsr4-xefg' -or $state.game -ne $GamePath){throw 'Installation ownership mismatch.'}
$allowedRoot=@((Join-Path $GamePath 'd3d11.dll'),(Join-Path $GamePath 'dxgi.dll'))
foreach($file in @($state.files)){
 $full=[IO.Path]::GetFullPath($file.path)
 if($full -notin $allowedRoot -and -not $full.StartsWith($plugin+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe recorded path.'}
 if((Test-Path -LiteralPath $full) -and (Get-FileHash -LiteralPath $full).Hash -ne $file.sha256){throw ('Modified file; not removed: '+$full)}
}
foreach($name in 'd3d11.ini','OptiScaler.ini'){
 $path=Join-Path $GamePath $name
 if((Test-Path -LiteralPath $path) -and [IO.File]::ReadAllText($path).IndexOf($plugin,[StringComparison]::OrdinalIgnoreCase) -lt 0){throw ('Configuration ownership mismatch: '+$path)}
}
$expectedProfile=if($ProfilePath){[IO.Path]::GetFullPath($ProfilePath)}else{Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Need for Speed Heat\settings\PROFILEOPTIONS_profile'}
if($state.profile -ne $expectedProfile){throw 'Profile path mismatch.'}
$archive=Join-Path $GamePath ('HeatTemporalBridge-backups\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $archive -Force | Out-Null
foreach($file in @($state.files)){
 if(Test-Path -LiteralPath $file.path){
  $relative=$file.path.Substring($GamePath.Length+1);$dest=Join-Path $archive $relative
  New-Item -ItemType Directory -Path (Split-Path $dest) -Force | Out-Null
  Move-Item -LiteralPath $file.path -Destination $dest
 }
}
foreach($name in 'd3d11.ini','OptiScaler.ini'){$path=Join-Path $GamePath $name;if(Test-Path -LiteralPath $path){Move-Item -LiteralPath $path -Destination (Join-Path $archive $name)}}
$text=[IO.File]::ReadAllText($expectedProfile)
Copy-Item -LiteralPath $expectedProfile -Destination (Join-Path $archive 'profile-before-restore.backup')
foreach($key in $state.original.PSObject.Properties.Name){
 $pattern='(?m)^GstRender\.'+[regex]::Escape($key)+' ([^\r\n]+)';$m=[regex]::Match($text,$pattern)
 if($m.Success -and $m.Groups[1].Value -eq $state.applied.$key){$text=[regex]::Replace($text,$pattern,('GstRender.'+$key+' '+$state.original.$key))}
 else{Write-Warning ('User-changed setting preserved: '+$key)}
}
[IO.File]::WriteAllText($expectedProfile,$text,[Text.UTF8Encoding]::new($false))
Move-Item -LiteralPath $statePath -Destination (Join-Path $archive 'install-state.json')
if((Resolve-Path -LiteralPath $plugin).Path -ne (Join-Path $GamePath 'HeatTemporalBridge')){throw 'Unexpected residual folder path.'}
Move-Item -LiteralPath $plugin -Destination (Join-Path $archive 'residual-plugin')
Write-Host ('Restored. Files retained in / 已恢复，文件保留在: '+$archive)
