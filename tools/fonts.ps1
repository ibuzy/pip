# Шрифты тем из assets\fonts (подмножества Fontsource woff, OFL) через lv_font_conv (нужен Node.js).
# Текст: Latin-1, общая пунктуация (тире, кавычки, буллет, многоточие), евро, торговая марка, вся кириллица, знак номера.
# Значков нет: в интерфейсе только текст.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'winenv.ps1')
Set-Location $KeyPetRoot
if (-not (Get-Command node -ErrorAction SilentlyContinue)) { Fail 'нужен Node.js: winget install OpenJS.NodeJS.LTS' }
# lv_font_conv ставится в свою папку и запускается через node.exe напрямую (npx.cmd прогнал бы аргументы через cmd.exe)
$LvConvDir = Join-Path $env:LOCALAPPDATA 'keypet\lv_font_conv'
$LvConv = Join-Path $LvConvDir 'node_modules\lv_font_conv\lv_font_conv.js'
if (-not (Test-Path $LvConv)) {
    cmd /c "npm install --silent --no-audit --no-fund --prefix `"$LvConvDir`" lv_font_conv@1.5.3"
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $LvConv)) { Fail 'npm install lv_font_conv@1.5.3' }
}

$F = 'assets/fonts'
$LATIN = '0x20-0x7E,0xA0-0xFF,0x2010-0x2027,0x2030-0x203A,0x20AC,0x2122,0x2191,0x2193,0x2212'
$CYR = '0x400-0x45F,0x490-0x491,0x2116'
# казахские буквы: из cyrillic-ext, кроме Ұұ (они в cyrillic); у JetBrains Mono их нет - берём IBM Plex Mono
$KZ = '0x492-0x493,0x49A-0x49B,0x4A2-0x4A3,0x4AE-0x4AF,0x4BA-0x4BB,0x4D8-0x4D9,0x4E8-0x4E9'
$KZU = '0x492,0x49A,0x4A2,0x4AE,0x4BA,0x4D8,0x4E8'
function Kz($family, $weight, $caps) {
    $ext = if ($caps) { $KZU } else { $KZ }
    $u = if ($caps) { '0x4B0' } else { '0x4B0-0x4B1' }
    @('--font', "$F/$family-cyrillic-ext-$weight.woff", '-r', $ext, '--font', "$F/$family-cyrillic-$weight.woff", '-r', $u)
}

function Conv {
    & node $LvConv --no-compress --no-prefilter --bpp 4 --format lvgl --lv-include lvgl.h @args
    if ($LASTEXITCODE -ne 0) { Fail "lv_font_conv: $($args -join ' ')" }
}
function Text($name, $size, $family, $weight, $kzFamily = $family, $kzWeight = $weight) {
    $kz = Kz $kzFamily $kzWeight $false
    Conv '--size' $size '--font' "$F/$family-latin-$weight.woff" '-r' $LATIN '--font' "$F/$family-cyrillic-$weight.woff" '-r' $CYR `
        @kz '--lv-font-name' $name '-o' "ui/fonts/$name.c"
}
function Title($name, $size, $family, $weight, $kzFamily = $family, $kzWeight = $weight) {  # заглавные для больших заголовков
    $kz = Kz $kzFamily $kzWeight $true
    Conv '--size' $size '--font' "$F/$family-latin-$weight.woff" '-r' '0x20-0x5F,0xAB,0xBB,0xC0-0xD6,0xD8-0xDE,0x2013,0x2014,0x2026' `
        '--font' "$F/$family-cyrillic-$weight.woff" '-r' '0x401,0x406,0x410-0x42F' @kz '--lv-font-name' $name '-o' "ui/fonts/$name.c"
}
function Script($name, $size, $family, $weight) { # Latin-1, dashes, Cyrillic, no icons: a script face for mixed-case titles
    Conv '--size' $size '--font' "$F/$family-latin-$weight.woff" '-r' '0x20-0x7E,0xA0-0xFF,0x2013,0x2014,0x2026' `
        '--font' "$F/$family-cyrillic-$weight.woff" '-r' '0x400-0x45F' @(Kz $family $weight $false) '--lv-font-name' $name '-o' "ui/fonts/$name.c"
}
function Digits($name, $size, $family, $weight) { # 0-9 and ':' only: every th->digits label (timer, stopwatch, score, dB, 8, camera)
    Conv '--size' $size '--font' "$F/$family-latin-$weight.woff" '-r' '0x30-0x3A' '--lv-font-name' $name '-o' "ui/fonts/$name.c"
}

function Mini($name, $family, $weight) { # 14 px digits, %, minus: the menu's battery
    Conv '--size' 14 '--font' "$F/$family-latin-$weight.woff" '-r' '0x25,0x2D,0x2E,0x30-0x3A' '--lv-font-name' $name '-o' "ui/fonts/$name.c"
}

Text   font_instr_20 20 jetbrains-mono 400 ibm-plex-mono 500
Text   font_instr_28 28 jetbrains-mono 700 ibm-plex-mono 600
Digits font_instr_96 96 jetbrains-mono 700
Title  font_instr_40 40 jetbrains-mono 700 ibm-plex-mono 600
Text   font_diag_16 16 ibm-plex-mono 500
Text   font_diag_16b 16 ibm-plex-mono 600
Text   font_diag_20 20 ibm-plex-mono 500
Text   font_diag_20b 20 ibm-plex-mono 600
Text   font_diag_28 28 ibm-plex-mono 600
Digits font_diag_96 96 ibm-plex-mono 600
Title  font_diag_40 40 ibm-plex-mono 600
Text   font_barbie_20 20 comfortaa 700
Text   font_barbie_28 28 comfortaa 700
Digits font_barbie_96 96 pacifico 400
Script font_barbie_40 40 pacifico 400
Mini   font_instr_14 jetbrains-mono 700
Mini   font_diag_14 ibm-plex-mono 600
# Pip's own 48 px marks over his eyes ("!", "?") and the swearing grawlix: these nine glyphs of LVGL's Montserrat
# Medium, the same look as its whole built-in 48 px font (~97 KB of flash) for ~6 KB
Conv '--size' 48 '--font' "$F/montserrat-medium.ttf" '-r' '0x21,0x23-0x26,0x2A,0x3F,0x40' '--lv-font-name' 'font_pip_48' '-o' 'ui/fonts/font_pip_48.c'
Mini   font_barbie_14 comfortaa 700
Write-Host 'Шрифты обновлены. Пересоберите cmake: tools\sim.ps1 build (новые файлы - удалить build\ или cmake build), idf.py reconfigure'
