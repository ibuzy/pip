# Отладочная страница http://localhost:8765 (остановите её перед прошивкой - она держит COM-порт).
# Нужен pyserial: он есть в окружении ESP-IDF, иначе ставится tools\setup-windows.ps1.
. (Join-Path $PSScriptRoot 'winenv.ps1')
$idf = if ($env:IDF_PATH) { $env:IDF_PATH } else { Join-Path $HOME 'esp\esp-idf' }
if (Test-Path (Join-Path $idf 'export.ps1')) { Use-Idf; $py = 'python' } else { $py = Get-Python }
Write-Host 'Отладка Pip: http://localhost:8765  (Ctrl+C - остановить)'
& $py (Join-Path $PSScriptRoot 'pipdebug.py')
