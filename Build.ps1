#Requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$NgxInclude,[Parameter(Mandatory=$true)][string]$XeFgInclude)
$ErrorActionPreference='Stop'
if(-not (Get-Command cl.exe -ErrorAction SilentlyContinue)){throw 'Run from an x64 Visual Studio Developer PowerShell.'}
foreach($f in @((Join-Path $NgxInclude 'nvsdk_ngx.h'),(Join-Path $XeFgInclude 'xefg_swapchain_d3d12.h'))){if(-not (Test-Path -LiteralPath $f)){throw "Missing SDK header: $f"}}
$out=Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $out -Force | Out-Null
Push-Location $out
try{
 $src=Join-Path $PSScriptRoot 'src\native-probe'
 $c=@('buffer','hook','trampoline')|ForEach-Object {Join-Path $src ('minhook\src\'+$_+'.c')}
 $c+=Join-Path $src 'minhook\src\hde\hde64.c'
 & cl.exe /nologo /O2 /MT /c $c
 if($LASTEXITCODE -ne 0){throw 'MinHook build failed'}
 $target=Join-Path $PSScriptRoot 'payload\native-probe\HeatTemporalBridge.asi'
 & cl.exe /nologo /std:c++17 /EHsc /O2 /MT /W3 /LD "/I$NgxInclude" "/I$XeFgInclude" (Join-Path $src 'bridge.cpp') buffer.obj hook.obj trampoline.obj hde64.obj "/Fe:$target" /link d3d11.lib dxgi.lib dxguid.lib user32.lib bcrypt.lib d3dcompiler.lib
 if($LASTEXITCODE -ne 0){throw 'Bridge build failed'}
}finally{Pop-Location}
