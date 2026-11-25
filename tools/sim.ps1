# Симуляторы на Windows (аналог команд из CLAUDE.md):
#   tools\sim.ps1 build                  # cmake -S sim -B build -G Ninja && cmake --build build  (pipsim + *_test)
#   tools\sim.ps1 check                  # build + python sim\check.py (всё должно быть "ok")
#   tools\sim.ps1 run out\x.png 1.5 "0.05:H1 0.3:oqr 0.6:P"   # один сценарий -> PNG/GIF/MP4 + строка status
#   tools\sim.ps1 web                    # emcmake ... build-web -> sim\web\pip.{js,wasm}, затем serve
#   tools\sim.ps1 web-build              # только собрать веб-версию
#   tools\sim.ps1 serve [порт]           # http://localhost:8466 (sim\serve.py)
param([Parameter(Position = 0)][string]$Cmd = 'build', [Parameter(Position = 1, ValueFromRemainingArguments = $true)][string[]]$Rest)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'winenv.ps1')
Set-Location $KeyPetRoot

function Need-Lvgl {
    if (-not (Test-Path 'third_party\lvgl\lvgl.h')) { Fail 'нет LVGL в third_party\lvgl. Запустите tools\setup-windows.ps1' }
}

function Build-Sim {
    Need-Lvgl; Use-HostTools
    if (-not (Test-Path 'build\build.ninja')) {
        cmake -S sim -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
        if ($LASTEXITCODE -ne 0) { Fail 'cmake (configure) для sim' }
    }
    cmake --build build
    if ($LASTEXITCODE -ne 0) { Fail 'сборка pipsim' }
}

function Need-Ffmpeg {
    if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) { Fail 'pipsim пишет картинки через ffmpeg: winget install Gyan.FFmpeg' }
}

function Build-Web {
    Need-Lvgl; Use-HostTools; Use-Emsdk
    if (-not (Test-Path 'build-web\build.ninja')) {
        emcmake cmake -S sim -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
        if ($LASTEXITCODE -ne 0) { Fail 'emcmake cmake (configure) для веб-симулятора' }
    }
    cmake --build build-web
    if ($LASTEXITCODE -ne 0) { Fail 'сборка веб-симулятора' }
    # копия для панели (docs/sim): кнопка «Симулятор» во вкладке «Продвинутые настройки», работает с GitHub Pages
    New-Item -ItemType Directory -Force docs\sim | Out-Null
    Copy-Item sim\web\index.html, sim\web\pip.js, sim\web\pip.wasm docs\sim\ -Force
}

function Serve($port) {
    if (-not $port) { $port = 8466 }
    Write-Host "Веб-симулятор: http://localhost:$port  (Ctrl+C - остановить)"
    Start-Process "http://localhost:$port"
    & (Get-Python) sim\serve.py $port
}

switch ($Cmd) {
    'build' { Build-Sim }
    'check' {
        Build-Sim; Need-Ffmpeg
        & (Get-Python) sim\check.py build\pipsim.exe
        exit $LASTEXITCODE
    }
    'run' {
        if ($Rest.Count -lt 1) { Fail 'tools\sim.ps1 run <out.png|gif|mp4> [секунды] ["события"]' }
        Build-Sim; Need-Ffmpeg
        $out = $Rest[0]; $dir = Split-Path $out
        if ($dir) { New-Item -ItemType Directory -Force $dir | Out-Null }
        $a = @('--out', $out, '--secs', $(if ($Rest.Count -gt 1) { $Rest[1] } else { '4' }))
        if ($Rest.Count -gt 2) { $a += @('--events', $Rest[2]) }
        & .\build\pipsim.exe @a
        exit $LASTEXITCODE
    }
    'web-build' { Build-Web }
    'web' { Build-Web; Serve $Rest[0] }
    'serve' { Serve $Rest[0] }
    default { Fail "неизвестная команда '$Cmd' (build | check | run | web | web-build | serve)" }
}
