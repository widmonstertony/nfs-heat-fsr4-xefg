#Requires -Version 5.1
[CmdletBinding()]
param([string]$GamePath,[string]$RuntimeSource,[ValidateSet('FSR3','FSR4')][string]$Upscaler='FSR4',[switch]$AllowUntestedHardware,[switch]$ValidateOnly,[string]$ProfilePath)
Set-StrictMode -Version 2
$ErrorActionPreference='Stop'
if(-not $GamePath){$GamePath=(Read-Host 'Heat game folder / Heat 游戏目录').Trim('"')}
if(-not $RuntimeSource){$RuntimeSource=(Read-Host 'Existing working runtime folder / 现有可用运行库目录').Trim('"')}
$GamePath=(Resolve-Path -LiteralPath $GamePath).Path.TrimEnd('\')
$RuntimeSource=(Resolve-Path -LiteralPath $RuntimeSource).Path
if(-not (Test-Path -LiteralPath (Join-Path $GamePath 'NeedForSpeedHeat.exe'))){throw 'NeedForSpeedHeat.exe not found.'}
if(Get-Process NeedForSpeedHeat -ErrorAction SilentlyContinue | Where-Object Path -EQ (Join-Path $GamePath 'NeedForSpeedHeat.exe')){throw 'Exit Heat before installation / 请先退出游戏。'}
if(-not $AllowUntestedHardware){
 $gpu=@(Get-CimInstance Win32_VideoController | Where-Object PNPDeviceID -Match 'VEN_1002&DEV_7360&SUBSYS_021E106B')
 if(-not $gpu.Count){throw 'Only the Radeon Pro5600M DEV7360/SUBSYS021E106B configuration was tested. See README.'}
}
$manifest=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') -Raw | ConvertFrom-Json
$selected=@{}
foreach($item in @($manifest.runtime)){
 if($item.fsr4Only -and $Upscaler -ne 'FSR4'){continue}
 $found=@(Get-ChildItem -LiteralPath $RuntimeSource -Filter $item.name -File -Recurse | Where-Object {(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash -eq $item.sha256})
 if(-not $found.Count){throw ('Missing matching runtime / 缺少匹配运行库: '+$item.name+'; SHA256='+$item.sha256)}
 $selected[$item.name]=$found[0].FullName
}
foreach($item in @($manifest.payload)){
 $path=Join-Path $PSScriptRoot $item.path
 if(-not (Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path).Hash -ne $item.sha256){throw ('Release integrity check failed: '+$item.path)}
}
$plugin=Join-Path $GamePath 'HeatTemporalBridge'
if(Test-Path -LiteralPath $plugin){throw 'HeatTemporalBridge already exists. Restore the previous installation first.'}
foreach($name in 'd3d11.dll','d3d11.ini','dxgi.dll','OptiScaler.ini'){
 if(Test-Path -LiteralPath (Join-Path $GamePath $name)){throw ('Existing mod/configuration; not overwritten: '+$name+'. Restore/remove it first.')}
}
$profile=if($ProfilePath){[IO.Path]::GetFullPath($ProfilePath)}else{Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Need for Speed Heat\settings\PROFILEOPTIONS_profile'}
if(-not (Test-Path -LiteralPath $profile)){throw 'Start the unmodified game once to create its settings, then exit.'}
$before=[IO.File]::ReadAllText($profile)
$original=@{};$applied=@{FullscreenMode='1';OverallGraphicsQuality='5';AntiAliasingPost='2';ResolutionScale='0.666667'}
$after=$before
foreach($key in $applied.Keys){
 $pattern='(?m)^GstRender\.'+[regex]::Escape($key)+' ([^\r\n]+)'
 $m=[regex]::Match($before,$pattern)
 if(-not $m.Success){throw ('Missing profile setting: '+$key)}
 $original[$key]=$m.Groups[1].Value
 $after=[regex]::Replace($after,$pattern,('GstRender.'+$key+' '+$applied[$key]))
}
Write-Host ('Validated / 验证通过: '+$Upscaler+' + XeFG, '+$GamePath)
if($ValidateOnly){return}
New-Item -ItemType Directory -Path $plugin | Out-Null
$native=Join-Path $plugin 'native-probe';$runtime=Join-Path $plugin 'backend-probe\fg-runtime'
New-Item -ItemType Directory -Path $native,$runtime | Out-Null
$state=[ordered]@{format=1;owner='nfs-heat-fsr4-xefg';game=$GamePath;profile=$profile;original=$original;applied=$applied;upscaler=$Upscaler;files=@();complete=$false}
$statePath=Join-Path $plugin 'install-state.json'
function Save-State {$state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $statePath -Encoding UTF8}
function Install-OwnedFile([string]$source,[string]$destination){
 Copy-Item -LiteralPath $source -Destination $destination
 $state.files+=@{path=$destination;sha256=(Get-FileHash -LiteralPath $destination).Hash}
 Save-State
}
Save-State
Copy-Item -LiteralPath $profile -Destination (Join-Path $plugin 'profile-before-install.backup')
Install-OwnedFile (Join-Path $PSScriptRoot 'payload\native-probe\HeatTemporalBridge.asi') (Join-Path $native 'HeatTemporalBridge.asi')
Install-OwnedFile (Join-Path $PSScriptRoot 'payload\loader\d3d11.dll') (Join-Path $GamePath 'd3d11.dll')
Install-OwnedFile $selected['dxgi.dll'] (Join-Path $GamePath 'dxgi.dll')
foreach($name in $selected.Keys){if($name -ne 'dxgi.dll'){Install-OwnedFile $selected[$name] (Join-Path $runtime $name)}}
$loader=@"
[GlobalSets]
DontLoadFromDllMain=1
LoadPlugins=1
LoadFromScriptsOnly=1
LoadRecursively=0
LoadExtraPlugins=$(Join-Path $native 'HeatTemporalBridge.asi')
DebugLog=0
DisableCrashDumps=1
[FileLoader]
OverloadFromFolder=
"@
[IO.File]::WriteAllText((Join-Path $GamePath 'd3d11.ini'),$loader,[Text.UTF8Encoding]::new($false))
$template=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'OptiScaler.template.ini') -Raw
$template=$template.Replace('@RUNTIME@',$runtime).Replace('@UPSCALER@',$(if($Upscaler -eq 'FSR4'){'ffx_12'}else{'fsr31'}))
[IO.File]::WriteAllText((Join-Path $GamePath 'OptiScaler.ini'),$template,[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText($profile,$after,[Text.UTF8Encoding]::new($false))
$state.complete=$true;Save-State
Write-Host 'Installed. Select SOLO. Automatically enabled after ~10sec on the verified driving TAA pass. F9 disables for this session.'
Write-Host '安装完成。只玩单人模式；匹配驾驶画面后约10秒自动生效。F9关闭本次效果。'
Write-Host 'Experimental: no guaranteed FPS gain. Restore.ps1 removes only recorded, unchanged files.'
