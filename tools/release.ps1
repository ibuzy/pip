# Публичная прошивка для панели -> docs\firmware\firmware.js
# (каждая часть образа со своим адресом, base64; NVS - питомец, настройки, связка с телефоном - не трогается).
#   tools\release.ps1        (нужен ESP-IDF 5.5: tools\setup-windows.ps1)
. (Join-Path $PSScriptRoot 'winenv.ps1')
Use-Idf
Push-Location (Join-Path $KeyPetRoot 'firmware')
$log = cmd /c "python `"$env:IDF_PATH\tools\idf.py`" -B build-release reconfigure build 2>&1"
$code = $LASTEXITCODE
Pop-Location
if ($code -ne 0) { $log | Select-Object -Last 30; Fail 'сборка release не прошла' }
& python (Join-Path $PSScriptRoot 'pack_firmware.py')
exit $LASTEXITCODE
