# Общие функции для Windows-скриптов KeyPet (их dot-source'ят tools\*.ps1 и firmware\flash.ps1).
# Ищет установленные tools\setup-windows.ps1 инструменты и добавляет их в PATH текущего процесса:
#   ESP-IDF  : $env:IDF_PATH или $HOME\esp\esp-idf (как ~/esp/esp-idf на Mac)
#   emsdk    : $env:EMSDK или $HOME\emsdk
#   LLVM-MinGW, CMake, Ninja, FFmpeg: winget (или что уже есть в PATH)

$KeyPetRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Write-Step($text) { Write-Host "==> $text" -ForegroundColor Cyan }
function Fail($text) { Write-Host "Ошибка: $text" -ForegroundColor Red; exit 1 }

function Add-ToPath($dir) {
    if ($dir -and (Test-Path $dir) -and -not (($env:PATH -split ';') -contains $dir)) { $env:PATH = "$dir;$env:PATH" }
}

# winget кладёт portable-пакеты (zip) в %LOCALAPPDATA%\Microsoft\WinGet\Packages\<id>_<hash>\..., а ярлыки - в Links
function Find-WingetBin($idPrefix, $exe) {
    $root = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    if (-not (Test-Path $root)) { return $null }
    Get-ChildItem $root -Directory -Filter "$idPrefix*" -ErrorAction SilentlyContinue | ForEach-Object {
        Get-ChildItem $_.FullName -Recurse -Filter $exe -ErrorAction SilentlyContinue | Select-Object -First 1
    } | Select-Object -First 1 | ForEach-Object { $_.DirectoryName }
}

# компилятор C для pipsim и тестов, cmake, ninja, ffmpeg
function Use-HostTools {
    Add-ToPath (Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Links')
    Add-ToPath "$env:ProgramFiles\CMake\bin"
    if (-not (Get-Command clang -ErrorAction SilentlyContinue)) { Add-ToPath (Find-WingetBin 'MartinStorsjo.LLVM-MinGW' 'clang.exe') }
    if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) { Add-ToPath (Find-WingetBin 'Ninja-build.Ninja' 'ninja.exe') }
    if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) { Add-ToPath (Find-WingetBin 'Gyan.FFmpeg' 'ffmpeg.exe') }
    foreach ($t in 'cmake', 'ninja', 'clang') {
        if (-not (Get-Command $t -ErrorAction SilentlyContinue)) { Fail "не найден $t. Запустите tools\setup-windows.ps1" }
    }
}

# Python для симулятора (Pillow): py-лаунчер или python; заглушка Microsoft Store (WindowsApps\python3) не годится
function Get-Python {
    foreach ($c in 'py', 'python') {
        $cmd = Get-Command $c -ErrorAction SilentlyContinue
        if ($cmd -and $cmd.Source -notlike '*WindowsApps*') { return $c }
    }
    Fail 'нужен Python 3 (python.org или winget install Python.Python.3.13)'
}

function Use-Emsdk {
    $emsdk = if ($env:EMSDK) { $env:EMSDK } else { Join-Path $HOME 'emsdk' }
    if (-not (Get-Command emcmake -ErrorAction SilentlyContinue)) {
        $envScript = Join-Path $emsdk 'emsdk_env.ps1'
        if (-not (Test-Path $envScript)) { Fail "нет emsdk в $emsdk. Запустите tools\setup-windows.ps1" }
        # emsdk пишет в stderr служебные строки: в Windows PowerShell 5.1 при ErrorActionPreference=Stop это исключение
        $ErrorActionPreference = 'Continue'; $env:EMSDK_QUIET = '1'
        . $envScript *> $null
    }
}

function Use-Idf {
    $idf = if ($env:IDF_PATH) { $env:IDF_PATH } else { Join-Path $HOME 'esp\esp-idf' }
    $envScript = Join-Path $idf 'export.ps1'
    if (-not (Test-Path $envScript)) { Fail "нет ESP-IDF в $idf. Запустите tools\setup-windows.ps1" }
    $ErrorActionPreference = 'Continue'   # см. Use-Emsdk: stderr скрипта окружения не ошибка
    . $envScript *> $null
    if (-not $env:IDF_PATH) { Fail 'export.ps1 из ESP-IDF не выставил IDF_PATH' }
}

# idf.py через python из окружения IDF (после Use-Idf python в PATH - это venv ESP-IDF)
function Invoke-Idf {
    & python (Join-Path $env:IDF_PATH 'tools\idf.py') @args
}
