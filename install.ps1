# SPDX-License-Identifier: MIT
# Register the mouselaser driver with SteamVR on Windows.
# Only touches %LOCALAPPDATA%\openvr\openvrpaths.vrpath (a backup is made first).
$ErrorActionPreference = 'Stop'

$dir = Join-Path $PSScriptRoot 'driver\mouselaser'
$vrpaths = Join-Path $env:LOCALAPPDATA 'openvr\openvrpaths.vrpath'

if (-not (Test-Path (Join-Path $dir 'bin\win64\driver_mouselaser.dll'))) {
    throw 'Driver not built. See "Build and install" in the README.'
}
if (-not (Test-Path $vrpaths)) {
    throw "Cannot find $vrpaths. Start SteamVR once so it creates it."
}

$config = Get-Content $vrpaths -Raw | ConvertFrom-Json
$vrpathreg = Join-Path $config.runtime[0] 'bin\win64\vrpathreg.exe'
if (-not (Test-Path $vrpathreg)) { throw "vrpathreg.exe not found at $vrpathreg" }

# Two drivers with the same name would conflict.
foreach ($p in @($config.external_drivers)) {
    if ($p -and ((Resolve-Path $p -ErrorAction SilentlyContinue).Path -ne (Resolve-Path $dir).Path)) {
        $manifest = Join-Path $p 'driver.vrdrivermanifest'
        if ((Test-Path $manifest) -and ((Get-Content $manifest -Raw | ConvertFrom-Json).name -eq 'mouselaser')) {
            throw "Another mouselaser driver is already registered: $p`nRemove it first: & '$vrpathreg' removedriver '$p'"
        }
    }
}

Copy-Item $vrpaths "$vrpaths.bak-mouselaser" -Force
& $vrpathreg adddriver $dir
& $vrpathreg show
Write-Host 'Registered. Restart SteamVR to load it.'
