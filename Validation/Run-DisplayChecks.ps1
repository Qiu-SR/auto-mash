param([Parameter(Mandatory=$true)][string]$ZigPath)
$ErrorActionPreference = 'Stop'
$applicationPath = Join-Path $PSScriptRoot '..\Firmware\Application'
$driver = Get-Content -LiteralPath (Join-Path $applicationPath 'lcd1602.c') -Raw -Encoding UTF8
$start = $driver.IndexOf('void LCD_PrintLine(')
$end = $driver.IndexOf('void LCD_SetCursor(', $start)
if ($start -lt 0 -or $end -le $start) { throw 'LCD_PrintLine function could not be located.' }
$fixture = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'display-response-test.c') -Raw -Encoding UTF8
$testSource = $fixture.Replace('/* CURRENT_LCD_PRINT_LINE */', $driver.Substring($start, $end - $start))
$checkDirectory = Join-Path $env:TEMP ('washer-display-check-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $checkDirectory | Out-Null
$sourcePath = Join-Path $checkDirectory 'display-check.c'
$binaryPath = Join-Path $checkDirectory 'display-check.exe'
[System.IO.File]::WriteAllText($sourcePath, $testSource, [System.Text.UTF8Encoding]::new($false))
& $ZigPath cc -I $applicationPath $sourcePath -o $binaryPath
if ($LASTEXITCODE -ne 0) { throw 'Display check compilation failed.' }
& $binaryPath
if ($LASTEXITCODE -ne 0) { throw 'Display check failed.' }
