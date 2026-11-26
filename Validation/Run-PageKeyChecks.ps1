param([Parameter(Mandatory=$true)][string]$ZigPath)
$ErrorActionPreference = 'Stop'
$source = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\Firmware\Application\app_tasks.c') -Raw -Encoding UTF8
$start = $source.IndexOf('void App_PagePressedIrq(')
$end = $source.IndexOf('typedef struct', $start)
if ($start -lt 0 -or $end -le $start) { throw 'Page key functions could not be located.' }
$fixture = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'page-key-test.c') -Raw -Encoding UTF8
$testSource = $fixture.Replace('/* CURRENT_PAGE_KEY_FUNCTIONS */', $source.Substring($start, $end - $start))
$irqSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\Firmware\User\stm32f10x_it.c') -Raw -Encoding UTF8
$irqStart = $irqSource.IndexOf('void EXTI3_IRQHandler(')
$irqEnd = $irqSource.IndexOf('/* 串口一', $irqStart)
if ($irqStart -lt 0 -or $irqEnd -le $irqStart) { throw 'EXTI3 handler could not be located.' }
$testSource = $testSource.Replace('/* CURRENT_PAGE_IRQ_HANDLER */', $irqSource.Substring($irqStart, $irqEnd - $irqStart))
$checkDirectory = Join-Path $env:TEMP ('washer-page-key-check-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $checkDirectory | Out-Null
$sourcePath = Join-Path $checkDirectory 'page-key-check.c'
$binaryPath = Join-Path $checkDirectory 'page-key-check.exe'
[System.IO.File]::WriteAllText($sourcePath, $testSource, [System.Text.UTF8Encoding]::new($false))
& $ZigPath cc $sourcePath -o $binaryPath
if ($LASTEXITCODE -ne 0) { throw 'Page key check compilation failed.' }
& $binaryPath
if ($LASTEXITCODE -ne 0) { throw 'Page key check failed.' }
