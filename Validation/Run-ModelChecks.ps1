param([Parameter(Mandatory=$true)][string]$ZigPath)
$ErrorActionPreference = 'Stop'
$applicationPath = Join-Path $PSScriptRoot '..\Firmware\Application'
$binaryPath = Join-Path $env:TEMP 'washer-classroom-model-check.exe'
& $ZigPath cc -fshort-enums -I $PSScriptRoot -I $applicationPath (Join-Path $applicationPath 'washer_control.c') (Join-Path $applicationPath 'washer_plant.c') (Join-Path $PSScriptRoot 'test.c') -o $binaryPath
if ($LASTEXITCODE -ne 0) { throw 'Model check compilation failed.' }
& $binaryPath
if ($LASTEXITCODE -ne 0) { throw 'Model check failed.' }
