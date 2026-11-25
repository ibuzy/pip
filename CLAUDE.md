# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

KeyPet: firmware for a Waveshare ESP32-S3-Touch-AMOLED-1.75C keychain (466×466 round AMOLED, CO5300 over QSPI,
CST9217 touch, QMI8658 accelerometer, AXP2101 power, ES8311 speaker, ES7210 mics, 8 MB PSRAM, 32 MB flash).
The character is **Pip** (white eyes, moods driven by a tamagotchi model), plus a menu of apps and an iPhone link
over BLE. The owner speaks Russian: UI text, commit messages and replies are in Russian.

## Three targets, one UI

`ui/` is portable C + LVGL 9.5 and is compiled unchanged into:
- **headless recorder** `pipsim` (sim/main.c) — deterministic runs to PNG/GIF/MP4, used for every visual check;
- **web simulator** (sim/wasm.c → sim/web, Emscripten) — interactive, on a 3D model of the keychain;
- **firmware** (firmware/main, ESP-IDF 5.5) — main.c is the board glue (display port, sensors, power, audio),
  ble.c the iPhone link, debug.c the debug link.

When adding a `ui/*.c` file, add it to both `sim/CMakeLists.txt` (`set(UI ...)`) and
`firmware/main/CMakeLists.txt` (`SRCS`). Fonts in `ui/fonts/font_*_*.c` are globbed: after adding one, re-run cmake
(`cmake build`, `cmake build-web`, `idf.py reconfigure`).

## Commands

Development is on Windows only, PowerShell from the repository root (WINDOWS.md has the details):
```
tools\setup-windows.ps1          # once: CMake, Ninja, LLVM-MinGW (clang), FFmpeg, LVGL, emsdk, ESP-IDF 5.5
tools\sim.ps1 build              # pipsim + face_test/voice_test/maze_test/pet_test (build/, clang)
tools\sim.ps1 check              # the smoke suite sim/check.py (must stay all "ok")
tools\sim.ps1 run out\x.png 1.5 "0.05:H0 0.3:onoise 0.6:P"   # one scenario; events listed at the top of sim/main.c
tools\sim.ps1 web                # web sim -> sim/web/pip.{js,wasm}, served on :8466 (.claude/launch.json "keypet-sim")
local-servers.bat                # the panel on :8465 and the web sim on :8466 while its console is open (tools/serve_local.py)

firmware\flash.ps1 [secs] [-Port COM5]   # idf.py build + flash + print the log for N s (finds the COM port by VID 0x303A)
python firmware\serlog.py 20 --reset     # just the log (inside the IDF env: tools\winenv.ps1, Use-Idf)
tools\pipdebug.ps1               # debug page http://localhost:8765 (USB, or the page's Bluetooth button in Chrome)
python tools\bench.py [--secs 3] [--tasks] [--only a,b] [--out x.csv]  # frame rate on the board, mood by mood (IDF env)
python tools\face_diff.py [--ref]  # renderer refactors: every mood's frames vs a reference (build\face_ref), must stay identical
py tools\sprites.py              # ui/sprite_art.c: the little pictures round his eyes (A8 masks), after a shape or a size is added
py tools\emotions_table.py       # docs/list: every mood (ui/moods.h) - number, a pipsim picture, how to get it (Russian), search + filters
```

- Scripts share `tools/winenv.ps1` (finds the tools, `Use-Idf`, `Use-Emsdk`). The .ps1 files are UTF-8 with BOM + CRLF
  (Windows PowerShell 5.1 misreads Cyrillic otherwise); under `$ErrorActionPreference='Stop'` a native tool's stderr
  is an exception in 5.1, so env scripts and idf.py run with 'Continue' or through `cmd /c`. pipsim: clang from
  LLVM-MinGW, ffmpeg through `_popen` in binary mode. The release packing lives in `tools/pack_firmware.py`.
- The system SOCKS proxy (VPN client on 127.0.0.1:10808) breaks pip and the IDF/emsdk downloaders: the setup sets
  `NO_PROXY=*`; a blocked big archive can be dropped by hand into `~/emsdk/downloads` or `~/.espressif/dist`.
- A single check is just a pipsim run: the last stdout line is `status: mood=… reaction=… screen=… theme=…`;
  `sim/check.py` is a list of such runs with asserts. Theme in events: `H0` Стандартная (default), `H1` Ретро, `H2` Барби.
- The debug server holds the COM port: stop it before `flash.ps1`.
- Editing `firmware/sdkconfig.defaults` does nothing until `firmware/sdkconfig` is deleted (or edit both).
- Generators: `tools/fonts.ps1` (theme fonts from assets/fonts), `tools/release.ps1` (the public firmware for the panel:
  `docs/firmware/firmware.js` and `docs/firmware/version.js`, built in `firmware/build-release`).
- Nothing runs on real keychains yet: no migrations, no cleanup of removed features or old NVS keys, no reading of
  older save formats.

## The panel (what end users get)

`docs/index.html`: connect (Web Serial, VID 0x303A, or Web Bluetooth; detected by the API itself, not the browser's name: Zen has Web Serial; a browser with neither gets a notice and dead buttons, one with only Bluetooth loses the cable and flashing buttons), settings (sleep timer,
brightness, theme, features, notification privacy) and flashing with esptool-js (parts at their own addresses, so NVS -
pet, settings, bonds - survives; "Прошить начисто" also writes erased flash over NVS and the start of `storage`, `wipe` in firmware.js).
The page loads only `version.js` (version, date, `bytes`); the ~2.5 MB `firmware.js` is fetched when "Обновить" or
"Прошить начисто" is pressed (`loadFirmware`: over http a streamed fetch, "Скачиваю прошивку… x из y МБ" on the bar; from a
plain file a script tag), after the port is picked (requestPort needs the click). Update check: after connecting, a board whose `config` `ver` is older than firmware.js's version gets a prompt and the settings lock
until it is updated, so bump `PROJECT_VER` (firmware/CMakeLists.txt) with every `tools/release.ps1`. Works as a plain file (`offline-panel.bat` opens it
in Chrome/Edge) and from GitHub Pages. The site is `docs/` (GitHub Pages, branch main, folder /docs; `.nojekyll` so it is
served as it is): `/` the panel, `/sim/` the web sim (copied by `tools\sim.ps1 web-build`), `/list/` the emotions list
(made by `tools/emotions_table.py`: `index.html`, `img/<mood>.webp`, `film/<mood>.mp4` (a loop: a scene's own period `LOOP`, else found
by the likest two moments; played only while seen), `snd/*.mp3` (every feeling's sounds, pipsim `--sounds <dir>`), `names.js` = every mood's number, Russian name and
how-to for the debug page), `/debug/` the developer page (cable/Bluetooth buttons, or `tools/pipdebug.py`'s bridge,
which serves the whole `docs/` and sends `/` to `/debug/`). Every link between them is relative (the site lives under
/pip/ on github.io); the list and the debug page share `docs/pip.css` (the panel's look; the panel keeps its own inline
copy, it works as a plain file) and a header with the four pages. The debug page is all Russian: moods and reactions by
their Russian names (`names.js`, its `REACT`, `SCREENS`, `KNOB` tables: a new reaction or knob needs its words there). Board commands for settings: `power s`,
`bright %`, `theme i`, `care i` (how much looking after he needs: `pet_level`, the `levels[]` table in pet.c: need rates, steps, and his days: the morning's mood bias, yesterday's walk goal, how early needs show, and how he loses: `sore_after` games lost in a row (a win of his or 30 min clears it) make him sad / hurt or angry / furious for `sore_min` and cost fun and love, aftertaste `PET_AFTER_SORE`, reaction "sore loser"; pipsim `Y<n>`, a game's end `w1` / `w0`), `calls 0|1` (calls on the keychain at all: Приватность's "Звонки" row, phone.c `phone_set_calls`, NVS "calls"; the panel's switch in "Уведомления с телефона"), `eyes i` (eye colours, `pip_eyes`; also Настройки > Глаза on the board, main.c saves a change made there; pipsim `I<n>`), `privacy i`, `feat i 0|1`, `eco 0|1`, `abright 0|1` (automatic brightness by the battery: the set level down to 50% charge, then down to a third of it at 10%, full on the charger, never under 20%; main.c `auto_level`), `panel 1|0` (the panel's heartbeat every 2 s, main.c `settings_panel`, held 6 s, ended at once by `panel 0` on pagehide/the debug button or the Bluetooth link going: shell.c `shell_setup` lays "РЕЖИМ НАСТРОЙКИ" over everything and drops touches, keys and motion; `shell_where` `setup`, pipsim `X1`/`X0`; the flash's "Обновление" takes over), `time <unix> [tz min]` (the computer's clock: the panel sends it on the first `@C` of every link, again whenever `@C` `clk` is 0 or `@S` `t` is off by >30 s (a reset mid-link) and reconnects by itself after flashing, `reconnectAfterFlash`, so the date is real with no iPhone; main.c `settings_time`, the zone kept in NVS "tz", the iPhone's CTS too), `config` (answers `@C {json}`; `clk` 1 once the real clock is set); the hero's "Возраст" reads `@S` `age` (debug.c: the day of his life, 0 dormant, -1 dead), `menu …` (the menu's layout, below); all persisted by main.c in NVS.
Settings on the panel are staged (`set` -> `staged`, keyed by what they set): nothing reaches the board until the
"Применить изменения" bar (fixed at the bottom, the toast's pill, `applyBar`) sends them all at once with the menu's layout;
"Отменить" puts back what the board has (`base`, from its `@C`, or `DEFAULT_CFG`). A change set back as the board has it
drops out, and with nothing left the bar goes down. Developer tools (dev mode, the battery log…) still act at once (`setNow`).
The menu editor (Настройки > Меню брелока, folded until opened): rows dragged by a grip (or arrow keys), apps moved between blocks or onto the top
list, renamed in place (16 characters, glyphs every theme's menu font has); under it "Отменить все изменения" (`menuReset`: the standard menu back in the editor, staged as any edit; dead while the editor shows the standard one), the bar applies or cancels it with the rest. Board side: `menu new`,
`menu put <top|block> ids…` (lines ≤ ~110 bytes, the BLE line is 160), `menu name <id> <text>`, `menu apply` (shell.c
`shell_menu_set`, main.c keeps the text in NVS "menu"), `menu reset`; `menu` answers `@M [[id,default,name,[apps…]?]…]`.
A block is an app with no `.open`; an empty one is not listed on the board. An app added to `apps[]` goes into the panel's `MENU_DEFAULT` too. pipsim: `g<layout>` (';' a newline, '_' a space).
The panel's "Документация" tab (to the right of the advanced one): the user guide, short and plain (no technical detail),
drawn icon tiles (`.tiles`, inline SVG) and CSS theme cards rather than screenshots; only a few of Pip's faces from
`docs/guide/*.webp` (made by `py tools/guide_shots.py <pipsim> [names…]`). Keep it in step when mechanics change.
"Обновить" refuses (a toast) when the linked board already runs firmware.js's version.
Link and flashing errors are explained in plain Russian (`fail`: a short line in the pill, what to do in `needlink`; `usbFail`, `bleFail`, `flashFail` map the browser's and esptool's English), never the raw message alone. The board's log lines from other tasks can land inside a status line (a touch or a key logs at once): `onLine` wraps the handler (`lineIn`) in try/catch, a garbled line is dropped and the reading loop goes on (an exception there once ended it: "Пип перестал отвечать" after any press). The loading screen (`#loader`, also in the web sim) fades by one Web Animation and is then only hidden, never removed: a CSS transition plus `remove()` flashed it back after F5. Under "РЕЖИМ НАСТРОЙКИ" Pip is not drawn and says nothing (`pip_covered`: `say` is silent, the birthday song waits until the panel lets go). Brightness after a clean flash: 60%; the torch (Фонарик) lights the panel at 100% whatever it is (main.c `torch_on`) (main.c `bright`, the panel's `DEFAULT_CFG`).
The panel's "Продвинутые настройки" tab: the simulator (`docs/sim`, copied by `tools\sim.ps1 web-build`; from a plain file
it opens localhost:8466), "Таблица эмоций" (`openList`: `list/`), debug, dev mode, the battery log. Every control
that talks to the board stays locked until a Pip is linked (there is no unlocking it any more). Linking is a stage of its
own (`linkStart` -> `linkReady`, body class `connecting`, the `#linking` loader in the middle): from the button until the
board's `@C` and a `@S` are in (or 2.5 s after `@C`) the panel stays locked; no answer in 12 s: "Брелок не отвечает". The flash's own links (before it, and after it for the clock: `openPort(p, word)`) are quiet: no loader, the pill says "Прошивка…"/"Перезапуск…", and a flash's success is told at once (a toast and "Готово!") while the link back goes on behind it.
The panel's "Лицензии" tab (the last one, open even when the board is outdated; "Шрифты" and "Библиотеки" fold like the menu editor: every `.card-h.fold` opens what its `aria-controls` names): the project (MIT, made from
NovikovVisuals/Pip-KeyPet, a link to the licence, no text), the fonts (all SIL OFL 1.1) and the libraries that end up in the
firmware, the panel or the simulator; a font or library added or dropped goes into it too.
Dark screen = sleeping chip (always, not only eco): main.c releases its PM locks, the chip drops to 80 MHz and
light-sleeps between wakeups; Bluetooth uses modem sleep on the main XTAL. Dark, little polls (~28 wakes a second, was
~170): LVGL's task runs every `DARK_POLL_MS` (500) with the touch read timer paused (`touch_gate`): the CST9217's INT
(GPIO 11, `touch_isr`, a level wake-up that disables itself) wakes it and resumes polling for a second, so a touch lights
the screen as before; the bottom key (GPIO 0, `boot_isr`, armed for its next level each round) wakes the sensors' task;
that task reads the accelerometer, the top key (the AXP2101 latches it) and the battery every `DARK_SENS_MS` (40: 25 a
second, against 50 lit), with the gyro off (`qmi_dark`: CTRL7 0x01, CTRL2 0x17, 62.5 Hz) - steps, a lift and a shake all
seen. Do not use the QMI8658's FIFO: tried (read twice a second, ~9 wakes), on this part it handed samples over a word out
of step now and then (face up, gravity read on x: Pip "turned on his side" on the table and lit by himself, every few
seconds), its count is in 16-bit words where the datasheet says bytes, and a burst's last byte came back wrong (rounds
dropped for seconds: he slept through being picked up). pip.c keeps the gravity it compares a lift against (`pw.lp`)
from samples of 0.6..1.4 g only (a knock or a bad sample cannot turn him over; a lifting hand can). Measured lying still:
minutes dark, no wake; picked up, lit at once. (Switched, the sensor puts out saturated 4 g samples while it restarts -
seen as a lift, the screen lit again at once and a step counted: `qmi_dark` stops it, sets it and waits `QMI_SETTLE_MS`,
and three readings after it are dropped); every light-up logs its pip.c line (`lit by pip.c:N`, `pip_lit_why`); the panel's
task waits 2 s (pm_lit aborts the wait when the screen is wanted); the audio task sleeps until `voice_on_say`; the mic
task checks twice a second while shut; the debug task on battery (`board_usb` false) does not poll USB and makes no
status unless the Bluetooth page listens. Lit: the full CPU clock lock (`lit_cpu`) is held only `BOOST_MS` after the
screen lights or a key (`cpu_update`, LVGL's task alone; `FACE_HOLDS_CPU` 0: on his face too - bench showed no fps
difference); otherwise ESP-IDF's per-core rtos locks give 240 MHz to what runs and 80 MHz to idle (CONFIG_PM_PROFILING
measured the menu 61% of its time at 80 MHz, his face 20%, the clock 51%, all 0% before). Off his face LVGL waits at least
8 ms (it woke ~400 times a second in the menu). The speaker (ES8311 and its amplifier) closes after `SPK_IDLE_MS` (4 s)
of silence lit too when the mic is shut (off his face, or he sleeps); a finger or a key opens it ahead (`spk_warm`), a
sound otherwise starts ~0.1 s late (64 ms of muted silence against the thump). `perf` `wk`: wakes a second of LVGL, the
sensors, the mic, the audio, touch interrupts, touch readings with a finger down. The phone's link asks for 30..60 ms, latency 4, 5 s once it
is set up (ble.c `link_pace`; an Android phone settled on 60 ms, latency 4, logged on `BLE_GAP_EVENT_CONN_UPDATE`), quick
again while the panel rides that link. `perf` `tint`: the touch INT's level (1 idle) and whether polling is paused.
Light sleep isolates pins unless listed in `pm_keep_pins()`; with a USB host on the debug port it never light-sleeps.
An open I2S holds the chip awake too: `pip_mic_wanted()` is false whenever the screen is dark (timer or face down), so the
mic closes and the speaker after it (an open mic all night emptied a full battery in ~4.5 h). It is also false off his
face (menu, apps, games, the setup screen), asleep, dormant or dead; `shell_mic()` says what the hosts open: 2 all
(`mic_listen(true)`: breath, bangs, the beat and tones), 1 the level alone for the noise meter (`mic_listen(false)`: no
beat bands, no spectrum, no breath; ~1% of a core against ~3%), 0 closed (main.c `mic_task`, pipsim `fake_mic`, the web
sim's `sim_mic_feed`). The meter says "нет микрофона" only after 1.5 s without sound (the mic opens after it). Codecs switched on or
off thump through the speaker: main.c `spk_switch` opens the ES8311 muted with 64 ms of silence and closes it muted,
`mic_switch` mutes a running speaker around the mic's open/close (`spk_mx` between the two tasks); after a sound the
stream runs on with silence for `SOUND_TAIL_MS` (a sound started on a stopped I2S lands in stale DMA buffers: the
menu wheel's quick clicks came out chopped). On USB power main.c holds `usb_hold` (APB at max), as the always-open mic
used to: without it a debug command on a dark screen wedged the USB link until the cable was replugged.
The battery log rows carry the real clock (`batrow_t.t`, 0 unset; `@B seq up mV % flags lit t`); the panel's "Лог
батареи" draws them as bars (`drawBat`: buckets of 5 min..6 h, ≤48 bars; a fixed example `BAT_DEMO` under a plain note: "Подключи
Пипа…" with no Pip, "Недостаточно данных…" under 3 bars), fetched when the advanced tab opens. Under `EMPTY_MV` (3.3 V) for
`EMPTY_S` off USB main.c saves the pet and switches the AXP2101 off (`AXP_COMMON` bit 0; `VOFF_MV` the PMU's own backstop).
The care screen's meters are one `th_cells` object (`meter_grid`), not an object a cell: 128 small LVGL objects ate the
internal RAM and the Bluetooth controller's `esp_timer_create` on waking from modem sleep aborted.
`pm` dumps who holds the chip awake. Energy saving (`pip_eco`): ≤20 fps, no tilt in the gaze, no breath/bang listening (main.c closes the mic when `pip_mic_wanted()`
is false), BLE off the air while the screen sleeps (`ble_quiet`). `bat [clear]` dumps the battery log (`@B`, a row a minute
in the `storage` partition at 64 KB) — the panel's dev tab saves it as CSV, for measuring what a charge lasts.

## Architecture worth knowing before editing

**Pip** (`ui/pip.c`): moods are a table of eye shapes + behaviour (`moods[]`); every frame springs chase the
target shape, `face.c` rasterizes the two eyes. Base mood comes from `pet.c` (`pet_mood_why`, first match wins:
asleep → a rare card (`pet_rare_cards[]`: stoned, pinkeye, allergic, frozen, overheated, smitten, hypnotized, hyper, zombie;
rolled on the hour 9–21, one a day at most, ~1 in 10 days by `rare_chance`, likelier and which by `pet_rare_weights`: the
season (`pet_month`), the hour and his day — grubby → pinkeye, spring walks → allergic, winter mornings → frozen, summer noon
walks → overheated, cuddles → smitten, shakes → hypnotized, biscuits → hyper, broken sleep → zombie, evening on the charger →
stoned; `*_today` counters in pet_t; why `1.<card index>`; built from springs/tints/the renderer's features only, plus
sneezes, a few snowflakes or a sweat drop) → the body (battery) → an aftertaste of an event (won, lost,
fright, notification flood, sulking…) → a need under its line (near-equal needs take turns, a long spell shows the
urge's other face) → tired night → a situation (just woke, walking, charging) → the day's palette by valence, calmer
in the evening, weighted, never the same twice running). `pet_t.born` gives his age (`pet_age_days`, day 1 on the first; shown in the care app).
Clean (`pet_t.clean`, the 6th need, `PET_NEEDS`; Чистота in the care app, whose first meter is Настрой: `pet_mood_level`, the valence as 0..1 through the palette lines): dust settles by the hour (a quarter asleep), steps,
crumbs, fingers and drops add to it (`levels[]` `dirt`/`dirt_step`: ~1 wash a day on Неприхотливый/Уравновешенный, ~2 on
Требовательный); urges disgusted/gloomy, a valence penalty only when grubby, games refused under `play_dirty` ("Пип грязный").
On the glass (`ui/grime.c`): no dust above `dust_line`, then specks, hairs, smudges by `pet_dust()` — small fixed objects
shown/hidden, never scaled. On Pip the bottom key tapped and at once (within `WASH_AFTER_MS`) pressed and held washes him
(`pip_wash`, shell `wash_hold`; not the top key: held for seconds that one is the AXP2101's power off): a sponge, foam and
bubbles, `wash_rate` a second; `pet_washed` — glad and fresh (`PET_AFTER_WASHED`) or, if he was clean (`wash_clean`),
indignant → angry; scrubbed while clean past `wash_push` s he boils over and flings the sponge (`grime_fling`), and washed
clean twice within `wash_refuse_s` (10 min) he refuses the next (`pet_refuses_wash`, as with food); "clean" is `wash_clean` (0.9, really clean; at the dust line 0.55 - no dust on the glass - nearly every bath was "not
dirty", skeptical at once, as dust settles ~a wash a day). A dirtier Pip enjoys the sponge ("scrub", content), and come
clean under it is glad of it `WASH_GLAD_S` (1.5 s, "scrub: clean") before scrubbing on starts the skeptical..furious
ladder (`s.washed_at`). pipsim: `c<v>` sets clean, `B 0.2:B3` washes.
Voice (`ui/voice.c`): a little robot's palette — sine voices that glide and warble, a damped mouth, a ~3 kHz warmth filter,
eased envelopes; no saw or square buzz. Every sound is brought to one loudness by `level[]` (voice_test measures it
through a small-speaker filter and fails past x1.3, and past a 0.7 peak: the soft clip heard as a rasp). The render loop has
no libm per sample (`sin01`, glides as a ratio a sample, set up in `syl_start`). Petting giggles once as it starts
(`VOICE_GIGGLE`), no purr. `voice_started()` counts sounds started (pipsim's status `voices=`: a crackle of restarts shows).
His talk is `VOICE_BABBLE`, made to order by `voice_babble(kind, valence, arousal)` (voice.c `babble`): `BABBLE_SAY`, `ASK`
(rising end), `EXCLAIM`, `COMPLAIN` (low, rough), `SOB` (wavering, a catch), `WOOZY` (the line swims), `GIGGLY`, `INTERJ` (one
"o!"/"m?"/"eh"); valence sets pitch, line and brightness, arousal pace and syllables; `babble_level[]` levels the kinds
(voice_test runs every kind over valence x arousal against the other sounds, x1.35, and the 0.7 peak). `voice_tone(semis,
tempo)` colours the next of his own sounds (not the interface's, the notes, the song or the drums; voice_test checks the
extremes; a burp keeps its pitch, only its pace changes: raised it rasped): pip.c `tone_now` from affect, energy, the feeling's arousal and the night.
A burp (pip.c `pip_burp`, `burp_face`): swells, "BRRUUAARP" (`VOICE_BURP`: glottal pulses, `wave` 1, uneven by `jitter`/
`shimmer`, high-passed twice and rung through two vowel bands, `formant`/`formant2`/`damp`, so a small speaker plays it; a
breathy gulp first, `gurgle` drops and halves pulses for the wet rattle; never the same twice: a short "urp", the usual
one, a long gurgling one or two in a row, ending in a breath or a soft "p", each kind levelled by `amp`) with three
puffs, then shy and blushing; always ~1.4 s after a stuffing biscuit, a quarter of the time after an ordinary one; grubby
(clean under `dust_line`) more: after a biscuit up to `burp_dirty` (0.8) when filthy, and on his own every `burp_min`..3x
minutes, each a roll won by how grubby (pip.c `grub`, `burp_step`; never when clean); key `x`.
Ten everyday faces, seen often (moods.h after PIP_WOOZY: impatient, wistful, blissful, adoring, sly, sheepish, determined,
eureka, eyeroll, flustered; each its own sound, VOICE_TAPTAP..VOICE_KYA): about a third of the day's palette slots (pet.c
`daily[]`, knob `everyday`; 0 the classic palettes), his own moments (M_SCHEME, M_DREAM, M_BLISS, M_ADORE, M_WAIT,
M_EYEROLL; "ponders" now and then ends in "aha" = eureka), the chase's crouch and the drums (determined), the burp's and a
toot's end and a flung sponge's "sorry" (sheepish), an irked poke (eyeroll: pip.c update sweeps the gaze up and over, by
`s.eyeroll_t`). Built from springs only, nothing new per pixel; measured on the board ≥ ~20 fps like love (a glint on a
big pupil and a blush are what cost: adoring keeps a .7 glint on a middling pupil and no blush, eureka none). Hiccups
(`hic`, `pip_hiccups`, `VOICE_HIC`: 4..8 "hic!"s, the eyes popping wide `HIC_POP`; after a biscuit, a long laugh or out of
nowhere; a fright cures them, "hiccups gone") and a toot (`pip_toot`, `toot_face`, `VOICE_TOOT`: the burp's puffs from
under him, then sheepish/sly/cheeky); own dice, and only with `vary` > 0. pipsim/debug keys `h`, `t`.
Hands (pip.c, the section before `update`): main.c `touch_read_all` replaces the CST9217 component's read (it took one
point) with the whole report, and `touch_read` hands every finger to `pip_fingers(n, x, y, palm)` before LVGL hears of
the first (pipsim `h<x>,<y>,<x>,<y>`, `hP` a palm, `h0`; the web sim's "Жесты" card, `sim_fingers`). Measured (debug
`touchraw 1`: each report as `@R` hex): this panel reports two points at most - a third finger never shows - and a palm
sets bit 7 of a point's last byte (`touch_rep[4]`, `[11]`; `touch_palm`), never a finger; the flag lingers one report
after lifting (status 0), so it counts only with a finger down. Two fingers on his two eyes drawn apart `MT_SPREAD_PX`:
"stretched" (the eyes follow: `sc.spread`), let go "let go" (they twang back); drawn together: "pinched" (squeezed to his
nose, crossed), let go "popped"; two fingers elsewhere held `MT_PAIR_MS`: "two fingers" (overwhelmed, the eyes darting
from one to the other), lifted "fingers gone" (relieved); a palm "palm" (squished), lifted "palm off" (pops back, pays as
peekaboo). While more than one finger or a palm is down (`mt.seen`) touch_cb takes no poke, stroke, spiral or swipe.
`touchtrace` ends each `@F` with `| fingers most gesture spread on-eyes rawNN`. (A knock on the case was tried and
dropped, Nov 2026: the IMU at 50 readings a second with the mic's click told it apart in the log, but not well enough
in the hand.)
The newer faces (moods.h after PIP_FLUSTERED: squished, stretched, overwhelmed, pinched, beaming, cuddly, eager, yummy,
triumphant, deflated, sparkling, groggy) are little scenes: `scene_face` changes the table entry over the face's first
seconds (`sc.t`) and three springs move the eyes (`sc.spread` apart, `sc.sink` down, `sc.scale`), `sc.spin` turns the
head right round (triumphant's victory spin), `scene_gaze` steers the look; particles are round dots from the confetti's
layer (`dot`) or a sweat drop - no new per-pixel work. Each its own sound (VOICE_MMPH..VOICE_GROGGY) and two more.
Besides the gestures they come as the other face of an answer (`vary_to`, never at vary 0): stroked (beaming, cuddly in
the `pet` list), caught (beaming), the biscuit coming (eager), eating (yummy), a game he won (triumphant) or lost
(deflated: a game's end comes on its own screen, so pip_game keeps it in `game_kept` and `pip_away(false)` plays it
afresh if you are back on his face within `GAME_KEPT_S`, 10 s; `scene_face` runs no scene while he is away), a bath that left him clean (sparkling), woken (groggy). Every mood has two sounds or more in `mood_sound`
(pipsim `--list` fails otherwise; neutral alone has none).
The little scenes with pictures (moods.h after PIP_GROGGY, 22 kind faces: starstruck, singing, kiss, flirty, rainbow, peeking, zen,
sweetdream, melting, applause, cool, puppy, butterfly, bubbles, robot, kitty, silly, snicker, campfire, stargazing, zapped, sipping):
pip.c `show_face` (state `show`; each loops on its own clock, like `scene_face`'s; `show.out` lets the eyes leave the glass,
`show.gaze` says where he looks), `show_after` (what is stuck to the eyes: starstruck's stars, smaller as the lids close). The
pictures are `ui/sprite.c`: hearts, stars, notes, ears, a fire... A8 masks in flash (`ui/sprite_art.c`, ~475 KB, made by
`py tools/sprites.py`: add a shape or a size there, in its `ART`; not on the board - a big one is a hundred thousand hypotf; the
square sizes 0..4, or `SP_OWN`, a picture's own w x h, `sprite_own`), 14
held (`sprite_pin`/`sprite_bar`, anew every frame) and 14 flying (`sprite_go`), one object draws them all in a few joined
boxes. `sprite_snap` with the frame handed to the renderer, `sprite_paint` at its swap: a pinned picture stays with its eye.
The list is one heap block over 2 KB (PSRAM). Two sounds of their own each (VOICE_WOW..VOICE_FILLUP, `mood_voice` +
`mood_sound` o[1]): no hiss in them but a breath's (the owner found noise bursts harsh: psst, claps, crackle were redone as
soft tones). They come: his own moments (M_STARS..M_SKY in `life_moment`, weights by valence, needs, hour, month, the
charger), as answers (`answer_t` lists: peekaboo, cheeks, chin, side poke, poke, head pat), `vary_to` (a dance's end, the
walk's goal, a bath, dodged, teased, a boop, games), frozen by a breath and left in peace (`s.warm_in`: the campfire), a long stroke (`s.long_mood`: touched, melts, kisses), a finger
resting past `REST_S` + 2 s (`s.rest_mood`: zen), asleep well looked after (`s.sweet`), and the cable (pip_battery: "charging",
"charging: zap" lively, "charging: sip" under 30% or tired). Never at vary 0. Reactions have names of their own ("sings",
"hides", "purrs"...): an old name would bring its old sound (`said[]` in react). pipsim's profile prints "sprites: N px a frame". Measured on the board
(Nov 2026, 5 s each): 20.4 (melting) .. 32 fps, zen 44, against neutral 25.5, love 19, amazed 17, tumbling 16 in the same run.
38 more (moods.h after PIP_SIPPING, Nov 2026: 25 kind, xd .. sprout, then 13 sad or cross, raincloud .. sniffles), the same way, folded into
`show_face`/`show_after` (a new one's `sc.spin` is reset every frame: it spins only when it says so). No symbols for eyes (the owner
asked for his own eyes in all of them): knockout is heavy uneven lids, pupils drifting round, cross-eyed, stars. xd is his own eyes squeezed to thick crescents (no "> <": the owner disliked
them), hearteyes keeps his eyes and beats a heart over them (not hearts for eyes, nor on them). spooked and sulking hide
the eyes (dark tint, `show.out`). As reactions each has its name and one loop's length in `show_as` (`show_react`). Occasions
(never at vary 0): tickled (`s.tickle_mood`: laughing or xd, past 4 s rofl), a stroke begun (bouncy), long (hearteyes, blooming), a
head pat while shy and a cheek tap (paws), fed (icecream, bouncy), washed (angel, umbrella, sprout), a burp or a toot (angel), dodged
(imp, ninja), a flick (ninja: rare, once in `NINJA_GAP_S` 3 min at most), a light jiggle (juggler), peekaboo and a bang a bold one is not afraid of (popeyed), a bang at night
(spooked), the walk's goal (medal, fireworks), games (medal, king; your record: fireworks), music (`dn.disco`), fake sleep found
(magician), "aha" (lightbulb), lifted with the screen dozing (`picked_up`: hello after 5 min alone, balloon by day; out walking with
half the day's steps, `walked_lately`, also out of a face-down pocket sleep: strongman every time, `STRONG_GAP_S` apart), woken 6..11 fed
and loved (`good_morning`, sunrise, once a day), back after a while (hello, heartbroken), the meet's "likes you" (hello), dropped or
thrown when low (sobbing), thrown (heartbroken), dropped or shaken hard (knockout; kept up past 2x `SHAKE_LONG_S`, `s.shake_ko`), fed up
(fuming, rage -> burning), the cable fiddled (fuming), teased and jealous (sulking), a chill caught wet (`s.sniffle_in`: sniffles), and his
own moments M_BLOOM..M_WILT (season, hour, walking, battery under 10%, food, love, the grudge) plus tumbleweed for M_SIGH, imp for
M_SCHEME, sniffles for M_ILL. Measured on the board (Nov 2026): 23..60 fps, the median ~35, neutral 25 in the same run.
The cable pulled out (pip_battery, after the overcharge and "unplugged right" checks; not within 20 s of plugging, not at vary 0) by the
charge: >= 80% PIP_CHARGED (green pulses of charge through his eyes, a stretch), 30..79% PIP_CUTOFF (his eyes dim and flicker, he leans
after the cable), under 30% PIP_EMPTY (his eyes go out like an old TV and flicker back on, grey); "unplugged right" now and then shows
PIP_CHARGED too. Their sounds are short (<= 0.5 s): VOICE_POPFULL..VOICE_FEEBLE. pipsim: `J<pct>+` then `J<pct>`.
New Year (moods.h after PIP_QUEASY, Nov 2026; show_face, sprites only): PIP_NEWTREE (a fir `SP_FIR` below him, 8 running lights,
snow `SP_FLAKE`; the star lights at 1.7 s: wow, then beaming; VOICE_JINGLE, VOICE_OHTREE), PIP_WREATH (a wreath `SP_WREATH` + bow
`SP_BOW` comes down on a ribbon once, swinging, lights on at 2 s; VOICE_HANGUP, VOICE_AAH). The bells (JINGLE, HANGUP) keep pitch and
pace (voice.c `fixed`): the scenes are timed to them. 1 January by the real clock (pip.c `ny_step`, `ny_day`; not at vary 0): the
first time his face is up that day the tree or the wreath (reaction "new year tree"/"wreath"), then every `NY_GAP_MIN` (15)..2x min a
burst: a snowfall (`snow_start`, `SNOW_S` 9 s, three or four small flakes at once over any face, melting above his eyes (`SNOW_LIFE`): over the whole height they cost ~5 fps on the board, stopped off his face or dark) or, `NY_SCENE` of the
time, a scene. 2..10 January: only the snowfall, once a day, twice on `NY_LATE_TWO` (0.3) of the days, at times drawn 9..21 h (`ny_late`). The greeted day and the snowfalls still
due are kept beside the pet (`pip_ny_state`, main.c NVS `ny`, saved on `pip_ny_changed`): a pet_t field would change the save's size. Key `s` (debug `key s`, pipsim `ks`) a snowfall; pipsim reaches 1 January with
`f5,0 T1992` (his time stopped, so he does not starve on the way). PIP_HYPNOTIZED's pupils were made smaller (.5 -> .42, the eyes
128x140 -> 120x132): its per-pixel spiral was the slowest face on the board (14.4 fps).
PIP_QUEASY (fed when full: half the time instead of nauseous "too full"; green, swaying, puffed cheeks, a retch, woozy): VOICE_RETCH
and the burp; it cancels a burp an earlier biscuit had due (`s.burp_due`), the scene has its own.
Moods are kept in bytes (pet_t `after_mood`, `rare_mood`, `cur`, `prev`: uint8_t, `PET_NO_MOOD` 255 = the old -1): 254 at most.
A babble's sound code is `VB_BASE` (1000) + kind, past every VOICE_*.
Zones of touch (pip.c `zone_at`, knob `zones`; 0 = a plain poke and stroke there): cheeks under the eyes (tap: flustered,
"cheek"; stroke: "cheek rub"), the chin under both (tap "chin"; stroke "chin rub": blissful, head up), the rim beside the
eyes (tap "side poke": ticklish, he jumps away), a finger held still on an eye `EYE_HOLD_S` ("finger in eye": that eye
shut, the head away, angry after 3 s). On chin and cheeks a stroke is never a tickle. check.py runs with everyday 0 and
zones 0 unless a check sets them.
How much he talks (`pip_chat`, the panel's "Болтливость" Тихоня/Обычный/Болтун, `chat i`, NVS "chat", `@C` `chat`, pipsim
`j<n>`, status `chat=`): `chat_mul` scales his chats (M_CHAT, twice what it was), the gap between his own moments, tales
after events and a hello when you come back after 90 s. pipsim renders the voice past its 2-minute WAV too (it stayed
"busy" for ever before: long runs went silent).
Ten feelings come from what you do, each with its own sound (moods.h, the end of `moods[]`; inputs in pip.c `swing_step`,
`update` and `touch_cb`, constants at the top): swung side to side (excited, `VOICE_WHEE`), a finger resting still (dreamy,
`HUM`), a slow finger to follow (focused, `THINK`), one small patch rubbed fast (tickled: a real laugh - see below), stroked
`TOUCHED_S` (touched, `AWW`), strokes broken off three times (teased: pouty, `POUT`), face down under a second and back
(`IMU_PEEK`: face down 0.3 s..`FACE_DOWN_S` (2 s) and back; playful, `PEEK`), upright after `RELIEF_S` upside down or a head spin (relieved, `PHEW`), the day's walk goal
(`PET_STEP` -> proud, aftertaste `PET_AFTER_PROUD`, `TADA`), fed under `hungry_line` (grateful, `THANKS`). Dropped: shocked, then hurt (`PET_DROPPED`, `after_dropped`);
thrown: angry, worse each time by `annoy`, then hurt longer and it costs love (`PET_THROWN`, `after_thrown`); a stroke makes
either up. The web sim has buttons for both, for upside down, and a real microphone (`sim_mic_feed`).
Being alive (pip.c, the "life" section before `update`; its own dice `life_rand`, so no one else's random numbers shift):
answers vary - a plain poke, a breath, a shake, a swing, peekaboo, the first face of a stroke or a head pat are each
`answer()`/`pick()` from weighted `answer_t` lists (weights by valence, needs, trust, annoy; the last answer given less
likely); `pet_tune.vary` 0 gives the first (classic) one every time. Left untouched `LIFE_IDLE_S`, every
`life_gap`..x1.6 s he does something of his own (`life_moment`: looks round, yawns, hums, sighs, giggles, smug, ponders,
a shy look, a hop, pretends to sleep (`fake_on`: a finger finds him out, "boo"; else he peeks and laughs), a nervous
glance, stares at something, a wink, zones out, shivers when chilled, coughs/sneezes when ill, misses you).
New gestures: a tap right between the eyes is a boop (`boop`, `boop_face`: cross-eyed at his nose, then a snort
`VOICE_SNORT`, or a giggle, or a sneeze; checked before an eye's tap slack); a finger drawn slowly down over the eyes
(`life_swipe` -> `eyes_down`) puts a sleepy one to sleep (`PET_LULLED`: asleep, not bedded) or he pops them open; drawn
up (`eyes_up`) wakes a sleeper, grumbling; a stroke above `HEAD_Y` is a head pat (he leans into it: `lean_roll`,
`lean_pitch`); a quick flick (< 0.3 s) is followed by his eyes; now and then (playful, sure of you) he ducks a finger
coming at an eye (`dodged`) and laughs. A spiral drawn on him (`spiral_feed` in touch_cb, state `sp`: the finger sampled
every `SPIRAL_STEP` px, the turn between segments one `atan2f` of cross/dot, each half turn's loop size its arc over its
angle; `SPIRAL_HALVES` (3) half turns the same way, loops growing or shrinking to `SPIRAL_GROW` x, `SPIRAL_SLACK` for a
hand; or round and round in loops of one size for `CIRCLE_HALVES` (two turns), loops of `CIRCLE_R` px or more (smaller is
tickling); turns the other way adding up past `SPIRAL_BACK` reset it (a wobble does not), so strokes, tickling and the
chase never add up) takes the touch over (no stroke, no chase; the finger already `SPIRAL_LEAD` round one way starts no
stroke, so no "pet" flashes first): `spiral_answer` picks the face while it goes on, mostly `PIP_WHIRLED` (its own mood: the
pupils ride round where the finger is on its loop, `spiral_gaze` from the heading; reaction "spiral"), or spellbound
(hypnotized, likelier drawn inwards), drowsy (inwards, tired), tickles, what, stop (cross), uneasy (timid); he sways round
with each half turn. Let go after `on_halves` >= 2 (`spiral_release`): mostly `PIP_WOOZY` ("spun": the gaze goes on round
by itself at the finger's speed, slowing, `wz_phase`/`wz_w`, eyes out of step through the dizzy spring), or spellbound,
dizzy, again, phew, enough. Debug `@S` `sp`: the last touch's samples, start-overs, most half turns, sharpest turn, on,
loop sizes. Rocked gently (`rock_feel`: half swings 0.7..3 s, for a tired one (energy < `ROCK_TIRED`) from 0.4: a hand's own pace is ~0.5; no jolts; lulled he sleeps
at least pet.c `LULL_S` (20 min); faster is the
swing's "whee") he hums `VOICE_LULLABY` and a tired one falls asleep in ~`rock_s`; rested, he gets bored of it. After a
bath he is wet (`s.wet`, dries in `dry_s`, or blown dry in `DRY_BLOW_S` 1.5 s of breath, pauses to breathe in counting half;
no drops fall while he is blown on): drops (`drip`), a wet-dog shake now and then; blown on while wet it dries
him (content, snorts, a little fun). Blown on past `cold_s` (x1.6 wet) he is cold (frozen, `VOICE_BRR`, `s.chill` wears
off in a minute, faster stroked); cold while wet adds to `pet_ill`. Filthy (clean < 0.2) he falls ill over `ill_h`
hours (pet.c `pet_ill`, not saved; body branch why 2.5: sick / nauseous by turns; more sneezes), clean he recovers in a
few hours. pipsim `y<v>` sets energy, `s<secs>` the sleep timer (s0 never), `--trace` (first) prints every change of
face with its why; check.py runs everything at vary 0 / life_gap 0 / calm 0 unless a check sets them.
Feelings with weight (the affect section, before life): each mood has a valence (`mood_val`, from a name table in
`mood_vals`); `s.affect` follows the face shown (tau 3 s during a reaction, 15 s otherwise). A base mood with a reason
(the tree's asleep, rare, body, aftertaste or situation branch: `s.mood_reason`) shows at once; one that only drifted
(palette, urge, night) farther than `AFFECT_NEAR` from the affect is reached by steps (`calm_base`: neutral, content,
dull, bored... each held up to 10 s), so glum never turns glad for no reason, nor back (~30 s across; knob `calm`).
His own moments (`life_moment`, every `life_gap` 55..x1.6 s, only once his face has held 12 s) and the varied answers
are weighed by `affect_fit` (near how he feels). He talks too (`talk`, moment M_CHAT; a bob a syllable; how much by `chattiness()`: 1 + 0.9 affect + 0.6 (energy-0.5) + 0.6 (trust-0.5) + a little annoy, x0.6 at night, 0.3..2.2 - it weights M_CHAT (2.4x) and shortens the gap to his next moment, and over 1.2 he may say a second sentence, `s.chat_again`): `VOICE_CHAT`
glad, `VOICE_MUTTER` cross, `VOICE_MUMBLE` glum, `VOICE_TEASE` "be-be-be" cheeky (also a cheeky answer to a poke), mixed
with babbles (say, ask, giggly, complain); `talk` returns false when he keeps it to himself (no quiet for it). Never
the same kind of sentence twice running (`last_talk`, `talk_other`; tales alternate say/ask): the tease "be-be-be" is
one fixed phrase.
Tickled (measured on the owner's finger, debug `touchtrace 1`: `@F ms down x y speed spread petting tickle_s zone
reaction`; a tickle rubs a patch of 19 px median, 33 at most, at 425..960 px/s): petting within `TICKLE_SPREAD` (34) for
`TICKLE_S` (0.45 s) is a tickle wherever it is - before the chin, cheek and head-pat zones (they took it: "cheek rub",
blissful, instead of a laugh); stroked wider it is still those. `laugh_step`/`laugh_face`: "^ ^", a bounce and a toss of
the head on every "ha", `VOICE_LAUGH` again every 1.1..1.5 s, helpless by `LAUGH_FULL_S` (shaking, blushing, tears flying
from the eyes' outer corners: the sweat drops' pool); let go after a good one, "tickle: after" (a giggle, calming). The
bottom key's tickle (`tap_tickle` taps) laughs too.
His voice (pip.c, the voice section before `react`): every reaction's sound goes through `speak(sound, cls, mood)`.
`react_cls`: 3 a fright, a fall, a throw and the few that must show (waking, the song, the alarm: the `must` list); 2 an
answer to you (within 1.5 s of `s.input_at`: touch_cb, pip_seen, pip_tap/feed/wash/game/phone/note, a breath, the IMU
handled); 1 his own feelings. Budget `snd.tokens` (+1 every `snd_refill` s, cap 2.5): class 3 always speaks, 2 while the
budget is over -0.5 then a `BABBLE_INTERJ`, then silence, 1 only with a token (or an interjection for a big change of
feeling), talk (0) needs 1.3 tokens and 1.2 s of quiet; a sound never cuts a more important one, and two answers within
0.35 s (a swing that is a shake) keep the first's sound. `mood_sound` gives each feeling several sounds (the classic
`mood_voice` twice as likely, never the same twice running; `vrand`, the voice's own dice; vary 0: the classic only).
A feeling of his own (class 1) does not replace a face showing under `pet_tune.hold` (0.6 s): it waits in `rq` (the
newest, stale after 1.5 s); an answer to you is at once. Memory (`mem[MEM_N]`, 4): each fresh class 2-3 reaction with
some weight (|valence| >= 0.3 or a giddy one, >= 0.4 s) is remembered (`remember`, weight fading over `MEM_FADE_S`);
when that reaction runs out `tell_after` may (by `talk`, chattiness and weight) set `s.tell_in`: 0.35..1.1 s later, once
nothing else goes on (within 3 s), `tell` - a face from it calmed (`tell_face`: shocked -> pouty/nervous, rage -> annoyed/
indignant, laughing -> happy/cheeky, whirled -> woozy/dizzy...) and a babble in its tone blended with the affect, reaction
"tells"; idle chats recall the strongest untold memory half the time. An answer to you cuts a tale short (`voice_hush`).
Company (`COMPANY_S`, 45 s since your last input): his moments come 1.8x sooner (face held 7 s, not 12) and M_CHAT x2.2.
Knobs: `talk` (0 off .. 2), `hold`, `snd_refill` (0: every feeling speaks, as before); check.py runs with talk 0 unless
a check sets it.
Blowing (pip.c `blow_step`): breaths with pauses under `BREATH_GAP` (2.5 s, drawing breath) are one go - the face holds
and the pause counts in `blow_t`; the go builds in steps (`blow_stage`): a first answer (ouch/whoa/breeze/tickles), at 0.3
of `cold_s` "enough" (annoyed/pouty, or afraid/nervous by trust), at 0.65 "chilly" (`VOICE_BRR`), at `cold_s` (10 s) cold
(frozen). Whatever face a step picks, both eyes screw up against the breath while it lasts (`s.wind`, `WIND_SQUINT`
of a poke's squint, opening over ~0.3 s once it stops). A go begun on a wet Pip (`blow_wet`) dries him first (no "enough" step), chilly at 0.55 and cold at 0.6 of it.
Breath on the mic (mic.c, `BLOW_*`, tuned on the board): 25 ms blocks over `BLOW_DBFS` and `BLOW_RISE` dB over the room
(`m.floor`: down at once, up slowly, held while blown on), steady since they rose (within `BLOW_SPREAD` over `BLOW_WIN`
blocks) and low (`BLOW_LOW` of it under 300 Hz: a breath 30..50%, music and talk 8..23%) or overloading the mic
(`BLOW_LOUD`: blown on very close), for `BLOW_S`; on for over `BLOW_MAX` (12 s) with no pause it is a noise, the room now.
Knocks and claps are too short, a loud room raises the floor. Tuning on the board: debug `mictrace 1` prints every block
(`@A`), `@S` `bl`; `python firmware\serlog.py <s> --send "mictrace 1"` (`--clock` sets the computer's time first).
pipsim `Q` blows as a quiet mic hears it (low rumble), `Q2` close up (broadband, overloaded).
A bang (pip.c, knobs `loud_db`/`loud_jump`/`shock_db`): the loudest 25 ms block (`mic_peak_db`, unsmoothed: a knock is
over in 50 ms, the meter would take ~10 dB off it) over `loud_db` and `loud_jump` over the room, held back 0.45 s and
only if by then the meter fell `BANG_GONE` under the peak (short: not a breath or a noise come on); on the board a quiet
knock peaks 62..66, a clap 76..84, a loud one ~86. Afraid `after_fright` (10 s); any kindness (stroke, food, play,
tickles, chase, a bath...) ends the fright at once (pet.c `calm_fright`).
Music: mic.c hears it two ways, either is enough (or both halfway, `BOTH_ON`): a beat (onset curves in three bands every
10 ms - the kick under 150 Hz, the middle, the top - each autocorrelated over 6 s twice a second, beat lengths 65..180 a
minute in half frames, scored by beat + two + a bar; the kick, the top or all three together count, the middle alone
(talk) not; a beat that has not kept its tempo and phase over two evaluations counts half) and tones (a 512-point FFT
every 64 ms of the sound at ~8 kHz: peaks held `TONE_MIN` spectra, not in bins a tone always sits in (a hum), as a share
of the sound over ~2.5 s: an intro with a voice and chords, singing, a piano). `BT_HOLD` evaluations to switch on,
`BT_HOLD_OFF` off; ~25 KB on the heap (PSRAM). Its phase and tempo: `mic_music`, `mic_bpm`, `mic_beat_phase`. Tuned on a
bench of synthetic scenes (songs, rap over a beat, a tempo change, a cappella, piano; talk, two talking in noise, a fan,
claps). pip.c `dance_step` dances to it (`PIP_DANCING`: a hop, a nod and the eyes across on each beat) if he is in the mood
(`dance_chance`: cheerful moods always, sore/sad/poorly ones never, the rest `dance_will` + valence; not now: asked
again after a rest) when no other reaction is on, listening (`PIP_F_MUSIC`, the panel's "Музыка"; not in eco) and awake; in bouts of
`dance_s`..1.6x s, then "danced out" (relieved) and a rest of `dance_rest`..2x s; disturbed (touch, a button, lifting
him: `pw.idle` reset, or any other reaction) he stops and leaves it for `dance_break` s (pet_tune knobs). He also dances while
the linked phone plays music (`phone_playing()`, AMS; his own `PHONE_BPM` unless the mic hears it; `PIP_F_MUSIC`, and not in eco: the panel locks
"Музыка" off with the mic ones); music getting him going is `PET_TICKLE` (fun, love) once until it stops; music never holds the sleep timer
(left untouched he dozes off mid-song). pipsim:
`U<bpm>` music, `U<bpm>r` hip-hop, `D` talk, the status' `music=` `bpm=`. A reaction that
crowds out another still showing and spoke within `REACT_QUIET_S` comes back silently (`spoke[]`); stroking ends the rhythm
game.
A sneeze (pip.c `pip_sneeze`, `sneeze_face`): a ~1.4 s face over any mood, timed to `VOICE_SNEEZE`'s "CHOO" (`SNEEZE_BURST`),
five spray dots; on his own every `sneeze_min`..3x minutes, more likely with each poke (`sneeze_poke`, at most 1 in 5, none within `SNEEZE_REST` (60 s) of the last), every few seconds
when allergic; its own xorshift so it never shifts other dice; pipsim/web key `z`. `VOICE_RING` rings with a call (phone.c `call_spin`, which keeps the screen lit), an interface sound at the Звуки menu level; the timer's `VOICE_ALARM` always plays at full volume (voice.c `voice_say`), even with the sounds off;
a call takes the top button as answer and the bottom as decline, a ringing timer's card goes with either, even the press
that lights a dark screen (shell.c), and both hush at once (`voice_hush`: a 30 ms fade).
Notifications: asleep face down (`pip_sleeping_face_down`) a notification or a call makes no sound and lights nothing;
the newest `MAX_NOTES` (10) are kept; the banner (phone.c `banner_show`) is square but in Барби, as tall as
its lines, slides down, stays `BANNER_MS` (2 s) and slides up; a newer one replaces it at once. The fake phone's `N` comes
connected or not. `pet_settle()` remembers what it showed (palette hysteresis,
how long an urge has held). Drowsy (`pet_drowsy`: sleepy from tiredness, the night or just waking), a shake or half a second of stroking may
rouse him (`pet_rouse`, reaction "perked up", each miss makes the next try likelier: knobs `perk_*`); a nap with the screen
dark rests him `nap_rest` of a face-down sleep. Asleep, food runs at half the awake rate, love at `love_asleep`, the walk
at `walk_asleep`, fun not at all; 23-7 by his clock all three x`night_drain` (1.3), whatever the sleep (his own, face
down, a nap): a real night costs a little more than a day's nap (pet_test checks it). A fresh pet's food starts at `full_line`, so the first biscuit is welcome; the cable feeds him
(pip.c `CHARGE_FOOD_H`) only up to `full_line` too (a Pip just flashed sits on it: he was "too full" at his first biscuit).
The grudge (`pet_t.grudge`, 0..1; pet.c `grudge_add`, `soothe`): anger outlasts its face. Every cross face from `pet_event`
(annoyed .04 .. berserk .55; thrown +0.2 and pestered +0.12 more, cross washes) adds to it; it fades `grudge_fade` an hour
(x(1+0.6 valence), x2 asleep, Неприхотливый x1.3 / Требовательный x0.7, slower while `annoy`); kindness (strokes, food,
a bath, games, chase, tickles) takes only a little off, less the crosser he is; but a long stroke wins him round (`pet_coax`, knob `coax`: each half second takes more the longer the stroke goes on, x1..x3 by 30 s, at least half however cross): "still cross", after `COAX_NOTICE_S` "softening" (pouty, then shy), then "made up" (touched; the hurt aftertastes go, love and trust) - ~20 s for a sulky Pip, ~45 s seething. Lines (each ±0.1 by valence): over `grudge_show`
it is the base mood (why 3.19: hurt/suspicious, past 0.65 annoyed/indignant), over `grudge_forgive` (`pet_cold`) a stroke does
not make up a hurt aftertaste and kindness gets no thanks (reactions "still cross", "sulky bite"; a bath no "fresh" aftertaste),
past `grudge_refuse` food and the sponge may be turned down (`pet_spurns`: "sulky: no food" / "sulky: no washing"), past
`grudge_play` (`pet_sulking`) no games with him: the tired card says "Пип обиделся". pipsim `e<v>`, status `grudge=`; debug
`grudge <v>`, `@S` `gr`; the web sim's "Обида" bar.
Trust (`pet_t.trust`, 0.5 at first; the care app's 7th meter, Доверие): rough handling (thrown, dropped, pestered, woken, fed up) takes it at once, kindness
(stroked, fed hungry, played, chased) gives at most `trust_day` a day; under `trust_flinch` he may flinch from a finger
or a jolt (pip.c `flinch`), over `trust_bold` he says hi himself (`say_hi`), and it shifts `pet_meet`. Jealous: the
phone's notifications (60 s each) and its music while he is not dancing add up in `s.phone_s` (a touch clears it); past
`jealous_s` a `jealous_chance` roll, `jealous_gap` min apart, makes him pouty (`PET_JEALOUS`, `PET_AFTER_JEALOUS`).
Tumbling (pip.c `tumble_turn`, imu.c `IMU_TURN`: each turn over, upright <-> upside down, at once): `TUMBLE_N` (4) turns
within `TUMBLE_S` (4 s) -> `PIP_TUMBLING` (giddy: half-spiral pupils, grin, blush, the head going round), `VOICE_LOOP`,
`PET_TUMBLED` (fun); kept up, the fourth tumble running (`TUMBLE_MUCH`, one fades in 40 s) -> dizzy ("tumbled out");
`PIP_F_TUMBLE` (10) is the panel's "Перевороты"; the web sim's "Кувырок" button. No turns are counted while he is
shaken (imu.c: `shaking` or energy over `SHAKE_G`): shaken hard up and down, the raw y swung past -0.6 g on every stroke
and a hard shake came out a glad tumble.
How hard a shake (pip.c `pip_imu`): energy over `SHAKE_HARD_G` (1.2) or kept up past `SHAKE_LONG_S` (1.6 s) is "shaken
hard" - dizzy, never glad (`s.shake_hard`, once a bout); a light short one keeps its varied answers. Jiggled side to
side faster than `SWING_MIN_S` (0.22 s a half) is no swing (it was "whee" over and over); kept up, dizzy too. pipsim
`S<g>[y]` (how hard, along y), check.py "shaken hard". Measured on the owner's hand (debug `imutrace 1`: `@I ms energy
peak-energy peak-|a| shake-s shaking reaction` ~20 a second; Nov 2026): light 0.25..0.35, middling 0.3..0.6, hard 1.7..2
(peaks 4-5 g), wild ~4.5 (the ±4 g sensor saturated). A light jiggle (fast side to side, under `SHAKE_G`) is glad once a
bout ("shaken: light"/whee/giggle); kept up past `SHAKE_LONG_S` at over 0.7 `SHAKE_G`, or `SHAKE_LIGHT_LONG_S` (4 s) light,
dizzy. Shaken at all (`SHAKE_G_ANY`, 0.25) and a second after (`s.shook_at`), the mic's breath and bangs are ignored: the
case rattles (a wild shake was heard as "blown: breeze").
His body's noises can be switched off: `PIP_F_SNEEZE` (11), `PIP_F_BURP` (12), `PIP_F_TOOT` (13), the panel's "Неловкие звуки"
under Болтливость (`BODY` in docs/index.html; feat bits like the rest): off, none at all, not even by key (pip_sneeze/burp/toot
return; M_TOOT not picked, M_ILL coughs, a boop never sneezes).
The owner's birthday (the panel's "Дни рождения" tile next to "Падения", off after a flash: `bday d m`, 0 0 off, NVS "bday",
`@C` `bday:[d,m]`; pip.c `pip_birthday`, `bday_step`): on that day by the real clock only (`pip_clock_known`), the first time
his face is up and he is awake (never waking him) he sings `VOICE_BIRTHDAY` (the first two lines of Happy Birthday, public
domain) excited, hopping to it, with two pairs of party poppers (`confetti`, plain rectangles), once a day (`pet_t.bday_greeted`;
a date set anew, not the one read at power-on, is greeted afresh); all day `pet_festive` gives the great palette, no sour base mood (pet.c `pet_mood_why` turns one into content/comfy/happy/curious;
reactions still show) and his day mood lifts by itself, and every
`bday_gap`..2x min a burst ("festive": poppers or a rain, a hop, happy/excited/laughing), one in 1/`bday_encore` the song again. 29 Feb is the 28th in other years.
pipsim `b<d>,<m>` (runs start 10 October); the web sim's "Время" block in "Жизненные показатели" ("Вернуть реальное время":
the speed to x1 and the clock to the wall's, a clock ahead moved back with `pip_pet_rebase`, which shifts every timestamp of
the pet so nothing he lived is undone and nothing stands still).
The charger (pip.c `pip_battery`, pet.c): plugged in he is glad ("charging", "charging: zap", "charging: sip"); kept on it at ≥99% past `overcharge_h` (`pet_t.full_h`; 4.5 h Уравновешенный, 6 Неприхотливый, 3 Требовательный: pet.c `over_h`)
he is overcharged - the body branch `PET_BODY_OVERCHARGED` (why 2.4: annoyed/angry, past twice that sad/hurt; asleep he sleeps on)
and it costs day mood and love by the hour; taken off then, hurt (`PET_AFTER_OVERCHARGED`); taken off at `unplug_lo`..`unplug_hi`
% pleased (`PET_AFTER_UNPLUGGED`) only after a real charge (`unplug_min` min on it), not just after fiddling and once in `unplug_gap` h
(no farming his mood by the cable); the cable in twice within `fiddle_s` s is a doubtful look ("cable again"), from `fiddle_n` on every
plug is `PET_CABLE_FIDDLE` (Требовательный angry, longer, more trust lost), crosser each time by `annoy`: angry, furious, rage. pipsim
`J<pct>[+]`. The web sim's "Батарея" card: a USB-C cable on the model (click it, or the button), a charge slider; it drains (more
with the screen lit), charges fast to 80% then slowly, and at 0% the keychain is off (no frames, input or time for him) until
the cable or the top key (with charge) switches it on - clock lost, the power-on intro.
Run flat: main.c's power-off under `EMPTY_MV` for `EMPTY_S` with no cable (the only path that does it: never the top key, a
reset or a flash) calls `pip_drained` (`PET_DRAINED`: food -`drain_food`, energy -`drain_energy`, 0.4 each, a little love),
saves him and sets NVS "drained"; the next boot reads and erases it (`pip_drained_wake`) and the power-on intro ends
angry, not cheeky (`PET_WOKE_DRAINED`: reaction "drained", aftertaste `PET_AFTER_DRAINED` = why 3.20 for `after_drained`
s, a grudge, a little trust and day mood; a long stroke makes up). pipsim `z`; the web sim at 0% (`keypet.drained`).
A bad night: `night_notes` notifications while he sleeps 23–7 (`PET_NIGHT_NOTE`) and he wakes angry (`PET_AFTER_NIGHT`).
The chase: a finger kept moving (> 70 px/s, not petting) for `chase_s` is a game (`PET_CHASED` once a touch): his eyes
lead the finger, he hops, crouches and pounces when it stops, "caught" on release (pip.c `chase_step`).
Death: food at zero for `die_h` hours (faster with no energy or filthy too, and by his character: pet.c `starve_pace`,
die_h 27, x0.85 / 1 / 2: from full, dead in ~72 / 47 / 28 h; `pet_t.starve_h`, a meal clears it) and he dies (`pet_dead`, pip.c `die`): the pet is wiped, his screen says "Пип умер" and how many days he lived over cracked glass (pip.c `cracks`: fixed lv_lines made once),
goes dark after `DEAD_DARK_S` (5 s) and only a button lights it again (no touch, no motion: `pip_imu` ignores it), nothing answers
(shell.c, phone.c, `shell_open`). main.c erases NVS "pet" and keeps "dead" (days), read back at boot (`pip_set_dead`), so
an update leaves him dead and only a clean flash brings a new one; `@C` has `dead`, the panel shows it; debug `die`.
pipsim: `q<v>` sets trust, status `dead=` `trust=`; the web sim keeps `keypet.dead` until "Новый питомец".
Anger's ladder by `annoy` (pet.c `cross`): angry, furious, rage, swearing, then berserk, weighed by his life: a good day
takes more (`cross_good`), swearing only under `swear_v` valence, berserk only under `berserk_v` (`berserk` knob; `PIP_BERSERK`, "безумие":
red, pinpoint spiral pupils darting, jolted about, a hail of grawlix `FX_RAGE`, `VOICE_RANT`).
He is kind: pestering makes him cross slowly, by his character's patience (`levels[]` `patience`, `pet_patience()`: 2
Неприхотливый, 1.5 Уравновешенный, 1 Требовательный). Quick pokes add 1 to `s.annoy` (fading 0.4 a second) and he is fed up
at `POKE_FED_UP` = 3 + 4.5 patience (~15 / ~17 / ~20 quick pokes with the sneezes they bring; it was 5 for all); a poke or a
shake adds 0.35 / 0.5 over patience to `pet_t.annoy`; the "hey!" answers to a poke, a flick, a boop, a spiral come only past
half of it (`irk()`); and fed-ups climb the ladder one rung at a time (`PET_FED_UP`: the first always just put out, then
angry, furious, rage, swearing, berserk as far as `cross` of annoy / patience allows; 10 quiet minutes start it over).
A throw, a fall, the cable are mistreatment, not pestering: `cross` and their own ladders as before.
Every threshold is a knob in `pet_tune`, named in `pet_knobs[]` (debug `set`, pipsim
`K<name>=<v>`). Reactions (`react()`) override it for seconds. Touch (a tap pokes; on an eye only that eye squints, `eye_at`; the finger's speed over the last quarter second picks petting, above `PET_FAST`, or just watching it, under `PET_SLOW`), IMU (`imu.c`: tilt, shake, face-down, peekaboo, upside
down, free fall: dropped, or thrown when a shove over `THROW_G` came just before (`IMU_THROW`), steps), mic (`mic.c`: dB, blowing; a bang in `pip.c`), battery, the phone (`pip_phone`, `pip_note`),
games (`pip_game`), `pip_seen()` and `pip_wake_up()` (power-on intro) feed it. After a clean flash (no saved pet) he is dormant (`pet_t.met` false: a black screen, nothing drawn, time and his age frozen,
touch/IMU/reactions ignored) until any button (`pip_hatch`: his birth, ~32 s, once in his life; main.c saves him at once;
the sleep timer holds). First the sky, `ui/cosmos.c` (times in cosmos.h `COSMOS_*`, s after the button): stars come out, one
falls into the middle (the spark), it beats (`cosmos_beat`), a galaxy winds up and falls in, dark, the bang, two slits of
gold - his eyes shut (`cosmos_eye`). Every frame is worked out from the time alone and covers the glass: soft light (glows,
rings) for 2x2 cells two rows at a time, dithered and written once (never read back: the frame is in PSRAM), then the
stars' points; rows and stars taken by both cores (`cosmos_parallel`, main.c `cosmos_two_cores`), the face not drawn
meanwhile (pip.c tick_cb, until `HATCH_EYES` - 0.25). The first version (full resolution, one core, libm per star) ran at
9..13 fps on the board; ~30 now. `lv_event_set_cover_res` only raises what the object said (COVER is 0, NOT_COVER 1), so it can
never say "covers": write `info->res` itself, as cosmos.c's `cover` does (without it LVGL filled the screen first, 14 ms).
Then him (pip.c `HATCH_E(x)`, hatch_fx): stirring, one eye first, the world, PIP_STARSTRUCK, PIP_BUTTERFLY on his nose, you,
fireworks (`hatch_burst` sprites, `hatch_dots` confetti, one at a time: two at once cost the eyes' frames), "hello" and hearts.
Sounds: `VOICE_SKYLIT`, `HEART`, `RISER`, `GRAND`, `ANTHEM` with the old `SPARK`, `STARDUST`, `BLOOM` (fixed pitch) over a chord
held under it all (`voice_pad`: up to ten soft sines easing from chord to chord, at his voice's level). On the board:
sky 30 fps, bang and waking 22..24. Debug `birth [s]` (`pip_birth_show`) plays it again for a met Pip from that moment (only
wakes him); bench.py phases "birth sky" .. "birth joy"; check.py looks for the stars, the spark, the dark, the rings, the eyes.
pipsim starts every run met (`pip_meet_quietly`); event `O` makes a fresh dormant pet. `pip_feature()` switches inputs off
(`PIP_F_*`, NVS "featoff" a u16 of the ones off, so a new one starts on; the panel's "Что Пип замечает" lists them, `PIP_F_FALL` = "Падения": dropped/thrown
ignored; `PIP_F_WAKE` (9) is the panel's "Просыпается от движения" tick under the brightness slider: on, a dozed screen lights
up when picked up or shaken; off, while the screen is dark motion does nothing at all (no wake, no reaction, no sound,
not even turning over out of face down; steps still count); while it is on `PIP_F_SHAKE` is held on (pip.c `FEAT`, the
panel locks "Тряска"); pipsim `f<i>,<0|1>`).
Left alone for `pip_power()` seconds (default 30; touch, buttons, lifting him, notifications count; the web sim's "Экран"
card sets it and `PIP_F_WAKE`) the screen dims, then sleeps
(and so does he, a nap, `PET_DOZE`, unless he is Требовательный; lit again he wakes calmly, `PET_UNDOZE`); no app holds it on, only a game played without touching (one with `.imu`: the ball, the maze, coin, 8 ball, die) while it is tilted or shaken. Face down for 2 s the screen sleeps (`pip_display_on()`: main.c turns the panel off, pip.c stops drawing, pipsim
renders black and prints `display=0`); turning it over or any button wakes it (that press does nothing else).

**Shell** (`ui/shell.c`): Pip ⇄ menu ⇄ apps, side buttons (top = open, bottom = back/meet; on Pip a tap of the bottom one is `pip_tap` on release, held 0.6 s it feeds him: `pip_feed`, a biscuit drops in, pipsim `B1`;
bottom twice within 0.45 s from an app or a group's list goes straight to Pip, `BACK_TWICE_MS`; a bottom press an app
takes for itself, such as a notification's text back to its list, arms it too).
Bottom taps on Pip (`pip_tap`, `tp` in pip.c, knobs `tap_*`): each answered at once, no waiting for the series to end: 1 meet,
2–3 "tickled" (`PET_TICKLE`), then "stop it" and "pestered" (`PET_PESTER`: angry/furious, aftertaste `PET_AFTER_PESTERED` hurt, a stroke
makes up) from `tap_annoy` / `tap_angry` + 1 + 4 (patience - 1): Требовательный 5 / 8, Уравновешенный 7 / 10, Неприхотливый 9 / 12 -
past `tap_spin` (10) "pestered" never comes, 10 is "head spin" then "phew"; a series of 2–5 is blinked back; asleep `tap_wake` taps wake him grumpy ("tapped awake"), drowsy each
tap of a series may rouse him (`pet_rouse`). Four slow even taps (`tap_quick`..`tap_slow` apart) are a beat: "groove" (he bobs), you stop,
he drums a pattern (`VOICE_TOCK`, "drums"), "your turn", then "right beat" (longer, up to 6, "rhythm master") or "wrong beat"/"no answer";
counts as a game with him once (`PET_PLAY`). Feeding and playing are not in the care app any more (it shows needs, days and steps; the Звуки app in Настройки sets how loud his voice and the interface's sounds are, apart, 0..`VOICE_STEPS` (5) by a tap on a row's segments: `voice_levels`, NVS "sounds" (pip | ui << 4; after a clean flash him 5, the interface `VOICE_UI_DEFAULT` 3 = 60%), the panel's `sound p u` with two sliders, `@C` `sound:[p,u]`); a move against him in noughts and crosses is `pip_play_move` (PET_MOVE). From Pip the menu
opens with the top button only (no swipe on the face; a flick there does nothing). The way back from an app or the menu: either button (the top one too, unless the app gives it a job: stopwatch, maze, camera, music, the ball once over; no app swallows it doing nothing), or the back gesture: a finger put down within `EDGE_W` of the right edge and drawn `EDGE_DX` left (shell.c `edge_input`, watched on the touch input itself, PRESSED/RELEASED reach it before any widget; it acts as the bottom key, but leaves the stopwatch rather than resetting it, nothing on his face or under a call/banner/pairing; pipsim: `d455,233` then `m` left and `u`); its sign is `edge_hint`, a 4 x 44 px bar at the right edge in the theme's ink at 30% on the top layer, shown off his face only (`edge_hint_show` in `go`/`open_app`). The timer is worked by taps only; when it
ends (open or not) it goes home and lays a "Время вышло" card over everything (in Ретро the utilities show no
"пип-01 // …" header) (`apps_alert_*`, shell_where `timer_done`). `apps[]` in
`ui/apps.c` (the default layout; the panel may reorder, move and rename them: shell.c `mn`, rows by `mn.slot`, never by app index): the apps with no `.group` are the menu (Пип, Игры, Утилиты, Телефон, Настройки; the group owners have no `.open`); apps
with `.group` follow, each group in one run: "tools" (clock, timer, stopwatch, noise, torch), "games" (xo, pong, snake, flight, repeat, ball, eight, coin, dice, maze), "phone" (notify,
music, camera, locate), "settings" (sound = "Звуки", style = "Темы", bright = "Яркость" (`shell_brightness`: main.c reads it back each loop and saves it as the panel's `bright`; the web sim dims its screen by it), eyes = "Глаза", privacy = "Приватность" (phone.c, what a
notification shows; the panel's "privacy i"), bt). Themes are set in Темы (a tap on a row) or on the panel. The list menu turns into a group when its owner row is opened (`group_enter`; back and a theme
change land on the app's row, `group_return`).
shell.c `MAX_APPS` rows at most. An app gets `.imu` (accelerometer, g) while it is open. A game played without touching (group "games" with `.imu`: ball, maze, coin, 8 ball, die) holds the screen lit while it is played (shell.c `playing`: tilted by `PLAY_TILT`, touched or a key); the others sleep on the set timer, lying still and untouched for `PLAY_STILL_MS` (2 min) the sleep timer runs again. Off his face (menu, any app) Pip is switched off (`pip_away`, set in shell.c `go`): no voice, no faces shown, the IMU gives him only steps and face down; reactions are still recorded silently (a game's result) - but a game with him ending (`pip_game` won/lost, not a record) is heard once on its screen: glad (`VOICE_TADA`/`HAPPY`/`LAUGH`) when he won, sad (`SAD`/`MUMBLE`/`POUT`, a sore loser `SAD`) when he lost, in his tone of the moment. The maze's physics is plain C in `ui/maze.c`
(`maze_test` checks it). Games played with him (`.with_pip`: ping-pong) open `app_tired` ("Пип устал") instead
while `pip_wants_play()` is false (asleep, or energy under `pet_tune.play_tired`, the line the care app's "Играть"
refuses at); a move with him costs 0.01 energy and a game's end 0.04, times his character's `play_cost` (levels[]: 0.45 / 0.65 /
0.85: from a night's rest ~19 / ~13 / ~10 games before he is tired, it was ~8 for all; pet_test counts them); `sh.gated` + the `CUR` macro in shell.c route close/imu/buttons to it, `shell_where()` says `tired`.
Noughts and crosses stays open but only "с другом" then; the ball, the maze and the toys are not gated. The toys (coin,
8 ball, die: `toy_*` in apps.c) share one look: a token in the screen's background, made once per theme and toy as three
region pictures (ground with shadow, token colours, coverage); while it plays (a 20 ms timer, only then) `toy_paint` puts
the token back shifted and squashed by row copies and a look-up (no per-pixel maths, no LVGL transforms): the coin is
tossed and turns over (Орёл / Решка inside its ring), the die tumbles with a hop and a new face each time it is edge-on,
the 8 ball (in the middle of the screen, `toy_y`) is shaken and the answer rises in its window with a click (`eight_fit`:
lines broken by hand to fit the round window, a one- or two-letter word kept with the next, the biggest font that fits;
`eight_says[]` has no punctuation: a dash, comma or colon at a line's end pulls it off centre), no word under it; the coin and the die land with a word; ping-pong (`pg_*`) is first to 5, his aim error grows when he leads. Snake (`sn_*`): a 15x15 grid in one RGB565 canvas, a step repaints and invalidates only the cells that changed (no object per segment); a swipe turns (again every `SN_SWIPE` px), a tap turns toward that side of the screen, two turns queued, none straight back; faster each bite; top pauses/goes on, a tap or top after a crash starts again; the canvas' buffer is freed with the canvas (`sn_gone`: the screen still slides away after close); pipsim status `snake=len,state` (0 ready 1 playing 2 paused 3 over). "Flappy Pip" (`fl_*`, id flight): Pip's eyes fly through the gaps of three columns (a tap on
its press or the top key flaps, `VOICE_FLAP` "fwip"; a point is `VOICE_POINT`, an achievement's two bell notes C6-G6 at a
fixed pitch, ~0.2 s, and a flap within `FL_POINT_MS` of it stays silent so it is not cut off; both at the interface's sound level, not his voice's; gravity and the columns' pace on real time between frames, so it plays the same at any
frame rate); the eyes look up as they climb and down as they fall, screw up a moment at a flap, shut on a crash; drawn
from plain rectangles only moved (no per-pixel work, no opacity: the trail's inks are mixed solid, `fl_mix`); a point a
column, faster and narrower with each; after a crash taps wait `FL_AGAIN_MS`; best of this power-on kept. Not a game with
him (no `.with_pip`) but in the games group, so his attention counts. On the board ~27-35 frames a second while flying
(every frame streams all 466 rows: the columns are full height), render ~10 ms. pipsim status `flight=points,state`
(0 ready 1 flying 2 over). An autopilot for benches (`apps_flight_auto`, debug `flauto 1`, pipsim `x1`): it flaps when
under the next gap's middle and starts again after a crash. Measured with it (Nov 2026): Стандартная 27.2, Ретро 24.0,
Барби 21.6 fps -> ~31 / ~30 / ~28 (the grid drawn by rows, `grid_draw`; the changed windows streamed alone and started
by `scan_plan`, which also ended the columns tearing near the top; Барби's rounded ledges cost nothing measurable). Tried and dropped,
slower: the columns as one object drawing all of them with only their edges' bands redrawn (thin full-height bands
cost about what wide ones do, and every area drew every rectangle: Барби's render 19 -> 30 ms). The stopwatch stops at 99:59.99 (`SW_MAX`). "Повтори за мной" (`rp_*`, `.with_pip`): his eyes look each way in turn, that side's pad lights and he sings its note (`VOICE_NOTE0..3`, C E G C, fixed pitch), then you repeat by swipe or a tap on that side; right: a squint and one more, wrong or `RP_WAIT` s idle: over (smug); past `rp_goal[pet_level]` (6/8/10) you beat him (`pip_game` LOST once), short of it his win; each round passed `pip_play_move`; shown faster as it grows and by `rp_pace`; pipsim status `repeat=len,state` (0 ready 1 showing 2 your turn 3 right 4 over).
Attention (pet.c `attend`, units): a minute of an app or the menu is 1, of a game 2, of a game with him 3 (shell.c `shell_imu`: off his face, touched, a key or a tilt within `ATTN_IDLE_MS`, 20 s; an app left lying open counts for nothing), of stroking 6 (`PET_STROKE`); a biscuit 1, a bath 2, a whole game 3, catch 1.5, a tickle, peekaboo or a tumble 0.5, a made-up grudge 4. `attn_day` (12.5 / 30 / 55 by character: ~5, ~12, ~22 minutes in a mix) makes up a day's loss of love (16 h awake + 8 asleep) and, games being half of it, of fun; it is the only way love and fun rise. `newborn()`: love and fun run down x1.5 for his first 2 days, x1 by the 7th, none of it past trust 0.7. pipsim `v<love>`. How hard he is to beat follows his character (`pet_level`): `xo_skill` (look-ahead, slips) and `pg_skill` (aim spread, paddle speed).
The menu is a list wheel in every theme, words only (no "01" numbers before the rows) (no icons anywhere in the UI, the fonts carry none); the battery over
it is drawn in the theme's inks; on the charger (main.c: the cable in, `bat.usb`, not just the AXP2101 charging - full,
it reads standby, and the pet's overcharge needs "charging" at >=99%) its fill turns a green of the theme's darkness
(shell.c `bat_green`) and a bolt (one 4-point `lv_line`) stands before the figure, the battery the same size. A theme
with a dot grid (`th->grid`) has the screen draw it itself (theme.c `grid_draw` on `LV_EVENT_DRAW_MAIN_BEGIN`, the style
transparent, `grid_cover` claiming cover): straight into the frame a row at a time from two rows made once (plain, dotted;
~1 KB each, they stay in the 64 KB cache). A full-screen picture was copied out of PSRAM under everything that moved
(434 KB, past the cache); it is still drawn by LVGL into a layer of another format or while the screen fades in (its
colour, then the picture, at the screen's opacity: as the style once drew it). Pixel-identical in pipsim. List rows are pre-rendered A8 masks at every size (rows_build/shrink) — never scale
text live, it costs ~45 ms a frame on the board.

**Themes** (`ui/theme.c`): `th->…` colours/fonts; `TH_TERMINAL` (Ретро) draws with a cell kit
(`th_cells`, `th_header` with its rule, `th_plate`; no theme shows the "пип-01 //" tag, and only the menu's plate cursor
blinks; screen titles go through `th_title`: capitals, or a capital first letter, the same words in every theme), the other themes must not show those terminal elements. No theme shows button hints (no triangles, no footers): a button's word is the same everywhere. `th_button_at` fits its word: too wide for the button less 20 px, the theme's smaller face, or (Барби has none with letters) letters 1-2 px closer. The
terminal's menu scroll cells sit left of the focus plate.
Three themes: 0 Стандартная (default), 1 Ретро, 2 Барби. The chosen one persists in NVS (main.c `theme_load/theme_keep`)
by index, the panel's `theme i`: add new themes at the end of `TH_*`.
Барби (`TH_BARBIE`): pink on deep plum, Pacifico (script: menu rows via `title`, clock digits) + Comfortaa Bold
(text), `caps = false`; its fonts come from `tools/fonts.ps1` (`Script` makes the mixed-case title font).

**iPhone** (`ui/phone.c` UI model ↔ `firmware/main/ble.c`, `sim/fakephone.c` in the sim): NimBLE peripheral
advertising as a HID keyboard "Pip"; ANCS notifications/calls, AMS music, CTS time, HID volume-up = camera shutter.
Two roles: the phone and the panel (or the debug page: whoever writes the data service is it; the Bluetooth page calls it
"Панель управления"). Nothing else is ours: a computer (it says so in Appearance, or has no Apple services and a public
address) is left alone, `not_ours`: no pairing, no services (there is no computer link any more; GATT_LAYOUT 4), and ble.c `sweep_now` lets go of any link that is neither the phone nor the panel `FOREIGN_S` (10 s) after it came (`pending2`, still unsure, `PENDING2_S` 30 s): it held a slot and woke the radio. The
bottom key's release is `shell_button_up` (it decides meet or feed on Pip). `B<s>` in pipsim holds the bottom key; the
status ends with `link=iphone|android|off`.
**Android** (no app): no ANCS/AMS/CTS. ble.c keeps it in the phone's place when it has none of Apple's services and either
says it is a phone (Appearance) or connects from a resolvable private address (computers use their public one: `private_addr`);
`phone_kind(PHONE_ANDROID)`: Уведомления and Музыка say "не поддерживается / на android",
the camera's shutter works, the Bluetooth page says Android. Its time comes from the panel (`time`), which also runs in Chrome
on Android over Web Bluetooth (no cable or flashing there). pipsim `l0/1/2`, the web sim's "Android (поддельный)" card.
No phone: Уведомления, Музыка, Камера and Локатор say "Нет связи / Нужно Bluetooth подключение" (phone.c `offline`); that comes first.
Локатор (phone.c `phone_locate_*`, id locate): the linked phone's link RSSI, to walk toward it. The host reads it only while
it is open (`phone_on_rssi(read, active)`: ble.c `rssi_read` asks the controller, `ble_gap_conn_rssi`, every `LOC_MS` 200,
from LVGL's task; `rssi_active` quickens the link, `link_pace` quick, and gives it back its easy pace on close; nothing is
read otherwise). Smoothed (`LOC_TAU` 0.8 s, the signal jumps 5-10 dB), a dial (lv_arc as the noise meter's), five bands
with `LOC_HYST` (Далеко, Неподалёку -82, Близко -74, Рядом -67, Здесь -60 dBm: this keychain hears a phone held against it
at only ~-55, its antenna on the board in the case; the dial -92..-52), "теплее / холоднее" from the smoothed slope, a radar ping
(`VOICE_TICK`) quicker the nearer, `VOICE_POINT` for each band up. Lost while open (`relinking`: phone_link rebuilt it):
"Связь пропала / телефон далеко или Bluetooth выключен / последний сигнал N дБм" (th_label keeps 92 bytes: short labels);
linked again it goes on; ~8 readings in a row with nothing: "нет данных о сигнале". A phone cannot be made to ring over
these links, and only the linked one can be found. pipsim `i<dBm>` sets the fake phone's signal, status `locate=band,dBm`.
Notifications off in Приватность (a phone linked): "отключены / в настройках". No UI text ends with a period.
The clock app: until a phone or the panel sets the time after a power-off (`shell_clock_known`, main.c from `real_time`)
it shows only "Время не синхронизировано / Нужно Bluetooth подключение" (no dial; apps.c `clock_unknown`, rebuilt when it changes);
an iPhone sets it on every link (CTS; the fake one too, `linked`) and, while it stays linked, again every 24 h (ble.c
`resync`: the chip's RC clock drifts in light sleep); the panel on every link (`time`) and every 24 h while linked; Android tells none.
pipsim `t0`, the web sim's "Часы" card; the web sim drops the phone's link when it goes flat and the phone comes back
2.5 s after power-on.

**Board display port** (firmware/main/main.c): own LVGL port, DIRECT mode into one PSRAM frame buffer
(RGB565_SWAPPED), flush hands the changed areas to the panel task, which streams them in 8 KB DMA chunks at 80 MHz QSPI:
as windows of their own (`rect_out`, joined when they overlap across, written top to bottom in bands of `RECT_BAND` rows,
every window's part of a band in turn) when they cover under 60% of their rows, else as whole rows. It starts when the write
cannot meet the panel's scan (`scan_plan`: TE on both edges gives the scan's period and blanking, so where it is; the
earliest start in the next frame at which neither overtakes the other, for the write 15% either side of `stream_mb_s`,
`SCAN_MARGIN` rows apart; TE's own pulse only if its timing is unknown). Before, every frame waited for TE (~8 ms on
average) and Flappy Pip's full-height columns (466 rows, ~16.5 ms, as slow as the scan) tore near the top. `@P` `rects`:
frames sent as windows and windows a frame.
LVGL on core 0, the face renderer splits rows across both cores (hot loops in IRAM, `-O2`). Pip's canvas is drawn in
the panel's byte order (`face_t.swapped`), so LVGL copies it with a plain memcpy; pipsim's display is RGB565_SWAPPED
too, so the sim runs the same paths. In `face.c` keep libm (`sinf`, `atan2f`, `sqrtf`, `fminf`…) out of per-pixel code:
use `sin_fast`/`atan2_fast`/`sqrt_fast`/`mn`/`mx`.
Measuring: debug `perf` (`perf lit` also keeps the screen on) answers `@P {json}` with what happened since the last
call: Pip's frames (count, avg, worst ms), the face draw and each core's band, LVGL's refresh and render, the panel's
TE wait and stream, rows a frame, each FreeRTOS task's share of a core (`CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS`), a
yardstick (`cal`: a float loop and a PSRAM copy, us); `tools/bench.py` pins every mood in turn and tabulates it. A
profiling build (`$env:PIP_FACE_PROFILE=1` before flash.ps1, touch firmware/main/CMakeLists.txt to reconfigure; pipsim:
cmake `-DPIP_FACE_PROFILE=ON`, sim/prof stands in for the cycle counter) adds `prof`: cycles and pixels a frame per path
of face.c (`PS`/`PE`). The springs ring down toward 0 for minutes: pip.c passes heart/glint/spiral/veins/wet under 0.01
as 0 (`TRACE`), or a trace kept the whole eye on the slow path (2-3x slower for minutes after a wet or veined mood).
Frame pipeline: the face master starts a frame over LVGL's priority (`FACE_START_PRIO`), hands core 1 its rows and drops
to `FACE_PRIO`; core 0 takes rows 4 at a time (`CHUNK0`: LVGL preempts it), core 1 16; a finished frame wakes LVGL's
task (`pip_frame_ready`) and the swap readies the display's refresh timer at once, so its wait for the panel is over
before the next frame is done. In face.c tears and veins keep the flat spans and 2x2 blocks (dry / deep-pool x ranges of
a row, `veined()` from `ball_hit()` alone), the spiral keeps blocks (its arms per pixel inside black ones).
The UI lock (`ui_lock`) is fair to waiters: a face cheaper than a refresh (ouch, asleep) has the next frame done before
LVGL's pass ends, so its task never blocked and took the lock straight back - the status line, `perf lit` and BLE starved
for seconds (bench.py "no answer", fake low fps); a waiter counts itself in `ui_waiting` and LVGL's task steps aside a tick.
On his face LVGL refreshes once a face frame (tick_cb pauses the display's refresh timer and readies it at the swap), so
effects moved meanwhile cost one refresh; small invalidated areas grow to 48 px tiles (`fx_tiles`) so many small pieces
stay under LVGL's 32 areas (past them it redraws the whole screen). The confetti is one object drawing every piece
(`confl`, `cf_paint` at the swap: each piece's old and new place as one area, near ones joined into at most `CF_BOXES`).
pip_key `y` sings the birthday song, `p` a burst of poppers, `r` a confetti rain (debug `key y`, pipsim `ky`; bench.py
has them as "bday song/poppers/rain").
The status log every 5 s prints fps, render ms, face ms, free internal/PSRAM and the worst frame of those 5 s with
what was on screen (`pip_frame_worst`; also `fw` in `@S`) — check it after visual changes.

**Debug link** (firmware/main/debug.c ↔ tools/pipdebug.*): `@S` JSON status each second, `@H` mood history
(a minute per row, a flash ring in the `storage` partition, survives reboots), text commands in
(`mood` (an index or a name), `react`, `need`, `urge`, `set`, `rare`, `game`, `feat`, `demo`, `hist`, `open`, `btn`, `key` (pip_key: `z` a sneeze), `wash 1|0`…); the knobs go out as
`@K` lines then `@T`. A new branch of the tree needs its `why` text in docs/debug/index.html and a check in sim/check.py.
A new mood (moods.h) needs its line in `HOW` of tools/emotions_table.py (the owner's table of every emotion: the script
refuses to build without it; its shots are cached in build\moods, delete one to retake it; a scene's telling moment goes into `WHEN`), and a way to get a mood
changed in pip.c/pet.c changes its line there too.

## Board constraints that bite

- Internal RAM is the scarce one (~25 KB free after BLE; 24.8 KB at boot in 1.3.10: face.c's hot code in IRAM takes from the same SRAM); allocations ≥ 2 KB go to PSRAM automatically, smaller ones stay
  internal: many small buffers (shell.c's menu row masks, one block per row now) ran it out before the speaker started
  (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=2048`). Never `esp_partition_read`/`esp_flash_read` straight into a PSRAM buffer:
  IDF bounces it through the largest free internal block (up to 16 KB), all there is - debug.c `hist_load` did, at boot
  while Bluetooth started (the status line's "least" 64 B); read in small chunks through the stack. Least free internal
  is ~7.7 KB now (after `ble_start`, BLE takes ~47 KB), ~15 KB free running. Never use `__thread` for buffers (it broke boot).
- Every I2C transfer has a finite timeout. The BSP's touch IO had 0 (= for ever): a NACKed CST9217 read with the bus
  left busy spun in the I2C driver for good, LVGL's task with it (face frozen, no answer, task_wdt naming lvgl in
  touch_read). main.c `touch_new` makes the touch itself with `TOUCH_I2C_MS` (30), and 3 failed reads in a row reset
  the bus (`touch: I2C failed` in the log) once a run of failures (runs are ordinary: ~100 at boot), never while a codec is set up or switched (`codec_mx`, taken without waiting: a reset in the ES8311's or ES7210's init made the BSP assert, a reboot in 2 boots of 4); reset every third failure the resets kept the
  reads failing for good and broke the codecs' init on the shared bus (boot loop on the ES7210's assert). Don't go back to `bsp_touch_new`.
- No FPU divide/sqrt on the ESP32-S3: avoid them in per-pixel loops (see face.c, the dice rasterizer in apps.c).
- Big LVGL circles/transforms/opacity layers are slow; prefer pre-rendered images (cup, grid background).
- The face renderer is bound by float maths (~4 cycles an operation with latency, divisions in software): no `/`, `floorf`,
  `ceilf` per row or pixel in face.c (`rcp_fast`, `ifloor`, `iceil`). Its cores split the rows with stealing (face.c
  `take_rows`): LVGL and the panel hold core 0 now and then. PSRAM is shared: with Pip's frame running others get ~13 MB/s.
- Dark greys in RGB565 tint green; quantize channels to 5 bits (see the dice cup) or use multiples of 8.
- Measure on the board before claiming performance; the sim is not representative.

Pip's frame rate: rules learned the hard way (Nov 2026: median 18.7 -> 26 fps, the slowest moods 2-4x, nothing simplified)
- Measure with `tools/bench.py` (every mood and the birthday's parts, `--tasks` for each task's share of a core) before
  and after; a new reaction, effect or scene goes into its phases. Keep the keychain still and untouched while it runs.
- Any face.c change must leave `tools/face_diff.py` at "identical" (take `--ref` before editing); if pixels move, look at
  them and say why. Simplifying an animation (fewer pieces, lower detail, a cheaper outline) is the owner's call: ask.
- A feature in eye_t (heart, glint, spiral, veins, wet, blush...) is switched off by exactly 0. Springs never reach 0: pass
  traces as 0 (`TRACE` in pip.c) or a long-gone effect keeps the whole eye on the slow path - this was the "random
  2-3x slowdowns for minutes" (and left grey specks in the pupils).
- A new per-pixel effect in face.c keeps the fast paths: work out where along a row it cannot change the colour (spans,
  linear ranges like the tears' dry/deep) and fill those flat; only the rest goes per pixel. Never make a whole eye
  "not plain" for something that touches a band of it.
- Don't move many small LVGL objects over Pip's face: every move is two invalidated areas, past LVGL's 32 it redraws the
  whole screen, and each refresh holds core 0 from the eyes (28 confetti objects made the birthday song 6 fps). Draw a
  swarm as one object (`confl`), invalidate each piece's old+new place once a frame, joined into a few boxes.
- LVGL on his face refreshes only at the face's swaps (tick_cb); keep it so, and don't add work to LVGL's task or core 0
  at a priority above the face without measuring - core 1 idles waiting for rows core 0 holds.
- Debug-only buffers (perf, the task table) live in PSRAM, made on first use: internal RAM is the scarce one.
  The debug task has a 4 KB stack: `board_perf` (main.c) prints `@P` in three snprintfs and keeps its arrays static in
  PSRAM (one call with ~30 doubles overran it mid-bench: resets). `@P` `dstack` is its least stack left (~600 B); `bench.py
  --log f` keeps every board line, so a reset or panic mid-run shows.
- A big pupil is what costs (amazed ~15 fps, neutral ~23 at night's bench): the pupil box's sample lines (`pupil_at`),
  its 2x2 blocks and the slow rim band all grow with it; the glint itself is a small part. Done (Nov 2026, face_diff
  identical, +0..2 fps, median 19.0 -> 19.8): `rbox` in IRAM (it sat in flash, called per sample and slow pixel), the
  block threshold 0.5 / 0.2 (0.3 / 0.1 moves pixels), blocks deep in a catchlight flat white (`glinted`), the glint's
  roots skipped far from it (`glint_near`) and outside the pupil. Then (Nov 2026, face_diff identical) each 2x2 block's
  verdict is worked out once per pair of sample lines (`eye_scratch_t.cls`, `cls_y`; its first pixel computes it, the
  other three read a byte): amazed 13.7 -> 14.9, touched/tumbling 14.5 -> 16.2, neutral 21.8 -> 26.4, median 18.7 -> ~21.
  Then (Nov 2026, face_diff identical) a flat block's path runs on along the row without
  the row's tests again (`block_cls`, the run in `eye_row`; `prev_row`/`left` get the same `pdi`): 14 big-pupil moods'
  median 20.0 -> 22.0 back to back, amazed 15.5 -> 16.7..19.7 (the spread between runs is as big as the gain). Check `xtensa-esp32s3-elf-nm` that face.c's per-pixel helpers sit at 0x403.. (IRAM), not 0x42.. (flash).
  bench.py sends "wake" before each mood: at night he dozed off mid-run (3 s "frames", "no answer").

## Conventions

- Verify visually with pipsim renders (contact sheets via PIL) and keep `sim/check.py` green; extend it when
  adding reactions/apps (`app_ids` list).
- Commit messages in Russian, summary line + short bullet list.
