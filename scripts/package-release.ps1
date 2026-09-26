#!/usr/bin/env pwsh
#Requires -Version 5.1
# Package release ZIPs for Skyrim SE Head Tracking.
# Produces two archives in release/:
#   - SkyrimSEHeadTracking-v<version>-installer.zip (GitHub Releases)
#       launcher-manifest.json + install.cmd + uninstall.cmd + plugins/
#       + vendor/ultimate-asi-loader/
#       + shared/ (find-game.ps1 + GamePathDetection.psm1 + games.json)
#       + docs
#   - SkyrimSEHeadTracking-v<version>-nexus.zip (Nexus Mods)
#       SkyrimSEHeadTracking.asi at the archive root, alongside
#       README/CHANGELOG/THIRD-PARTY-NOTICES/LICENSE. Users drop the .asi
#       next to SkyrimSE.exe (Vortex, MO2 with Root Builder, manual all work
#       the same way). No config - the mod creates CameraUnlock.ini on
#       first launch, so bundling one would clobber user config on update.
#       Nexus users manage their own ASI loader.
#
# Vendoring is refreshed manually by the dev via 'pixi run update-deps'
# (scripts/update-deps.ps1) and committed under vendor/. CI never refreshes;
# this script consumes whatever is committed.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectDir "cameraunlock-core/powershell/ReleaseWorkflow.psm1") -Force

# cameraunlock-core is MIT under a different copyright holder (CameraUnlock)
# than this mod's own LICENSE (itsloopyo), and it is compiled into the .asi, so
# MIT wants its notice travelling with the binary in BOTH ZIPs. Defined here
# rather than taken from the submodule on purpose: a helper added to core does
# not reach this mod until its submodule pointer moves, and a licence
# obligation cannot wait on that.
function Copy-CoreLicense {
    param([Parameter(Mandatory)][string]$StagingDir)

    $src = Join-Path $projectDir "cameraunlock-core/LICENSE"
    if (-not (Test-Path $src)) {
        throw "cameraunlock-core/LICENSE not found at: $src. Run 'git submodule update --init' - the core licence must ship in every release ZIP."
    }

    $destDir = Join-Path $StagingDir "licenses"
    New-Item -ItemType Directory -Path $destDir -Force | Out-Null
    Copy-Item $src -Destination (Join-Path $destDir "cameraunlock-core-LICENSE.txt") -Force
    Write-Host "  licenses/cameraunlock-core-LICENSE.txt" -ForegroundColor Green
}

$manifest = Get-Content (Join-Path $projectDir "manifest.json") -Raw | ConvertFrom-Json
$version  = $manifest.version
$modName  = 'SkyrimSEHeadTracking'

Write-Host "=== $modName - Package Release ===" -ForegroundColor Magenta
Write-Host ""
Write-Host "Version: $version" -ForegroundColor Cyan
Write-Host ""

$releaseDir = Join-Path $projectDir "release"
if (-not (Test-Path $releaseDir)) {
    New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
}

# --- Required source artifacts ------------------------------------------------

$asiPath = Join-Path $projectDir "bin/Release/$modName.asi"
if (-not (Test-Path $asiPath)) {
    throw "$modName.asi not found at: $asiPath. Run 'pixi run build-release' first."
}

$scriptsDir = Join-Path $projectDir "scripts"
foreach ($script in @("install.cmd", "uninstall.cmd")) {
    $p = Join-Path $scriptsDir $script
    if (-not (Test-Path $p)) { throw "Required script not found: $p" }
}

$vendorDir = Join-Path $projectDir "vendor/ultimate-asi-loader"
foreach ($f in @('dinput8.dll', 'LICENSE', 'README.md')) {
    $p = Join-Path $vendorDir $f
    if (-not (Test-Path $p)) {
        throw "vendor/ultimate-asi-loader/$f missing. Run 'pixi run update-deps' to populate."
    }
}

# --- Installer ZIP ------------------------------------------------------------

Write-Host "--- Installer ZIP ---" -ForegroundColor Yellow
Write-Host ""

$stagingInstaller = Join-Path $releaseDir "staging-installer"
if (Test-Path $stagingInstaller) { Remove-Item -Recurse -Force $stagingInstaller }
New-Item -ItemType Directory -Path $stagingInstaller -Force | Out-Null

foreach ($script in @("install.cmd", "uninstall.cmd")) {
    Copy-Item (Join-Path $scriptsDir $script) -Destination $stagingInstaller -Force
    Write-Host "  $script" -ForegroundColor Green
}

# Launcher manifest: the file lopari reads at the installer ZIP root to learn
# the mod's identity, strategy, delivery mode and where its config file is.
# Stamped to the release version. No config is shipped: the mod creates
# CameraUnlock.ini at first launch.
$manifestSource = Join-Path $projectDir "launcher-manifest.json"
if (-not (Test-Path $manifestSource)) { throw "launcher-manifest.json not found at: $manifestSource" }

$launcherManifest = Get-Content $manifestSource -Raw | ConvertFrom-Json
$launcherManifest.mod_info.version = $version
$launcherManifest | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $stagingInstaller "launcher-manifest.json") -NoNewline
Write-Host "  launcher-manifest.json" -ForegroundColor Green

$pluginsDir = Join-Path $stagingInstaller "plugins"
New-Item -ItemType Directory -Path $pluginsDir -Force | Out-Null
Copy-Item $asiPath -Destination $pluginsDir -Force
Write-Host "  plugins/$modName.asi" -ForegroundColor Green

$vendorDest = Join-Path $stagingInstaller "vendor/ultimate-asi-loader"
New-Item -ItemType Directory -Path $vendorDest -Force | Out-Null
foreach ($f in @('dinput8.dll', 'LICENSE', 'README.md')) {
    Copy-Item (Join-Path $vendorDir $f) -Destination $vendorDest -Force
    Write-Host "  vendor/ultimate-asi-loader/$f" -ForegroundColor Green
}

# install.cmd expects shared/find-game.ps1 + GamePathDetection.psm1 + games.json
Copy-SharedBundle -StagingDir $stagingInstaller

# LICENSE and THIRD-PARTY-NOTICES.md carry the notices for everything linked
# into the .asi, so a missing one is a licence violation rather than a cosmetic
# gap. Throw instead of skipping quietly: a guarded copy turns that violation
# into a green build.
foreach ($doc in @("README.md", "CHANGELOG.md", "THIRD-PARTY-NOTICES.md", "LICENSE")) {
    $p = Join-Path $projectDir $doc
    if (-not (Test-Path $p)) { throw "Required doc for installer ZIP not found: $p" }
    Copy-Item $p -Destination $stagingInstaller -Force
    Write-Host "  $doc" -ForegroundColor Green
}

Copy-CoreLicense -StagingDir $stagingInstaller

$installerZip = Join-Path $releaseDir "$modName-v$version-installer.zip"
if (Test-Path $installerZip) { Remove-Item $installerZip -Force }

Write-Host ""
Write-Host "Creating installer ZIP..." -ForegroundColor Cyan
Push-Location $stagingInstaller
try {
    Compress-Archive -Path ".\*" -DestinationPath $installerZip -Force
} finally { Pop-Location }
Remove-Item -Recurse -Force $stagingInstaller

$installerKB = (Get-Item $installerZip).Length / 1KB
Write-Host ("  $installerZip ({0:N1} KB)" -f $installerKB) -ForegroundColor Green

# --- Nexus ZIP ----------------------------------------------------------------

Write-Host ""
Write-Host "--- Nexus ZIP ---" -ForegroundColor Yellow
Write-Host ""

$stagingNexus = Join-Path $releaseDir "staging-nexus"
if (Test-Path $stagingNexus) { Remove-Item -Recurse -Force $stagingNexus }
New-Item -ItemType Directory -Path $stagingNexus -Force | Out-Null

Copy-Item $asiPath -Destination $stagingNexus -Force
Write-Host "  $modName.asi" -ForegroundColor Green

# No config is shipped: the mod creates CameraUnlock.ini on first launch, and
# imports an earlier version's HeadTracking.ini into it once. A copy of either
# in the ZIP would overwrite the player's settings when extracted over the game
# folder, and a CameraUnlock.ini there would also stop that import.
#
# Docs sit at the archive root (informational, not deployed to the game
# folder). THIRD-PARTY-NOTICES travels with the binary for attribution of the
# statically-linked libraries (MinHook, inih).
foreach ($doc in @("README.md", "CHANGELOG.md", "THIRD-PARTY-NOTICES.md", "LICENSE")) {
    $p = Join-Path $projectDir $doc
    if (-not (Test-Path $p)) { throw "Required doc for Nexus ZIP not found: $p" }
    Copy-Item $p -Destination $stagingNexus -Force
    Write-Host "  $doc" -ForegroundColor Green
}

Copy-CoreLicense -StagingDir $stagingNexus

$nexusZip = Join-Path $releaseDir "$modName-v$version-nexus.zip"
if (Test-Path $nexusZip) { Remove-Item $nexusZip -Force }

Write-Host ""
Write-Host "Creating nexus ZIP..." -ForegroundColor Cyan
# Use tar.exe (bsdtar) instead of Compress-Archive. Windows PowerShell 5.1's
# Compress-Archive writes backslash path separators into the zip, which is not
# spec-compliant: stricter extractors treat "Root\file.asi" as a literal
# filename rather than a Root/ folder, defeating the MO2 Root Builder layout.
# bsdtar writes forward slashes.
$tarExe = Join-Path $env:SystemRoot "System32\tar.exe"
Push-Location $stagingNexus
try {
    & $tarExe -a -c -f $nexusZip *
    if ($LASTEXITCODE -ne 0) { throw "tar.exe failed to create the Nexus ZIP (exit $LASTEXITCODE)" }
} finally { Pop-Location }
Remove-Item -Recurse -Force $stagingNexus

$nexusKB = (Get-Item $nexusZip).Length / 1KB
Write-Host ("  $nexusZip ({0:N1} KB)" -f $nexusKB) -ForegroundColor Green

# --- Summary ------------------------------------------------------------------

Write-Host ""
Write-Host "=== Package Complete ===" -ForegroundColor Magenta
Write-Host ""
Write-Host ("Installer: $installerZip ({0:N1} KB)" -f $installerKB) -ForegroundColor Green
Write-Host ("Nexus:     $nexusZip ({0:N1} KB)" -f $nexusKB) -ForegroundColor Green

Write-Output $installerZip
Write-Output $nexusZip
