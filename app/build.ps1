# SPDX-License-Identifier: GPL-3.0-only
# Builds the PS5 app in Docker (app/build.sh). The result is build/app/PPSA47989 and its zip.
#   -HostElf      the host payload to bundle (default: out/prospero_sunshine.elf, built by scripts/build.ps1)
#   -OpenGlSdk    a ps5-opengl SDK folder to put into the build cache (only needed once; without it the
#                 SDK is copied from PS5SD's Docker volume ps5sd-work when that exists)
#   -RebuildImage rebuild the build image
#   -Debug        the debug app, bundling out/debug/prospero_sunshine.elf (scripts/build.ps1 -Debug); the result is
#                 build/app-debug/PPSA47989 and PPSA47989-debug.zip
param([string]$HostElf, [string]$OpenGlSdk, [switch]$RebuildImage, [switch]$Debug)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$image = 'prosperosunshine-app:2'
$cache = 'prosperosunshine-app-cache'

docker image inspect $image 2>$null | Out-Null
if ($RebuildImage -or $LASTEXITCODE -ne 0) {
    docker build -t $image -f "$PSScriptRoot/Dockerfile" $PSScriptRoot
    if ($LASTEXITCODE -ne 0) { throw 'app build image failed' }
}

# The ps5-opengl SDK, once, into the cache volume.
docker run --rm --mount "type=volume,source=$cache,target=/cache" $image test -f /cache/ps5-opengl-sdk/manifest.sha256
if ($LASTEXITCODE -ne 0) {
    if ($OpenGlSdk) {
        docker run --rm --mount "type=bind,source=$((Resolve-Path $OpenGlSdk).Path),target=/sdk,readonly" `
            --mount "type=volume,source=$cache,target=/cache" $image cp -a /sdk /cache/ps5-opengl-sdk
    } else {
        docker volume inspect ps5sd-work 2>$null | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'no ps5-opengl SDK: pass -OpenGlSdk <folder> (see app/README.md)' }
        docker run --rm --mount 'type=volume,source=ps5sd-work,target=/src,readonly' `
            --mount "type=volume,source=$cache,target=/cache" $image cp -a /src/.deps/ps5-opengl-sdk /cache/ps5-opengl-sdk
    }
    if ($LASTEXITCODE -ne 0) { throw 'copying the ps5-opengl SDK failed' }
}

if (-not $HostElf) { $HostElf = Join-Path $projectRoot $(if ($Debug) { 'out/debug/prospero_sunshine.elf' } else { 'out/prospero_sunshine.elf' }) }
$dockerArgs = @('run', '--rm', '--mount', "type=bind,source=$projectRoot,target=/repo",
    '--mount', "type=volume,source=$cache,target=/cache", '-e', "APP_FLAVOR=$(if ($Debug) { 'debug' } else { 'normal' })")
if (Test-Path $HostElf) {
    $elf = (Resolve-Path $HostElf).Path
    $dockerArgs += @('--mount', "type=bind,source=$(Split-Path -Parent $elf),target=/hostelf,readonly",
        '-e', "HOST_ELF=/hostelf/$(Split-Path -Leaf $elf)")
} else {
    Write-Warning "no host ELF at $HostElf; the app is built without one"
    $dockerArgs += @('-e', 'HOST_ELF=/nonexistent')
}
docker @dockerArgs $image bash app/build.sh
if ($LASTEXITCODE -ne 0) { throw 'app build failed' }
