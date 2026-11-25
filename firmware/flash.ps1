# flash.ps1 [секунды] [-Port COM5]: сборка, прошивка, затем лог платы N секунд (по умолчанию 13).
# Порт находится сам (USB с VID 0x303A). Сначала остановите tools\pipdebug.ps1: он держит порт.
param([double]$Secs = 13, [string]$Port)
. (Join-Path $PSScriptRoot '..\tools\winenv.ps1')
Set-Location $PSScriptRoot
Use-Idf

# сборка (reconfigure: шрифты и файлы ui/ подхватываются по маске, новые - только так): из вывода только ошибки и предупреждения; stderr сливаем через cmd, чтобы PowerShell 5.1 не превращал его в исключения
$log = cmd /c "python `"$env:IDF_PATH\tools\idf.py`" reconfigure build 2>&1"
$code = $LASTEXITCODE
$log | Select-String -Pattern 'error:|warning: ' | Select-Object -First 10 | ForEach-Object {
    $_.Line -replace '^.*(error|warning): ', '$1: '
}
if ($code -ne 0) {
    if (-not ($log | Select-String 'error:')) { $log | Select-Object -Last 20 }
    Fail 'сборка не прошла'
}

if (-not $Port) { $Port = (& python serlog.py --which) }
if (-not $Port) { Fail 'плата не найдена: подключите брелок по USB (если не видна - зажмите BOOT и нажмите RESET)' }
Write-Host "Порт: $Port"

$log = cmd /c "python `"$env:IDF_PATH\tools\idf.py`" -p $Port -b 921600 flash 2>&1"
$code = $LASTEXITCODE
$log | Select-String -Pattern 'error|Error|Hard reset' | Select-Object -First 3 | ForEach-Object { $_.Line }
if ($code -ne 0) { Fail 'прошивка не прошла' }

& python serlog.py $Secs --reset --port $Port
