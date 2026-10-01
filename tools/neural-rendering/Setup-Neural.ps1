#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$Pcsx2Directory = $PSScriptRoot,
    [string]$CacheDirectory = '',
    [int]$FromPcsx2Id = 0
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$target = (Resolve-Path -LiteralPath $Pcsx2Directory).Path
$exe = Join-Path $target 'pcsx2-qt.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'Run this script beside pcsx2-qt.exe, or pass -Pcsx2Directory.' }
if ($FromPcsx2Id) {
    $caller = Get-Process -Id $FromPcsx2Id -ErrorAction Stop
    if (-not [string]::Equals($caller.Path, $exe, [StringComparison]::OrdinalIgnoreCase)) { throw 'The caller is not this copy of PCSX2.' }
    $loaded = $caller.Modules | Where-Object { $_.ModuleName -in @('ReShade64.dll','VkLayer_feed_vk.dll','renodx-dlss5.addon64','dlss5-feed.addon64') }
    if ($loaded) { throw 'Disable Neural / ReShade in settings before repairing loaded components. PCSX2 can stay open.' }
}
foreach ($process in (Get-Process pcsx2-qt -ErrorAction SilentlyContinue)) {
    if ($process.Path -and [string]::Equals($process.Path, $exe, [StringComparison]::OrdinalIgnoreCase)) {
        if ($process.Id -ne $FromPcsx2Id) { throw 'Close other instances of this copy of PCSX2 before installing components.' }
    }
}
if (-not $CacheDirectory) { $CacheDirectory = Join-Path $target '.neural-downloads' }
$stage = Join-Path $target ('.neural-setup-' + [Guid]::NewGuid().ToString('N'))
$runtime = Join-Path $stage 'runtime'
$helper = Join-Path $stage 'diagnostic-helper'

# Download and validate first. Never execute downloaded code or alter Vulkan's registry.
& (Join-Path $PSScriptRoot 'fetch-neural-runtime.ps1') -RuntimeDirectory $runtime -TestHelperDirectory $helper -CacheDirectory $CacheDirectory
foreach ($name in @('ReShade64.dll','dlss5-feed.addon64','renodx-dlss5.addon64','nvngx_dlssnr.dll','nvngx_dlss.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $runtime $name) -PathType Leaf)) { throw "Missing downloaded component: $name" }
}

# Managed components are replaceable; existing user settings are retained.
foreach ($name in @('ReShade64.dll','dlss5-feed.addon64','renodx-dlss5.addon64','nvngx_dlssnr.dll','nvngx_dlss.dll','LOCAL-USE-NOTICE.txt','PROVENANCE.json')) {
    Copy-Item -LiteralPath (Join-Path $runtime $name) -Destination (Join-Path $target $name) -Force
}
foreach ($directory in @('neural-rendering','reshade-shaders','licenses')) {
    $sourceDirectory = Join-Path $runtime $directory
    $destination = Join-Path $target $directory
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $sourceDirectory) {
        Copy-Item -LiteralPath $item.FullName -Destination $destination -Recurse -Force
    }
}
if (-not $FromPcsx2Id) {
foreach ($name in @('ReShade.ini','ReShadePreset.ini','dlss5-feed.cfg')) {
    if (-not (Test-Path -LiteralPath (Join-Path $target $name))) {
        Copy-Item -LiteralPath (Join-Path $runtime $name) -Destination (Join-Path $target $name)
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $target 'neural-rendering.json'))) {
    [IO.File]::WriteAllText((Join-Path $target 'neural-rendering.json'), '{"schema":1,"enabled":false}', [Text.UTF8Encoding]::new($false))
}
}
Copy-Item -LiteralPath (Join-Path $runtime 'SHA256SUMS.json') -Destination (Join-Path $target 'NEURAL-DOWNLOAD-SHA256SUMS.json') -Force

# Only this invocation's temporary directory can be removed.
$resolvedStage = (Resolve-Path -LiteralPath $stage).Path
if (-not [string]::Equals($resolvedStage, [IO.Path]::GetFullPath($stage), [StringComparison]::OrdinalIgnoreCase) -or
    -not $resolvedStage.StartsWith($target.TrimEnd('\') + '\.neural-setup-', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Refusing to clean a staging path outside the PCSX2 folder.'
}
Remove-Item -LiteralPath $resolvedStage -Recurse -Force
Write-Host 'Neural components installed. Existing INI/CFG settings were preserved.'
Write-Host 'In PCSX2: Settings > Graphics > Vulkan, then Neural / ReShade > choose a preset > Apply without restarting.'
