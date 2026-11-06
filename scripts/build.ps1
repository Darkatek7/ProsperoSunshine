# -Debug builds the debug flavor into out/debug (README, "Debug build").
param([switch]$RebuildImage, [switch]$Debug)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$image = 'prosperosunshine-sdk:v0.45'
docker image inspect $image 2>$null | Out-Null
if ($RebuildImage -or $LASTEXITCODE -ne 0) {
    docker build -t $image -f "$PSScriptRoot/Dockerfile" $projectRoot
    if ($LASTEXITCODE -ne 0) { throw 'SDK image build failed' }
}
# Here, not in the container: a worktree's .git points outside what the container sees.
$commit = git -C $projectRoot rev-parse --short=8 HEAD
$flavor = if ($Debug) { '1' } else { '0' }
docker run --rm -e "PROSPERO_COMMIT=$commit" -e "PROSPERO_DEBUG=$flavor" --mount "type=bind,source=$projectRoot,target=/repo" $image bash scripts/build.sh
if ($LASTEXITCODE -ne 0) { throw 'PS5 build failed' }
