#requires -Version 7.0
param([Parameter(Mandatory)][string]$PackageDirectory, [Parameter(Mandatory)][string]$DumpbinPath)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $PackageDirectory).Path
foreach ($name in @('pcsx2-qt.exe','pcsx2-settings-only.addon64','Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll',
    'msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll','platforms/qwindows.dll','portable.ini',
    'Setup-Neural.ps1','fetch-neural-runtime.ps1','resources/GameIndex.yaml','shaderc_shared.dll',
    'dxcompiler.dll','D3D12/D3D12Core.dll','avcodec-63.dll','avformat-63.dll','avutil-61.dll','swresample-7.dll','swscale-10.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $name) -PathType Leaf)) { throw "Missing required file: $name" }
}
$count = 0
foreach ($binary in Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object { $_.Extension -in @('.dll','.exe','.addon64') }) {
    $imports = & $DumpbinPath /nologo /dependents $binary.FullName
    if ($LASTEXITCODE) { throw "Cannot inspect $($binary.Name)" }
    foreach ($line in $imports) {
        $name = $line.Trim()
        if ($name -notmatch '^[A-Za-z0-9_.-]+\.dll$' -or $name -match '^(api-ms-|ext-ms-)') { continue }
        $paths = @((Join-Path $binary.DirectoryName $name),(Join-Path $root $name),(Join-Path "$env:SystemRoot/System32" $name))
        if (-not ($paths | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })) { throw "$($binary.Name) is missing $name" }
    }
    $count++
}
$forbidden = Get-ChildItem -LiteralPath $root -File -Recurse | Where-Object {
    $_.Name -match '^(ReShade64|nvngx.*|VkLayer_feed_vk)\.dll$|^(dlss5-feed|renodx-dlss5)\.addon64$|\.(log|pdb|iso|chd)$' -or
    $_.Name -in @('PCSX2.ini','ReShade.ini','ReShadePreset.ini','dlss5-feed.cfg')
}
if ($forbidden) { throw ('Public package contains runtime/user/debug files: ' + ($forbidden.Name -join ', ')) }
Write-Host "PASS: $count PE binaries resolve within the package or Windows/System32; app-local CRT present; public package clean."
