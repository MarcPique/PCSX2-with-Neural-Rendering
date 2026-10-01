#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$DependenciesDirectory,
    [Parameter(Mandatory)][string]$GuardBuildDirectory,
    [Parameter(Mandatory)][string]$NoticesDirectory,
    [Parameter(Mandatory)][string]$CRTRedistDirectory,
    [Parameter(Mandatory)][string]$DumpbinPath,
    [Parameter(Mandatory)][string]$PackageDirectory,
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$package = [IO.Path]::GetFullPath($PackageDirectory)
if (Test-Path -LiteralPath $package) { throw 'Use a new package directory, without user data.' }
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$deps = (Resolve-Path -LiteralPath $DependenciesDirectory).Path
$guard = (Resolve-Path -LiteralPath $GuardBuildDirectory).Path
New-Item -ItemType Directory -Path $package -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $build 'pcsx2-qt/pcsx2-qt.exe') -Destination $package
foreach ($folder in @('resources','translations')) {
    Copy-Item -LiteralPath (Join-Path $build "pcsx2-qt/$folder") -Destination $package -Recurse
}
# Base Qt translations are generated beside the executable by upstream CMake.
Get-ChildItem -LiteralPath (Join-Path $build 'pcsx2-qt') -Filter 'qt_*.qm' | Copy-Item -Destination (Join-Path $package 'translations')
& (Join-Path $deps 'bin/windeployqt.exe') --release --no-compiler-runtime --no-system-d3d-compiler --no-opengl-sw --dir $package (Join-Path $package 'pcsx2-qt.exe')
if ($LASTEXITCODE) { throw 'Qt deployment failed.' }
# These libraries are opened dynamically and cannot be discovered from PE imports.
foreach ($name in @('avcodec-63.dll','avformat-63.dll','avutil-61.dll','swresample-7.dll','swscale-10.dll','shaderc_shared.dll','dxcompiler.dll')) {
    Copy-Item -LiteralPath (Join-Path $deps "bin/$name") -Destination $package
}
Copy-Item -LiteralPath (Join-Path $deps 'bin/D3D12') -Destination $package -Recurse
Get-ChildItem -LiteralPath $CRTRedistDirectory -Filter '*.dll' | Copy-Item -Destination $package
Copy-Item -LiteralPath (Join-Path $guard 'Release/pcsx2-settings-only.addon64') -Destination $package
# Resolve native imports recursively from the exact dependency build; never PATH.
$checked = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
do {
    $pending = @(Get-ChildItem -LiteralPath $package -File -Recurse | Where-Object {
        $_.Extension -in @('.dll','.exe','.addon64') -and -not $checked.Contains($_.FullName)
    })
    foreach ($binary in $pending) {
        $imports = & $DumpbinPath /nologo /dependents $binary.FullName
        if ($LASTEXITCODE) { throw "Cannot inspect $($binary.Name)" }
        foreach ($line in $imports) {
            $name = $line.Trim()
            if ($name -notmatch '^[A-Za-z0-9_.-]+\.dll$' -or $name -match '^(api-ms-|ext-ms-)') { continue }
            if (Test-Path -LiteralPath (Join-Path $binary.DirectoryName $name)) { continue }
            if (Test-Path -LiteralPath (Join-Path $package $name)) { continue }
            $source = Join-Path $deps "bin/$name"
            if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $package }
            elseif (-not (Test-Path -LiteralPath (Join-Path "$env:SystemRoot/System32" $name))) { throw "Missing DLL: $name" }
        }
        [void]$checked.Add($binary.FullName)
    }
} while ($pending.Count)
Copy-Item -LiteralPath $NoticesDirectory -Destination (Join-Path $package 'THIRD-PARTY-NOTICES') -Recurse
Copy-Item -LiteralPath (Join-Path $guard 'ReShade-SDK-LICENSE.md') -Destination (Join-Path $package 'THIRD-PARTY-NOTICES')
foreach ($name in @('Setup-Neural.ps1','fetch-neural-runtime.ps1')) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $package }
$readme = [IO.File]::ReadAllText((Join-Path $repo 'NEURAL_RENDERING.md')).Replace('(docs/neural-rendering/VALIDACION.md)', '(VALIDACION.md)')
[IO.File]::WriteAllText((Join-Path $package 'LEEME.md'), $readme, [Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $repo 'docs/neural-rendering/VALIDACION.md') -Destination $package
Copy-Item -LiteralPath (Join-Path $repo 'COPYING.GPLv3') -Destination $package
[IO.File]::WriteAllText((Join-Path $package 'portable.ini'), '')
[IO.File]::WriteAllText((Join-Path $package 'neural-rendering.json'), '{"schema":1,"enabled":false}')
$commit = (& git -C $repo rev-parse HEAD).Trim()
[ordered]@{
    release = 'v0.1.1-neural'
    source = "https://github.com/MarcPique/PCSX2-with-Neural-Rendering/tree/$commit"
    sourceCommit = $commit
    upstreamBase = '94d86c891b1621c0b252e4fc2e155bf90274dcc0'
    build = 'Release x64; MSVC 14.51; Qt 6.11.2; upstream v2.9.93'
    executableSHA256 = (Get-FileHash -LiteralPath (Join-Path $package 'pcsx2-qt.exe')).Hash.ToLowerInvariant()
    dependenciesSHA256 = '18842cc10521a1be4227d703e6ce4c1ce7bd364544bba479239e584cb2a1cdee'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $package 'BUILD-INFO.json') -Encoding utf8
if (-not $OutputDirectory) { return }
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null
$sevenZip = 'C:/Program Files/7-Zip'
Copy-Item -LiteralPath (Join-Path $sevenZip 'License.txt') -Destination (Join-Path $package 'THIRD-PARTY-NOTICES/7-Zip-LICENSE.txt')
'Unmodified 7-Zip 26.02 self-extractor. Source: https://github.com/ip7z/7zip/tree/26.02' | Set-Content -LiteralPath (Join-Path $package 'THIRD-PARTY-NOTICES/7-Zip-SOURCE.txt')
$zip = Join-Path $output 'PCSX2-Neural-0.1.1-win64.zip'
$sfx = Join-Path $output 'Extraer-PCSX2-Neural-0.1.1-win64.exe'
if ((Test-Path -LiteralPath $zip) -or (Test-Path -LiteralPath $sfx)) { throw 'Release files already exist.' }
[IO.Compression.ZipFile]::CreateFromDirectory($package, $zip, [IO.Compression.CompressionLevel]::Optimal, $true)
Push-Location (Split-Path $package -Parent)
try {
    & (Join-Path $sevenZip '7z.exe') a -t7z '-mx=5' ('-sfx' + (Join-Path $sevenZip '7z.sfx')) $sfx (Split-Path $package -Leaf)
    if ($LASTEXITCODE) { throw 'Self-extractor creation failed.' }
} finally { Pop-Location }
@($zip,$sfx) | ForEach-Object { (Get-FileHash -LiteralPath $_).Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($_) } |
    Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding ascii
