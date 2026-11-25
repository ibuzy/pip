# Ставит всё для разработки KeyPet на Windows 10/11. Повторный запуск безопасен: готовое пропускается.
#   powershell -ExecutionPolicy Bypass -File tools\setup-windows.ps1              # всё
#   powershell -ExecutionPolicy Bypass -File tools\setup-windows.ps1 -NoFirmware  # только симуляторы (без ESP-IDF, ~3 ГБ)
#   ... -NoWeb                                                                    # без веб-симулятора (emsdk, ~1 ГБ)
# Что ставится:
#   winget : CMake, Ninja, LLVM-MinGW (clang для pipsim), FFmpeg (pipsim пишет PNG/GIF/MP4 через него)
#   pip    : Pillow (sim\check.py), pyserial (serlog.py, pipdebug.py)
#   git    : LVGL 9.5.0 в third_party\lvgl (для симуляторов; прошивка берёт LVGL из менеджера компонентов IDF)
#            emsdk в $HOME\emsdk, ESP-IDF v5.5.x в $HOME\esp\esp-idf (+ install.ps1 esp32s3)
param([switch]$NoFirmware, [switch]$NoWeb, [string]$IdfVersion = 'v5.5.6', [string]$LvglVersion = 'v9.5.0')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'winenv.ps1')

if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Fail 'нужен Git: winget install Git.Git, затем откройте новое окно PowerShell' }
if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { Fail 'нужен winget (App Installer из Microsoft Store)' }

Write-Step 'Инструменты сборки (winget)'
foreach ($id in 'Kitware.CMake', 'Ninja-build.Ninja', 'MartinStorsjo.LLVM-MinGW.UCRT', 'Gyan.FFmpeg') {
    $listed = winget list --id $id -e --accept-source-agreements --disable-interactivity 2>$null | Select-String -SimpleMatch $id
    if ($listed) { Write-Host "  $id уже установлен" }
    else {
        winget install --id $id -e --silent --accept-package-agreements --accept-source-agreements --disable-interactivity
        if ($LASTEXITCODE -ne 0) { Fail "winget не смог поставить $id" }
    }
}
Use-HostTools

Write-Step 'Python-пакеты'
$py = Get-Python
# Системный SOCKS-прокси Windows (VPN-клиенты вроде v2rayN/Nekoray): Python видит его, но pip и загрузчик ESP-IDF
# (urllib) SOCKS не умеют - такие загрузки идут напрямую, мимо прокси
$socks = (& $py -c "import urllib.request as u; print(any('socks' in v for v in u.getproxies().values()))") -eq 'True'
if ($socks) { Write-Host '  системный прокси - SOCKS: pip и ESP-IDF качают напрямую (NO_PROXY=*)'; $env:NO_PROXY = '*' }
& $py -m pip install --user --quiet --disable-pip-version-check --no-warn-script-location --upgrade pillow pyserial pysocks
if ($LASTEXITCODE -ne 0) { Fail 'pip install pillow pyserial' }

Write-Step "LVGL $LvglVersion -> third_party\lvgl"
$lvgl = Join-Path $KeyPetRoot 'third_party\lvgl'
if (Test-Path (Join-Path $lvgl 'lvgl.h')) { Write-Host '  уже есть' }
else {
    if (Test-Path $lvgl) { Remove-Item $lvgl -Recurse -Force }   # пустая папка сабмодуля из ZIP-архива
    if (Test-Path (Join-Path $KeyPetRoot '.git')) {
        git -C $KeyPetRoot submodule update --init third_party/lvgl
    } else {
        git -c advice.detachedHead=false clone --depth 1 --branch $LvglVersion https://github.com/lvgl/lvgl.git $lvgl
    }
    if ($LASTEXITCODE -ne 0) { Fail 'не удалось скачать LVGL' }
}

if (-not $NoWeb) {
    Write-Step 'Emscripten (emsdk) -> веб-симулятор'
    $emsdk = if ($env:EMSDK) { $env:EMSDK } else { Join-Path $HOME 'emsdk' }
    if (-not (Test-Path (Join-Path $emsdk 'emsdk.bat'))) {
        git clone --depth 1 https://github.com/emscripten-core/emsdk.git $emsdk
        if ($LASTEXITCODE -ne 0) { Fail 'не удалось скачать emsdk' }
    }
    & (Join-Path $emsdk 'emsdk.bat') install latest
    if ($LASTEXITCODE -ne 0) { Fail 'emsdk install latest' }
    & (Join-Path $emsdk 'emsdk.bat') activate latest
    if ($LASTEXITCODE -ne 0) { Fail 'emsdk activate latest' }
}

if (-not $NoFirmware) {
    Write-Step "ESP-IDF $IdfVersion -> прошивка"
    $idf = if ($env:IDF_PATH) { $env:IDF_PATH } else { Join-Path $HOME 'esp\esp-idf' }
    if (-not (Test-Path (Join-Path $idf 'install.ps1'))) {
        New-Item -ItemType Directory -Force (Split-Path $idf) | Out-Null
        git -c advice.detachedHead=false clone --depth 1 --branch $IdfVersion --recursive --shallow-submodules https://github.com/espressif/esp-idf.git $idf
        if ($LASTEXITCODE -ne 0) { Fail 'не удалось скачать ESP-IDF' }
    }
    $ErrorActionPreference = 'Continue'   # install.ps1 пишет прогресс в stderr: в PowerShell 5.1 это не ошибка
    & (Join-Path $idf 'install.ps1') esp32s3
    if ($LASTEXITCODE -ne 0) { Fail 'ESP-IDF install.ps1' }
}

Write-Step 'Готово'
Write-Host @'
Дальше (из корня проекта, в PowerShell):
  tools\sim.ps1 build      # pipsim + тесты
  tools\sim.ps1 check      # smoke-набор sim\check.py
  tools\sim.ps1 web        # собрать веб-симулятор и открыть http://localhost:8466
  firmware\flash.ps1       # прошивка на плату + лог
'@
