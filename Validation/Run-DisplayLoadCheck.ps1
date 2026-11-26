param([Parameter(Mandatory=$true)][string]$ZigPath)
$ErrorActionPreference = 'Stop'
$applicationPath = Join-Path $PSScriptRoot '..\Firmware\Application'
$driver = Get-Content -LiteralPath (Join-Path $applicationPath 'lcd1602.c') -Raw -Encoding UTF8
$start = $driver.IndexOf('void LCD_PrintLine(')
$end = $driver.IndexOf('void LCD_SetCursor(', $start)
if ($start -lt 0 -or $end -le $start) { throw 'Display functions could not be located.' }
$fixture = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'display-response-test.c') -Raw -Encoding UTF8
$fixture = $fixture.Substring(0, $fixture.IndexOf('int main(void)'))
$testSource = $fixture.Replace('/* CURRENT_LCD_PRINT_LINE */', $driver.Substring($start, $end - $start)) + @'
static void ShowPage(const char *const *lines) {
 unsigned i;LCD_BeginFrame(1);
 for(i=0;i<4;++i)LCD_PrintLine(i,lines[i]);
 LCD_EndFrame();
}
int main(void) {
 const char *overview[]={"待机","程序:自动","水量0/0","进度0%  页1/3"};
 const char *parameters[]={"重量40% 污40%","水温25.0度","转速0","不平衡280"};
 const char *outputs[]={"进水关 排水关","加热关 门锁关","注入掩码0","翻页键切换页面"};
 unsigned before;
 ShowPage(overview);before=count;
 LCD_BeginFrame(0);LCD_PrintLine(1,"程序:快洗");LCD_EndFrame();
 printf("MODE update pixel_bytes=%u\n",count-before);before=count;
 ShowPage(parameters);printf("PAGE overview_to_parameters pixel_bytes=%u\n",count-before);before=count;
 ShowPage(outputs);printf("PAGE parameters_to_outputs pixel_bytes=%u\n",count-before);
 return 0;
}
'@
$checkDirectory = Join-Path $env:TEMP ('washer-display-load-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $checkDirectory | Out-Null
$sourcePath = Join-Path $checkDirectory 'display-load.c'
$binaryPath = Join-Path $checkDirectory 'display-load.exe'
[System.IO.File]::WriteAllText($sourcePath, $testSource, [System.Text.UTF8Encoding]::new($false))
& $ZigPath cc -I $applicationPath $sourcePath -o $binaryPath
if ($LASTEXITCODE -ne 0) { throw 'Display load compilation failed.' }
& $binaryPath
if ($LASTEXITCODE -ne 0) { throw 'Display load check failed.' }
