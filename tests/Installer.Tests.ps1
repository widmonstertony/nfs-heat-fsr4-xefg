[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$RuntimeSource)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$testRoot=Join-Path ([IO.Path]::GetTempPath()) ('heat-bridge-tests-'+[Guid]::NewGuid().ToString('N'))
$game=Join-Path $testRoot 'Game';New-Item -ItemType Directory -Path $game -Force|Out-Null
$profile=Join-Path $testRoot 'profile.txt'
$original="GstRender.FullscreenMode 0`r`nGstRender.OverallGraphicsQuality 0`r`nGstRender.AntiAliasingPost 2`r`nGstRender.ResolutionScale 1.000000`r`nGstAudio.MasterVolume 0.5`r`n"
[IO.File]::WriteAllText($profile,$original)
[IO.File]::WriteAllText((Join-Path $game 'NeedForSpeedHeat.exe'),'test fixture, not a game binary')
& (Join-Path $repo 'Install.ps1') -GamePath $game -RuntimeSource $RuntimeSource -ProfilePath $profile -AllowUntestedHardware -ValidateOnly
if(Test-Path -LiteralPath (Join-Path $game 'HeatTemporalBridge')){throw 'ValidateOnly wrote files'}
& (Join-Path $repo 'Install.ps1') -GamePath $game -RuntimeSource $RuntimeSource -ProfilePath $profile -AllowUntestedHardware
if(-not (Test-Path -LiteralPath (Join-Path $game 'dxgi.dll'))){throw 'Installation missing dxgi'}
$ini=[IO.File]::ReadAllText((Join-Path $game 'OptiScaler.ini'))
if($ini -notmatch 'Dx11Upscaler=ffx_12' -or $ini -match '@RUNTIME@'){throw 'Template substitution failed'}
$stopped=$false
try{& (Join-Path $repo 'Install.ps1') -GamePath $game -RuntimeSource $RuntimeSource -ProfilePath $profile -AllowUntestedHardware}catch{$stopped=$true}
if(-not $stopped){throw 'Duplicate install was not blocked'}
& (Join-Path $repo 'Restore.ps1') -GamePath $game -ProfilePath $profile
if(Test-Path -LiteralPath (Join-Path $game 'dxgi.dll')){throw 'Restore left injector active'}
if([IO.File]::ReadAllText($profile) -ne $original){throw 'Profile roundtrip failed'}
& (Join-Path $repo 'Install.ps1') -GamePath $game -RuntimeSource $RuntimeSource -ProfilePath $profile -AllowUntestedHardware -Upscaler FSR3
if([IO.File]::ReadAllText((Join-Path $game 'OptiScaler.ini')) -notmatch 'Dx11Upscaler=fsr31'){throw 'FSR3 selection failed'}
$owned=Join-Path $game 'dxgi.dll';$golden=Join-Path $testRoot 'owned-dxgi.backup';Copy-Item -LiteralPath $owned -Destination $golden
[IO.File]::WriteAllText($owned,'modified after installation')
$stopped=$false
try{& (Join-Path $repo 'Restore.ps1') -GamePath $game -ProfilePath $profile}catch{$stopped=$true}
if(-not $stopped -or -not (Test-Path -LiteralPath $owned)){throw 'Modified owned-file guard failed'}
Copy-Item -LiteralPath $golden -Destination $owned -Force
$userText=[IO.File]::ReadAllText($profile).Replace('GstAudio.MasterVolume 0.5','GstAudio.MasterVolume 0.9').Replace('GstRender.FullscreenMode 1','GstRender.FullscreenMode 2')
[IO.File]::WriteAllText($profile,$userText)
& (Join-Path $repo 'Restore.ps1') -GamePath $game -ProfilePath $profile
if([IO.File]::ReadAllText($profile) -notmatch 'GstAudio.MasterVolume 0.9' -or [IO.File]::ReadAllText($profile) -notmatch 'GstRender.FullscreenMode 2'){throw 'User changes were not preserved'}
# Unknown-file guard, no overwriting.
[IO.File]::WriteAllText((Join-Path $game 'dxgi.dll'),'unrelated mod')
$stopped=$false
try{& (Join-Path $repo 'Install.ps1') -GamePath $game -RuntimeSource $RuntimeSource -ProfilePath $profile -AllowUntestedHardware}catch{$stopped=$true}
if(-not $stopped -or [IO.File]::ReadAllText((Join-Path $game 'dxgi.dll')) -ne 'unrelated mod'){throw 'Unknown-file guard failed'}
Write-Host ('PASS: preflight, FSR3/4 install, duplicate/unknown/modified-file guards, profile roundtrip and user-change preservation. Fixture retained: '+$testRoot)
