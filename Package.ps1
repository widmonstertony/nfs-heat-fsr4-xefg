#Requires -Version 5.1
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$payload=@('payload\native-probe\HeatTemporalBridge.asi','payload\loader\d3d11.dll')|ForEach-Object {
 @{path=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $root $_) -Algorithm SHA256).Hash}
}
if($payload[1].sha256 -ne '57EC56390C92A18B82F58855E63C93985E254D1E29D5F75CBC28DC08F96C8A12'){throw 'Unexpected ASI loader'}
$runtime=@(
 @{name='dxgi.dll';sha256='691CAC1C94F41DF97C1559A7DE122E36538BD9E7E71F517E98B1702B02DA073C';fsr4Only=$false},
 @{name='libxess_fg.dll';sha256='EC5E0C65E075570C6EDE72618BB666D0BE0C2E10B2EA9762C0FE8CB8E375AB27';fsr4Only=$false},
 @{name='libxell.dll';sha256='D2030DCD694FDA8F2EC7E044B13E6DB8F0B56D4BA9113A5EFAD334E3F3DED8C7';fsr4Only=$false},
 @{name='amd_fidelityfx_loader_dx12.dll';sha256='E2D85AA05A9BD9ED8B38935FDF5199372CCA6F74C12015143BB6F945EE1608AA';fsr4Only=$true},
 @{name='amd_fidelityfx_upscaler_dx12.dll';sha256='480FAA4B68647A9432575B9E409F5CCE1C33365E7BABCD96613C235B987786F0';fsr4Only=$true}
)
$manifest=[ordered]@{version='0.1.0-experimental';payload=@($payload);runtime=$runtime}
$manifest|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $root 'manifest.json') -Encoding UTF8
$dist=Join-Path $root 'dist';New-Item -ItemType Directory -Path $dist -Force|Out-Null
$zip=Join-Path $dist 'Heat-FSR4-XeFG-v0.1.0-experimental-bridge-kit.zip'
if(Test-Path -LiteralPath $zip){throw 'Release archive exists; move it aside before repackaging.'}
$stage=Join-Path $root ('build\release-stage-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage -Force|Out-Null
foreach($name in @('Install.ps1','Restore.ps1','INSTALL.cmd','RESTORE.cmd','README.md','README.zh-CN.md','RELEASE-NOTES.md','LICENSE','manifest.json','OptiScaler.template.ini','licenses')){
 Copy-Item -LiteralPath (Join-Path $root $name) -Destination (Join-Path $stage $name) -Recurse
}
# Explicit allowlist: never ship linker outputs, logs, private fixtures or SDKs.
foreach($item in $payload){
 $dest=Join-Path $stage $item.path
 New-Item -ItemType Directory -Path (Split-Path $dest) -Force|Out-Null
 Copy-Item -LiteralPath (Join-Path $root $item.path) -Destination $dest
}
$files=@(Get-ChildItem -LiteralPath $stage | ForEach-Object FullName)
Compress-Archive -LiteralPath $files -DestinationPath $zip -CompressionLevel Optimal
$hash=Get-FileHash -LiteralPath $zip -Algorithm SHA256
($hash.Hash+'  '+[IO.Path]::GetFileName($zip))|Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -Encoding ASCII
Get-Item -LiteralPath $zip|Select-Object Name,Length
