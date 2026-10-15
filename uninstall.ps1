# SPDX-License-Identifier: MIT
# Unregister the mouselaser driver from SteamVR on Windows.
$ErrorActionPreference = 'Stop'

$dir = Join-Path $PSScriptRoot 'driver\mouselaser'
$vrpaths = Join-Path $env:LOCALAPPDATA 'openvr\openvrpaths.vrpath'
$config = Get-Content $vrpaths -Raw | ConvertFrom-Json
$vrpathreg = Join-Path $config.runtime[0] 'bin\win64\vrpathreg.exe'

& $vrpathreg removedriver $dir
Write-Host 'Unregistered. Restart SteamVR to unload it.'
