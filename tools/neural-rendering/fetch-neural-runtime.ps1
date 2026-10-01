[CmdletBinding()]
param(
    [string]$RuntimeDirectory = (Join-Path $PSScriptRoot '..\research\runtime'),
    [string]$TestHelperDirectory = (Join-Path $PSScriptRoot '..\research\runtime-test-helper'),
    [string]$CacheDirectory = (Join-Path $PSScriptRoot '..\downloads\neural-runtime'),
    [ValidatePattern('^\d+\.\d+\.\d+$')][string]$ReShadeVersion = '6.8.0',
    [string]$FeederTag = 'v1.16.0-beta.4',
    [ValidateSet('renodx-dlss5-4.55','renodx-dlss5-4.70')][string]$RenoDxTag = 'renodx-dlss5-4.55',
    [string]$NeuralModelTag = 'dlssnr-310.8.SF-v2',
    [string]$DlssTag = 'v310.9.1'
)
# Downloads only from the same upstreams as DLSS5oneclick. Does not execute a
# downloaded binary, installer, shader, or script; does not alter the registry.
# The assembled kit is for local use. Lumenite's AGNYA license forbids rehosting;
# RenoDX NR/model redistribution permissions are not established by this script.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
Add-Type -AssemblyName System.IO.Compression
$runtimeRoot = [IO.Path]::GetFullPath($RuntimeDirectory)
$testHelperRoot = [IO.Path]::GetFullPath($TestHelperDirectory)
$cacheRoot = [IO.Path]::GetFullPath($CacheDirectory)
$utf8 = [Text.UTF8Encoding]::new($false)
$records = [Collections.Generic.List[object]]::new()
$httpHeaders = @{ 'User-Agent' = 'PCSX2-Neural-Research'; 'Accept' = 'application/vnd.github+json' }
foreach ($dir in @($runtimeRoot,$cacheRoot,(Join-Path $runtimeRoot 'licenses'),(Join-Path $runtimeRoot 'neural-rendering'),(Join-Path $runtimeRoot 'reshade-shaders\Shaders'),(Join-Path $runtimeRoot 'reshade-shaders\Textures'))) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}
function Save-Text([string]$Path,[string]$Text) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path)) | Out-Null
    [IO.File]::WriteAllText($Path,$Text,$utf8)
}
function Fetch([string]$Url,[string]$Name,[string]$Component,[string]$Version,[string]$License,[string]$Expected = '') {
    $dest = Join-Path $cacheRoot $Name
    if (-not (Test-Path -LiteralPath $dest -PathType Leaf)) {
        Write-Host "Downloading $Component $Version"
        Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile ($dest + '.part') -Headers @{ 'User-Agent'='PCSX2-Neural-Research' } -TimeoutSec 180
        Move-Item -LiteralPath ($dest + '.part') -Destination $dest -Force
    }
    $hash = (Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($Expected -and $hash -ne $Expected.ToLowerInvariant()) { throw "Published checksum mismatch: $Component ($dest)" }
    $records.Add([ordered]@{component=$Component; version=$Version; url=$Url; archive=$Name; sha256=$hash; bytes=(Get-Item -LiteralPath $dest).Length; license=$License; upstreamChecksumVerified=[bool]$Expected})
    return $dest
}
function Release([string]$Repo,[string]$Tag) {
    return Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases/tags/$Tag" -Headers $httpHeaders -TimeoutSec 45
}
function Open-Zip([string]$Path) {
    # .NET may reject self-extracting ZIP offsets. Retry from each local-file
    # header, validating central directory entries before accepting a candidate.
    $bytes = [IO.File]::ReadAllBytes($Path)
    $offsets = [Collections.Generic.List[int]]::new()
    $offsets.Add(0)
    $isReshadeSetup = $bytes.Length -gt 2 -and $bytes[0] -eq 77 -and $bytes[1] -eq 90
    if ($isReshadeSetup) {
        for ($i=2; $i -lt $bytes.Length-4; $i++) {
            if ($bytes[$i] -eq 80 -and $bytes[$i+1] -eq 75 -and $bytes[$i+2] -eq 3 -and $bytes[$i+3] -eq 4) { $offsets.Add($i) }
        }
    }
    foreach ($offset in $offsets) {
        $stream = [IO.MemoryStream]::new()
        $stream.Write($bytes,$offset,$bytes.Length-$offset)
        $stream.Position=0
        try {
            $zip = [IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Read,$false)
            if ($zip.Entries.Count -eq 0) { throw 'Empty ZIP candidate' }
            if ($isReshadeSetup -and -not ($zip.Entries | Where-Object { $_.Name -eq 'ReShade64.dll' })) { throw 'Not the ReShade payload ZIP' }
            # Opening the first file confirms local-header offsets, too.
            $first = $zip.Entries | Where-Object { $_.Name } | Select-Object -First 1
            if (-not $first) { throw 'ZIP contains no files' }
            $probe=$first.Open(); $probe.Dispose()
            return $zip
        } catch { if ($zip) { $zip.Dispose() }; $stream.Dispose() }
    }
    throw "No readable ZIP archive in $Path"
}
function Extract-Entry($Entry,[string]$Destination) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Destination)) | Out-Null
    $inputStream=$Entry.Open()
    $outputStream=[IO.File]::Create($Destination)
    try { $inputStream.CopyTo($outputStream) } finally { $inputStream.Dispose(); $outputStream.Dispose() }
}
function Extract-Named([string]$Archive,[string]$Name,[string]$Destination) {
    $zip=Open-Zip $Archive
    try {
        $entries=@($zip.Entries | Where-Object { $_.Name -eq $Name })
        if ($entries.Count -ne 1) { throw "Expected exactly one $Name in $Archive, found $($entries.Count)" }
        Extract-Entry $entries[0] $Destination
    } finally { $zip.Dispose() }
}
function Fetch-ReleaseZip([string]$Repo,[string]$Tag,[string]$Component,[string]$License,[string]$Expected='') {
    $release=Release $Repo $Tag
    $assets=@($release.assets | Where-Object { $_.name -match '\.zip$' -and $_.name -notmatch '(?i)layer|source|pdb|debug' })
    if ($assets.Count -ne 1) { throw "Ambiguous ZIP assets for $Repo ${Tag}: $($assets.name -join ', ')" }
    return Fetch $assets[0].browser_download_url $assets[0].name $Component $Tag $License $Expected
}

$setup=Fetch "https://reshade.me/downloads/ReShade_Setup_${ReShadeVersion}_Addon.exe" "ReShade_Setup_${ReShadeVersion}_Addon.exe" 'ReShade add-on build' $ReShadeVersion 'BSD-3-Clause plus bundled notices'
Extract-Named $setup 'ReShade64.dll' (Join-Path $runtimeRoot 'ReShade64.dll')
$reshadeLicense=Fetch "https://raw.githubusercontent.com/crosire/reshade/v$reshadeVersion/LICENSE.md" "ReShade-$reshadeVersion-LICENSE.md" 'ReShade license' $reshadeVersion 'BSD-3-Clause'
Copy-Item -LiteralPath $reshadeLicense -Destination (Join-Path $runtimeRoot 'licenses\ReShade-LICENSE.md')

$feederExpected=if ($FeederTag -eq 'v1.16.0-beta.4') {'D16F8B527F76FF1F531682A576D699CB5634838C0E2899585E3671814EDA2745'} else {''}
$feeder=Fetch-ReleaseZip 'jlrouzies-fr/DLSS5-Feeder' $FeederTag 'DLSS5-Feeder' 'MIT; third-party notices' $feederExpected
Extract-Named $feeder 'dlss5-feed.addon64' (Join-Path $runtimeRoot 'dlss5-feed.addon64')
Extract-Named $feeder 'DLSS5_Feed.fx' (Join-Path $runtimeRoot 'reshade-shaders\Shaders\DLSS5_Feed.fx')
Extract-Named $feeder 'dlss5-feed-host64.exe' (Join-Path $testHelperRoot 'dlss5-feed-host64.exe')
$feedZip=Open-Zip $feeder
try {
    $layerEntry=$feedZip.Entries | Where-Object { $_.Name -eq 'VkLayer_feed_vk.dll' } | Select-Object -First 1
    if ($layerEntry) { Extract-Entry $layerEntry (Join-Path $runtimeRoot 'neural-rendering\VkLayer_feed_vk.dll') }
    $nested=$feedZip.Entries | Where-Object { $_.Name -eq 'feed-vk-layer.zip' } | Select-Object -First 1
    if (-not $layerEntry -and $nested) {
        $nestedPath=Join-Path $cacheRoot "$FeederTag-feed-vk-layer.zip"
        Extract-Entry $nested $nestedPath
        Extract-Named $nestedPath 'VkLayer_feed_vk.dll' (Join-Path $runtimeRoot 'neural-rendering\VkLayer_feed_vk.dll')
    }
} finally { $feedZip.Dispose() }
if (-not (Test-Path -LiteralPath (Join-Path $runtimeRoot 'neural-rendering\VkLayer_feed_vk.dll'))) {
    $layerAsset=(Release 'jlrouzies-fr/DLSS5-Feeder' $FeederTag).assets | Where-Object { $_.name -match '(?i)layer.*\.zip$' } | Select-Object -First 1
    if (-not $layerAsset) { throw 'Feeder release has no x64 fallback layer.' }
    $layerZip=Fetch $layerAsset.browser_download_url $layerAsset.name 'Feeder Vulkan layer' $FeederTag 'MIT'
    Extract-Named $layerZip 'VkLayer_feed_vk.dll' (Join-Path $runtimeRoot 'neural-rendering\VkLayer_feed_vk.dll')
}
$feedLicense=Fetch "https://raw.githubusercontent.com/jlrouzies-fr/DLSS5-Feeder/$FeederTag/LICENSE" "Feeder-$FeederTag-LICENSE" 'Feeder license' $FeederTag 'MIT'
Copy-Item -LiteralPath $feedLicense -Destination (Join-Path $runtimeRoot 'licenses\Feeder-LICENSE.txt')

$renoExpected=if ($RenoDxTag -eq 'renodx-dlss5-4.55') {'15481c492db76682e9a88917e7f78897351ecf088bfae9bca74a0c5b74ddd033'} else {''}
$reno=Fetch-ReleaseZip 'RankFTW/rhi-repo' $RenoDxTag 'RenoDX DLSS5 neural consumer' 'Closed-source community binary; redistribution permission unverified' $renoExpected
Extract-Named $reno 'renodx-dlss5.addon64' (Join-Path $runtimeRoot 'renodx-dlss5.addon64')
$modelExpected=if ($NeuralModelTag -eq 'dlssnr-310.8.SF-v2') {'1da35941894994eb087e017577829e492454e9bae3a6a9397027069ceb74955c'} else {''}
$model=Fetch-ReleaseZip 'RankFTW/rhi-repo' $NeuralModelTag 'NVIDIA DLSS neural model (community patched)' 'Proprietary NVIDIA/community patch; redistribution permission unverified' $modelExpected
Extract-Named $model 'nvngx_dlssnr.dll' (Join-Path $runtimeRoot 'nvngx_dlssnr.dll')
$dlss=Fetch "https://raw.githubusercontent.com/NVIDIA/DLSS/$DlssTag/lib/Windows_x86_64/rel/nvngx_dlss.dll" "NVIDIA-DLSS-$DlssTag-nvngx_dlss.dll" 'NVIDIA DLSS SR runtime' $DlssTag 'NVIDIA DLSS license'
Copy-Item -LiteralPath $dlss -Destination (Join-Path $runtimeRoot 'nvngx_dlss.dll')
# Separate upstream diagnostic helper. Never put this helper in the product kit:
# it is only for the maintainer's explicit NGX self-test, and is not run here.
foreach ($helperFile in @('ReShade64.dll','renodx-dlss5.addon64','nvngx_dlssnr.dll','nvngx_dlss.dll')) {
    $helperName=if ($helperFile -eq 'ReShade64.dll') {'dxgi.dll'} else {$helperFile}
    Copy-Item -LiteralPath (Join-Path $runtimeRoot $helperFile) -Destination (Join-Path $testHelperRoot $helperName)
}
Save-Text (Join-Path $testHelperRoot 'ReShade.ini') "[ADDON]`nAddonPath=.\`n[RenoDX.DLSS5]`nNeuralUplift=1`nNREnableUpscaling=0`nEnableHooks=2`nNRStyle=1`nNRPaperWhiteScale=15.401`nNRIntensity=1`nNRTransferStrength=1`nNRColorStrength=1`n"
Save-Text (Join-Path $testHelperRoot 'README.md') "# Feeder diagnostic helper`n`nOfficial dlss5-feed-host64.exe from $FeederTag, extracted for local testing only. No helper or downloaded library has been executed by the downloader. The helper is deliberately outside research/runtime and is not a product runtime component.`n`nUpstream documents --test as a standalone NGX test with 300 evaluations. Run only as an explicit diagnostic after reviewing host source and installed files. The helper loads the adjacent ReShade dxgi.dll, exactly one neural consumer, and NVIDIA runtimes. It performs GPU work, writes logs beside its executable, and may crash in an incompatible neural runtime/driver. This is not an PCSX2 game compatibility test.`n`nLaunch through Start-Process with -WindowStyle Hidden, -ArgumentList '--test', -WorkingDirectory set to this directory, and a bounded timeout. Hiding the process window does not verify that the helper never creates another window; inspect source before running. Do not use --pipe normal host mode for this standalone test. Preserve stdout/stderr and dlss5-feed-host.log/ReShade.log for inspection.`n`nReview source: https://github.com/jlrouzies-fr/DLSS5-Feeder/tree/$FeederTag/host`n"
$dlssLicense=Fetch "https://raw.githubusercontent.com/NVIDIA/DLSS/$DlssTag/LICENSE.txt" "NVIDIA-DLSS-$DlssTag-LICENSE.txt" 'NVIDIA DLSS license' $DlssTag 'NVIDIA DLSS license'
Copy-Item -LiteralPath $dlssLicense -Destination (Join-Path $runtimeRoot 'licenses\NVIDIA-DLSS-LICENSE.txt')

$lumeniteSha='f8cbbb4eccfcb7adf0d74bb358ba349272e3c1e9'
$lumenite=Fetch "https://codeload.github.com/umar-afzaal/LumeniteFX/zip/$lumeniteSha" "LumeniteFX-$lumeniteSha.zip" 'LumeniteFX' $lumeniteSha 'AGNYA rev1.4; local use only, public rehosting prohibited'
$lumeniteZip=Open-Zip $lumenite
try {
    foreach ($entry in $lumeniteZip.Entries) {
        $relative=$entry.FullName -replace '^[^/]+/',''
        if ($relative -match '^Shaders/(lumenite_[^/]+\.fx|include/[^/]+\.fxh)$' -or $relative -eq 'Textures/lumenite_bluenoise256.png') {
            Extract-Entry $entry (Join-Path $runtimeRoot ('reshade-shaders/'+$relative))
        } elseif ($relative -in @('LICENSE.md','NOTICE')) {
            Extract-Entry $entry (Join-Path $runtimeRoot ('licenses/LumeniteFX-'+$relative))
        }
    }
} finally { $lumeniteZip.Dispose() }
$headersSha='6db142b4b1a05c764222e5b0bd9a644b7ccfe1dc'
foreach ($header in @('ReShade.fxh','ReShadeUI.fxh','DrawText.fxh')) {
    $headerPath=Fetch "https://raw.githubusercontent.com/crosire/reshade-shaders/$headersSha/Shaders/$header" "$headersSha-$header" 'ReShade shader headers' $headersSha 'Copyright and license notices retained in files'
    Copy-Item -LiteralPath $headerPath -Destination (Join-Path $runtimeRoot "reshade-shaders\Shaders\$header")
}

$manifest=[ordered]@{file_format_version='1.0.0'; layer=[ordered]@{name='VK_LAYER_PCSX2_reshade'; type='GLOBAL'; library_path='..\ReShade64.dll'; api_version='1.3.268'; implementation_version='1'; description='ReShade for this PCSX2 installation'; device_extensions=@(@{name='VK_EXT_tooling_info'; spec_version='1'; entrypoints=@('vkGetPhysicalDeviceToolPropertiesEXT')}); disable_environment=@{DISABLE_VK_LAYER_PCSX2_reshade='1'}}}
Save-Text (Join-Path $runtimeRoot 'neural-rendering\ReShade64.json') ($manifest | ConvertTo-Json -Depth 8)
$manifest=[ordered]@{file_format_version='1.2.0'; layer=[ordered]@{name='VK_LAYER_feed_vk'; type='GLOBAL'; library_path='.\VkLayer_feed_vk.dll'; api_version='1.3.280'; implementation_version='1'; description='DLSS5-Feeder external-memory device features'; functions=@{vkNegotiateLoaderLayerInterfaceVersion='vkNegotiateLoaderLayerInterfaceVersion'}; disable_environment=@{DISABLE_VK_LAYER_feed_vk='1'}}}
Save-Text (Join-Path $runtimeRoot 'neural-rendering\VkLayer_feed_vk.json') ($manifest | ConvertTo-Json -Depth 8)
Save-Text (Join-Path $runtimeRoot 'ReShade.ini') "[ADDON]`nAddonPath=.\`n[GENERAL]`nEffectSearchPaths=.\reshade-shaders\Shaders\**`nTextureSearchPaths=.\reshade-shaders\Textures\**`nPresetPath=.\ReShadePreset.ini`nPreprocessorDefinitions=DLSS5_MV_PROVIDER=3`n[RenoDX.DLSS5]`nNeuralUplift=1`nNREnableUpscaling=0`nEnableHooks=2`nNRStyle=1`nNRPaperWhiteScale=15.401`nNRIntensity=1`nNRTransferStrength=1`nNRColorStrength=1`n"
Save-Text (Join-Path $runtimeRoot 'ReShadePreset.ini') "Techniques=Lumenite_Kernel@lumenite_Kernel.fx,DLSS5_Feed@DLSS5_Feed.fx`nTechniqueSorting=Lumenite_Kernel@lumenite_Kernel.fx,DLSS5_Feed@DLSS5_Feed.fx`n"
Save-Text (Join-Path $runtimeRoot 'dlss5-feed.cfg') "enabled=1`nmode=2`nhdr=1`ndepth_inverted=1`ncreate_delay=60`nwarmup_rebuild=180`npreset=0`nwork_resolution=100`nwork_upscale=0`ngpu_timeout_ms=2000`nmv_scale_x=1.0`nmv_scale_y=1.0`nvk_present_sync=1`n"
Save-Text (Join-Path $runtimeRoot 'LOCAL-USE-NOTICE.txt') "This kit downloads from upstream for local use. It is not cleared for public redistribution. LumeniteFX AGNYA prohibits public rehosting; RenoDX NR and patched NVIDIA NR runtime redistribution permission is unverified. Preserve all component licenses. See PROVENANCE.json for sources and hashes."
Save-Text (Join-Path $runtimeRoot 'PROVENANCE.json') ($records | ConvertTo-Json -Depth 6)
$files=Get-ChildItem -LiteralPath $runtimeRoot -File -Recurse | ForEach-Object { [ordered]@{path=$_.FullName.Substring($runtimeRoot.Length+1); bytes=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()} }
Save-Text (Join-Path $runtimeRoot 'SHA256SUMS.json') ($files | ConvertTo-Json -Depth 6)
Write-Host "Runtime prepared at $runtimeRoot. No downloaded code was executed."
