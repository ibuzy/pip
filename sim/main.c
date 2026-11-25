/* Headless recorder for the KeyPet UI: deterministic runs saved to PNG/GIF/MP4 through ffmpeg.
   The interactive simulator is the web one (sim/web).

   pipsim --out a.gif [--secs 4] [--fps 30] [--events "0.5:k2 1:d100,200 1.3:m300,200 1.5:u 2:a0,0,-1"]
   events: <sec>:k<key>         pip_key
                 d<x>,<y>       finger down    m<x>,<y> move    u  up
                 h<x>,<y>,<x>,<y>  two fingers on the glass (held until the next h; the board's panel reports two at
                                most); h0 none, hP a palm (the panel's flag). The first finger is the pointer too
                 a<ax>,<ay>,<az> accelerometer (g, device axes), held until changed
                 S / W[g]       toggle shaking / walking on top of it (W0.6: a hard walk, 0.6 g bounce);
                                S<g>[y] shakes this hard (2.5 g by default), along y (up and down) with y; S0 stops
                 V<db>          room noise at about this many dB SPL; Q toggles someone blowing on the mic (Q2: close up, overloading it)
                 U<bpm>         music on the pretend mic (kick, snare, hats, a bass line) at this tempo; U0 stops it;
                 U<bpm>r        hip-hop: syncopated kicks with gaps, a half-time snare, uneven hats, chords (add D: rap)
                 D              toggles someone talking near the mic: syllables at no steady pace (no beat to dance to)
                 J<pct>[+]      the battery level, + while charging
                 p<id>          side button (0 PWR, 1 BOOT)
                 n<name>        pin a mood by name, e.g. nrage (A turns the automatic mood back on)
                 T<hours>       jump the wall clock (the run starts at 2026-10-10 12:00 UTC)
                 F / G          feed / play
                 o<app>         open an app (clock, care, noise, timer, torch, ball, notify, music, camera)
                 P / B          top (PWR) / bottom (BOOT) side button; B<s> holds the bottom one s seconds
                                (on Pip: B1 feeds him, "B 0.2:B3" - a tap, then held - washes him)
                 c<v>           how clean he is: 0 filthy .. 1 just washed (dust shows under 0.55)
                 y<v>           his energy: 0 worn out .. 1 rested
                 s<secs>        the sleep timer (s0: the screen never sleeps on its own)
                 q<v>           trust: 0 timid (flinches) .. 1 bold (says hi himself); a fresh pet has 0.5
                 v<v>           love: 0 lonely .. 1
                 e<v>           the grudge: what is left of his anger, 0 none .. 1 seething (status grudge=)
                 H<n>           theme: 0 Стандартная, 1 Ретро, 2 Барби
                 K<knob>=<v>    set a mood-tree knob (pet_knobs[] in ui/pet.c), e.g. Kmeet_shy=1
                 R[name]        a rare card now (stoned, pinkeye, allergic, frozen, overheated, smitten, hypnotized, hyper,
                                zombie; without a name picked by the day's weights)
                 L<0|1|2> N C<0|1> M<0|1>  the fake iPhone: link off/pairing/on, a notification (connected or not), a call, music
                 l<0|1|2>       the fake Android phone instead: link off/pairing/on (notifications and music: not supported)
                 i<dBm>         the phone link's signal the locator reads (the Bluetooth page too), e.g. i-75; i0 none
                 b<d>,<m>       the owner's birthday (the panel's "Дни рождения"; b0,0 off): runs start 10 October
                 t<0|1>         t0: the clock was never set since power-on (the clock app says so), t1 set
                 Z              "Обновление" on top: the panel is about to flash
                 x<0|1>         Flappy Pip's autopilot (debug `flauto`): it plays itself
                 z              the battery ran flat and he is switched on again (board: under 3.3 V for 30 s, no cable):
                                food and energy lost, the power-on intro, then angry ("drained")
                 X<0|1>         the panel linked (X1: "РЕЖИМ НАСТРОЙКИ" on top, no touch, keys or motion) / gone (X0)
                 E<0|1>         energy saving off/on
                 f<i>,<0|1>     pip_feature: what he notices (PIP_F_*, e.g. f8,0: falls ignored)
                 O              a fresh pet, dormant until a button (as after a clean flash; runs start met)
                 w<0|1>         a game with him ends: w1 you beat him, w0 he beat you
                 Y<n>           how much care he needs: 0 Неприхотливый, 1 Уравновешенный (default), 2 Требовательный
                 j<n>           how much he talks (the panel's "Болтливость"): 0 quiet, 1 as he is (default), 2 a chatterbox
                 I<n>           eye colours (0 standard .. 9, pip_eyes; on the board only the panel sets them)
                 g<layout>      the menu's layout as the panel sets it (shell_menu_set): ';' between lines, '_' a space,
                                e.g. gput_top_tools_care;name_tools_Мелочи; "g" alone: the default menu
   .png saves the last frame. Prints pip's status at the end. --wav out.wav also records Pip's voice (16 kHz mono). */
#include "fakephone.h"
#include "phone.h"
#include "mic.h"
#include "lvgl.h"
#include "pet.h"
#include "pip.h"
#include "shell.h"
#include "sprite.h"
#include "theme.h"
#include "voice.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32 /* Windows: ffmpeg through the CRT's pipes, in binary mode (text mode would mangle the frames) */
#define popen _popen
#define pclose _pclose
#define PIPE_W "wb"
#define strtok_r strtok_s
#else
#define PIPE_W "w"
#endif

#define S 466

static uint16_t fb[S * S];
static uint8_t rgb[S * S * 3];
static struct { bool down; int x, y; } ptr;
static struct { int n; int16_t x[5], y[5]; bool palm; } fing; /* h: every finger (pip_fingers each frame) */
static float acc[3] = {0, 1, 0}; /* held upright, screen facing you */
static float boot_hold; /* s until the held bottom key is let go */
static bool shaking, walking, shake_y;
static float shake_g = 2.5f; /* S<g>[y]: how hard, and along y (up and down) rather than x */
static float walk_g; /* W<g>: the bounce of a walk, 0.35 g if not given */
static float room_db = -1; /* V<db>: the pretend mic hears white noise this loud; < 0: no mic */
static bool blowing, blow_close; /* Q2: blown on as the board hears it close up */
static float music_bpm; /* U<bpm>: a song playing near the mic (0: none) */
static char music_style; /* U<bpm>r: hip-hop - syncopated kicks with gaps, half-time snare, uneven hats, chords, plucks,
                            someone rapping over it (a beat that is there but never plain) */
static uint32_t step_hash(uint32_t k) { k ^= k >> 16, k *= 0x7FEB352Du, k ^= k >> 15, k *= 0x846CA68Bu, k ^= k >> 16; return k; }
static bool talking;    /* D: someone talking */

/* the pretend microphone: white noise for the room, plus loud low rumble while someone blows */
static void fake_mic(float dt)
{
    if (room_db < 0) return;
    int mode = shell_mic(); /* as the board: nobody listening, the mic is off */
    if (!mode) { if (mic_on()) mic_stop(); return; }
    mic_listen(mode == 2);
    static int16_t buf[4096];
    static float rumble;
    static double t; /* s of pretend sound so far */
    static float syl_left, syl_f, gap_left = 0.2f; /* talking: the syllable sounding now, and the pause after it */
    int n = (int)(dt * 16000);
    n = n > 4096 ? 4096 : n;
    float a = powf(10, (room_db - 96) / 20) * 1.732f * 32768; /* uniform noise of that RMS */
    for (int i = 0; i < n; i++, t += 1.0 / 16000) {
        float x = a * (rand() / (float)RAND_MAX * 2 - 1);
        rumble += ((rand() / (float)RAND_MAX * 2 - 1) * 30000 - rumble) * 0.04f; /* noise low-passed near 100 Hz */
        if (blowing && blow_close) { /* the board's mic close up: overloaded, broadband (~-5 dBFS) */
            x += (rand() / (float)RAND_MAX * 2 - 1) * 30000 + rumble * 0.5f;
            x = x > 32767 ? 32767 : x < -32768 ? -32768 : x;
        } else if (blowing) x += rumble * 2.5f;
        if (music_bpm > 0 && music_style == 'r') {
            double six = 15.0 / music_bpm; /* a sixteenth */
            uint32_t k = (uint32_t)(t / six), st = k % 16, bar = k / 16;
            float p = (float)fmod(t, six);
            static const uint16_t kicks[2] = {1u << 0 | 1u << 6 | 1u << 10, 1u << 0 | 1u << 3 | 1u << 11 | 1u << 14};
            bool kick_here = kicks[bar % 2] >> st & 1 && step_hash(k * 7) % 5; /* now and then one left out */
            float kick = kick_here ? expf(-p * 16) * sinf(6.2832f * (50 + 80 * expf(-p * 30)) * p) : 0;
            float snare = st == 8 ? expf(-p * 20) * (rand() / (float)RAND_MAX * 2 - 1) : 0;
            float hat = step_hash(k) % 10 < 6 ? expf(-p * 90) * (rand() / (float)RAND_MAX * 2 - 1) * (0.15f + 0.2f * (step_hash(k + 99) % 3) / 2) : 0;
            static const float roots[4] = {220, 196, 174.6f, 164.8f};
            float root = roots[(bar / 2) % 4], hold = (float)fmod(t, 8 * six);
            float chord = 0.12f * expf(-hold * 1.5f) * (sinf(6.2832f * root * (float)t) + sinf(6.2832f * root * 1.26f * (float)t) + sinf(6.2832f * root * 1.5f * (float)t));
            float pluck = step_hash(k * 13) % 10 < 4 ? 0.25f * expf(-p * 12) * sinf(6.2832f * root * (2 + step_hash(k) % 3) * (float)t) : 0;
            x += 6000 * (0.9f * kick + 0.55f * snare + hat + chord + pluck);
        } else if (music_bpm > 0) { /* a beat at about 75 dB: kick on every beat, snare on 2 and 4, hats on the eighths, a bass line */
            double beat = 60.0 / music_bpm, pos = fmod(t, beat), bar = fmod(t, 4 * beat);
            int n4 = (int)(bar / beat);
            float kick = expf(-(float)pos * 18) * sinf(6.2832f * (55 + 90 * expf(-(float)pos * 30)) * (float)pos);
            float snare = n4 % 2 ? expf(-(float)pos * 25) * (rand() / (float)RAND_MAX * 2 - 1) : 0;
            double hp = fmod(t, beat / 2);
            float hat = expf(-(float)hp * 80) * (rand() / (float)RAND_MAX * 2 - 1) * 0.3f;
            static const float bass_notes[4] = {55, 55, 65.4f, 49};
            float bass = 0.35f * sinf(6.2832f * bass_notes[n4] * (float)t) * (0.6f + 0.4f * expf(-(float)pos * 4));
            x += 6000 * (0.9f * kick + 0.6f * snare + hat + bass);
        }
        if (talking) { /* a voice: syllables of 80..250 ms, pauses of 50..400 ms, all at random - no pace to keep */
            if (syl_left > 0) {
                syl_left -= 1.0f / 16000;
                float ph = (float)fmod(t * syl_f, 1.0);
                x += 16000 * fminf(1, syl_left * 30) * (0.6f * sinf(6.2832f * ph) + 0.3f * sinf(12.566f * ph) + 0.15f * (rand() / (float)RAND_MAX * 2 - 1));
            } else if ((gap_left -= 1.0f / 16000) <= 0) {
                syl_left = 0.08f + 0.17f * rand() / (float)RAND_MAX, syl_f = 140 + 120 * rand() / (float)RAND_MAX;
                gap_left = 0.05f + 0.35f * rand() / (float)RAND_MAX;
            }
        }
        buf[i] = (int16_t)fmaxf(-32768, fminf(32767, x));
    }
    mic_feed(buf, n, 16000);
}
static float sim_wet(void) { float bt, w; int st, go; pip_blow_info(&bt, &st, &go, &w); return w; } /* how wet he is, for the status */
static double clock_jump; /* s */
#define START 1791633600LL /* 2026-10-10 12:00 UTC: runs are reproducible */

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) { (void)a; (void)px; lv_display_flush_ready(d); }

static void read_cb(lv_indev_t *i, lv_indev_data_t *d)
{
    (void)i;
    d->point.x = ptr.x;
    d->point.y = ptr.y;
    d->state = ptr.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* RGB565 (byte-swapped, as the board's panel takes it) -> RGB24, with the round glass masked and a grey bezel around it */
static void to_rgb(void)
{
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float dx = x + 0.5f - S / 2.0f, dy = y + 0.5f - S / 2.0f;
            float in = pip_display_on() ? fminf(fmaxf(S / 2.0f - sqrtf(dx * dx + dy * dy) + 0.5f, 0), 1) : 0; /* off: all bezel-dark */
            uint16_t c = (uint16_t)(fb[y * S + x] << 8 | fb[y * S + x] >> 8);
            float r = ((c >> 11) & 31) * 255 / 31, g = ((c >> 5) & 63) * 255 / 63, b = (c & 31) * 255 / 31;
            uint8_t *o = &rgb[(y * S + x) * 3];
            o[0] = (uint8_t)(r * in + 42 * (1 - in));
            o[1] = (uint8_t)(g * in + 42 * (1 - in));
            o[2] = (uint8_t)(b * in + 48 * (1 - in));
        }
}

static void run_event(const char *ev)
{
    int x, y;
    if (ev[0] == 'k') pip_key(ev[1]);
    else if (ev[0] == 'u') ptr.down = false;
    else if (ev[0] == 'S') { /* S toggles; S<g>[y]: on, this hard, along y (up and down) instead of x */
        shake_g = ev[1] ? (float)atof(ev + 1) : 2.5f, shake_y = strchr(ev, 'y') != NULL;
        shaking = ev[1] ? shake_g > 0 : !shaking;
    }
    else if (ev[0] == 'h') { /* h<x>,<y>,... fingers; h0 none; hP a palm */
        fing.n = 0, fing.palm = ev[1] == 'P';
        if (!fing.palm) {
            const char *c = ev + 1;
            int v[10], k = 0;
            while (k < 10 && *c) { v[k++] = atoi(c); while (*c && *c != ',') c++; if (*c) c++; }
            for (int i = 0; i + 1 < k; i += 2) fing.x[fing.n] = (int16_t)v[i], fing.y[fing.n] = (int16_t)v[i + 1], fing.n++;
        }
        if (fing.n >= 1) ptr.x = fing.x[0], ptr.y = fing.y[0], ptr.down = true;
        else if (fing.palm) ptr.x = 233, ptr.y = 233, ptr.down = true;
        else ptr.down = false;
    }
    else if (ev[0] == 'V') room_db = (float)atof(ev + 1);
    else if (ev[0] == 'Q') blowing = !blowing, blow_close = ev[1] == '2', room_db = room_db < 0 ? 30 : room_db;
    else if (ev[0] == 'U') music_bpm = (float)atof(ev + 1), music_style = ev[strlen(ev) - 1], room_db = room_db < 0 ? 30 : room_db;
    else if (ev[0] == 'D') talking = !talking, room_db = room_db < 0 ? 30 : room_db;
    else if (ev[0] == 'J') shell_battery(atoi(ev + 1), strchr(ev, '+') != NULL);
    else if (ev[0] == 'W') walking = !walking, walk_g = (float)atof(ev + 1);
    else if (ev[0] == 'p') shell_button(atoi(ev + 1)), shell_button_up(atoi(ev + 1));
    else if (ev[0] == 'n' && pip_find_mood(ev + 1) >= 0) pip_set_mood(pip_find_mood(ev + 1));
    else if (ev[0] == 'A') pip_auto(true);
    else if (ev[0] == 'o') shell_open(ev + 1);
    else if (ev[0] == 'B') { /* pressed, and let go now or after B<s> seconds */
        shell_button(PIP_BTN_BOOT);
        if ((boot_hold = (float)atof(ev + 1)) <= 0) shell_button_up(PIP_BTN_BOOT);
    }
    else if (ev[0] == 'P') shell_button(PIP_BTN_PWR), shell_button_up(PIP_BTN_PWR);
    else if (ev[0] == 'c') ((pet_t *)pip_pet())->clean = (float)atof(ev + 1); /* how clean he is, 0 filthy .. 1 */
    else if (ev[0] == 'y') ((pet_t *)pip_pet())->energy = (float)atof(ev + 1); /* how rested, 0 worn out .. 1 */
    else if (ev[0] == 's') pip_power(atoi(ev + 1)); /* the sleep timer, s (0: the screen never sleeps) */
    else if (ev[0] == 'q') ((pet_t *)pip_pet())->trust = (float)atof(ev + 1); /* trust, 0 timid .. 1 bold */
    else if (ev[0] == 'v') ((pet_t *)pip_pet())->love = (float)atof(ev + 1); /* love, 0 lonely .. 1 */
    else if (ev[0] == 'e') ((pet_t *)pip_pet())->grudge = (float)atof(ev + 1); /* what is left of his anger, 0 .. 1 */
    else if (ev[0] == 'Z') shell_updating();
    else if (ev[0] == 'X') shell_setup(ev[1] != '0');
    else if (ev[0] == 'x') apps_flight_auto(ev[1] != '0');
    else if (ev[0] == 'z') pip_drained(), pip_drained_wake(), pip_wake_up(); /* ran flat, then switched on again */
    else if (ev[0] == 'E') pip_eco(atoi(ev + 1) != 0);
    else if (ev[0] == 'H') shell_set_theme(atoi(ev + 1));
    else if (ev[0] == 'I') pip_eyes(atoi(ev + 1));
    else if (ev[0] == 'Y') pip_care(atoi(ev + 1));
    else if (ev[0] == 'j') pip_chat(atoi(ev + 1));
    else if (ev[0] == 'w') pip_game(atoi(ev + 1) ? PIP_GAME_LOST : PIP_GAME_WON);
    else if (ev[0] == 'O') pip_forget();
    else if (ev[0] == 'g') { /* the panel's menu layout: ';' between lines, '_' for a space ("g" alone: the default) */
        char t[512];
        snprintf(t, sizeof t, "%s", ev + 1);
        for (char *c = t; *c; c++) *c = *c == ';' ? '\n' : *c == '_' ? ' ' : *c;
        shell_menu_set(t);
    }
    else if (ev[0] == 'L' || ev[0] == 'N' || ev[0] == 'C' || ev[0] == 'M') fake_phone(ev[0], atoi(ev + 1)); /* the fake iPhone */
    else if (ev[0] == 'l') fake_phone('A', atoi(ev + 1)); /* the fake Android phone */
    else if (ev[0] == 'i') fake_phone_rssi(atoi(ev + 1)); /* the phone's signal, dBm, as the locator reads it */
    else if (ev[0] == 'b' && sscanf(ev + 1, "%d,%d", &x, &y) == 2) pip_birthday(x, y); /* b10,10: the owner's birthday (b0,0 off) */
    else if (ev[0] == 't') shell_clock_known(atoi(ev + 1) != 0); /* t0: no phone or panel told the time since power-on */
    else if (ev[0] == 'T') clock_jump += atof(ev + 1) * 3600;
    else if (ev[0] == 'F') pip_action(PIP_ACT_FEED);
    else if (ev[0] == 'G') pip_action(PIP_ACT_PLAY);
    else if (ev[0] == 'R') pet_rare_now(pip_pet(), 0, pip_find_mood(ev + 1));
    else if (ev[0] == 'K' && strchr(ev, '=')) {
        char name[32];
        snprintf(name, sizeof name, "%.*s", (int)(strchr(ev, '=') - ev - 1), ev + 1);
        float *k = pet_knob(name);
        if (k) *k = (float)atof(strchr(ev, '=') + 1);
        else fprintf(stderr, "no knob %s\n", name);
    }
    else if (ev[0] == 'a' && sscanf(ev + 1, "%f,%f,%f", &acc[0], &acc[1], &acc[2]) == 3) {}
    else if (ev[0] == 'f' && sscanf(ev + 1, "%d,%d", &x, &y) == 2) pip_feature(x, y != 0);
    else if ((ev[0] == 'd' || ev[0] == 'm') && sscanf(ev + 1, "%d,%d", &x, &y) == 2) {
        ptr.x = x;
        ptr.y = y;
        ptr.down = ev[0] == 'd' || ptr.down;
    } else fprintf(stderr, "bad event: %s\n", ev);
}

static bool trace;
int main(int argc, char **argv)
{
    const char *out = NULL, *wav = NULL;
    char *events = NULL;
    float secs = 4;
    int fps = 30;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--out")) out = argv[i + 1];
        else if (!strcmp(argv[i], "--secs")) secs = atof(argv[i + 1]);
        else if (!strcmp(argv[i], "--fps")) fps = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--events")) events = argv[i + 1];
        else if (!strcmp(argv[i], "--wav")) wav = argv[i + 1];
        else if (!strcmp(argv[i], "--sounds") && argc == 3) break; /* below */
        else if (!strcmp(argv[i], "--trace")) { trace = true; i--; } /* every change of face: when, what, why */
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (argc == 2 && !strcmp(argv[1], "--list")) { /* every mood, and fail if one has no table entry or fewer than two sounds */
        int bad = 0;
        for (int i = 0; i < PIP_MOOD_COUNT; i++) {
            if (!pip_mood_name(i)) { fprintf(stderr, "mood %d has no entry\n", i); return 1; }
            if (i && pip_mood_sounds(i) < 2) { fprintf(stderr, "mood %s has %d sounds (two or more: pip.c mood_sound)\n", pip_mood_name(i), pip_mood_sounds(i)); bad = 1; }
            puts(pip_mood_name(i));
        }
        return bad;
    }
    if (argc == 3 && !strcmp(argv[1], "--sounds")) { /* every feeling's sounds to <dir>/<key>.raw (16 kHz mono), and "mood key key..." lines */
        voice_init(16000);
        voice_levels(VOICE_STEPS, VOICE_STEPS);
        lv_init();
        lv_display_t *d0 = lv_display_create(S, S); /* pip_create wants a screen (it makes the feelings' values) */
        lv_display_set_color_format(d0, LV_COLOR_FORMAT_RGB565_SWAPPED);
        lv_display_set_buffers(d0, fb, NULL, sizeof fb, LV_DISPLAY_RENDER_MODE_DIRECT);
        lv_display_set_flush_cb(d0, flush_cb);
        shell_create();
        static int16_t pcm[16000 * 6];
        for (int i = 0; i < PIP_MOOD_COUNT; i++) {
            printf("%s", pip_mood_name(i));
            for (int k = 0, n = pip_mood_sounds(i); k < n; k++) {
                bool babble;
                const char *v = pip_mood_say(i, k, &babble);
                char key[64], path[600];
                if (babble) snprintf(key, sizeof key, "%s_%d", pip_mood_name(i), k);
                else snprintf(key, sizeof key, "%s", v);
                int m = 0;
                while (voice_busy() && m < 16000 * 5) voice_render(pcm + m, 160), m += 160;
                snprintf(path, sizeof path, "%s/%s.raw", argv[2], key);
                FILE *f = fopen(path, "wb");
                if (f) fwrite(pcm, 2, (size_t)m, f), fclose(f);
                printf(" %s", key);
            }
            printf("\n");
        }
        return 0;
    }
    if (!out) { fprintf(stderr, "usage: pipsim --out file.png|gif|mp4 [--secs N] [--fps N] [--events ...]\n"); return 2; }

    lv_init();
    lv_rand_set_seed(1);
    voice_init(16000);
    static int16_t audio[16000 * 120];
    int audio_n = 0;     /* samples recorded (the WAV) */
    int64_t audio_done = 0; /* samples rendered, recorded or not */
    lv_display_t *disp = lv_display_create(S, S);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED); /* as the board draws: the same code paths */
    lv_display_set_buffers(disp, fb, NULL, sizeof fb, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_cb);
    shell_create();
    fake_phone_init();
    shell_battery(82, false); /* a pretend battery */
    shell_time(START, 0);
    pip_meet_quietly(); /* runs start with a pet that knows you; O for the first meeting */

    const char *ext = strrchr(out, '.');
    bool png = ext && !strcmp(ext, ".png");
    const char *vf = ext && !strcmp(ext, ".gif") ? "-vf \"split[a][b];[a]palettegen[p];[b][p]paletteuse\""
                     : png                      ? ""
                                                : "-pix_fmt yuv420p";
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgb24 -s %dx%d -r %d -i - %s \"%s\"", S, S,
             fps, vf, out);
    FILE *ff = popen(cmd, PIPE_W);
    if (!ff) { perror("ffmpeg"); return 1; }

    int frames = (int)lroundf(secs * fps);
    char *save = NULL, *ev = events ? strtok_r(events, " ", &save) : NULL;
    uint32_t now = 0;
    double busy = 0;
    for (int f = 0; f <= frames; f++) {
        uint32_t t = (uint32_t)((double)f * 1000 / fps);
        float dt = (t - now) / 1000.0f, ts = t / 1000.0f;
        lv_tick_inc(t - now);
        now = t;
        while (ev && atof(ev) * 1000 <= now) {
            run_event(strchr(ev, ':') ? strchr(ev, ':') + 1 : ev);
            ev = strtok_r(NULL, " ", &save);
        }
        float sh = shaking ? shake_g * sinf(ts * 2 * 3.14159f * 7) : 0, sx = shake_y ? 0 : sh;
        float wy = walking ? (walk_g ? walk_g : 0.35f) * sinf(ts * 2 * 3.14159f * 1.8f) : 0; /* along gravity: held upright */
        pip_fingers(fing.palm ? 0 : fing.n, fing.x, fing.y, fing.palm);
        shell_imu(acc[0] + sx, acc[1] + wy + (shake_y ? sh : 0), acc[2], dt > 0 ? dt : 1.0f / fps);
        if (boot_hold > 0 && (boot_hold -= dt) <= 0) shell_button_up(PIP_BTN_BOOT);
        fake_mic(dt);
        shell_time(START + (int64_t)(ts + clock_jump), 0);
        clock_t c0 = clock();
        lv_timer_handler();
        int want = (int)((int64_t)t * 16000 / 1000 - audio_done); /* the voice, in step with the frames */
        if (want > 0 && audio_n + want <= (int)(sizeof audio / 2)) voice_render(audio + audio_n, want), audio_n += want;
        else if (want > 0) { /* past the recording's two minutes: the voice still plays out (unheard), or it would stay
                                "busy" for ever and he would never speak again in a long run */
            static int16_t spill[4096];
            for (int left = want; left > 0; left -= 4096) voice_render(spill, left < 4096 ? left : 4096);
        }
        if (want > 0) audio_done += want;
        busy += (double)(clock() - c0) / CLOCKS_PER_SEC;
        if (trace) {
            static char was[96];
            char now_s[96];
            pip_info_t ti;
            pip_get_info(&ti);
            pet_why_t w;
            pet_mood_why(pip_pet(), 0, &w);
            snprintf(now_s, sizeof now_s, "%s %s", ti.mood, ti.reaction);
            if (strcmp(now_s, was)) printf("trace %7.1f  %-14s %-20s why %d.%d\n", ts, ti.mood, ti.reaction, w.kind, w.which), strcpy(was, now_s);
        }
        if (!png || f == frames) {
            to_rgb();
            fwrite(rgb, 1, sizeof rgb, ff);
        }
    }
    if (wav) { /* 16-bit mono WAV */
        FILE *f = fopen(wav, "wb");
        uint32_t data = (uint32_t)audio_n * 2, riff = 36 + data, fmt = 16, rate = 16000, bps = 32000;
        uint16_t pcmfmt = 1, ch = 1, align = 2, bits = 16;
        fwrite("RIFF", 1, 4, f), fwrite(&riff, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f), fwrite(&fmt, 4, 1, f);
        fwrite(&pcmfmt, 2, 1, f), fwrite(&ch, 2, 1, f), fwrite(&rate, 4, 1, f), fwrite(&bps, 4, 1, f);
        fwrite(&align, 2, 1, f), fwrite(&bits, 2, 1, f), fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f);
        fwrite(audio, 2, (size_t)audio_n, f);
        fclose(f);
    }
    pip_info_t in;
    pip_get_info(&in);
    printf("%s: %d frames, UI work %.2f ms/frame on this computer\n", out, frames + 1, busy * 1000 / (frames + 1));
#ifdef FACE_PROFILE
    {
        extern long face_prof[16][2], face_why[4];
        static const char *const names[16] = {"skip", "white", "rim", "full", "clear", "blush", "eyes", "out", "flat", "blocks", "plines", "rowset", "nothing", "f.flat", "f.cresc", "f.mixed"};
        printf("prof (pixels a frame):");
        for (int k = 0; k < 16; k++) if (face_prof[k][1] && k != 4 && k != 5 && k != 6 && k != 7 && k != 11) printf(" %s %ld", names[k], face_prof[k][1] / (frames + 1));
        printf("\nsprites: %ld px a frame", sprite_px_drawn() / (frames + 1));
        printf("\nslow pupil-box pixels a frame: no lines %ld, off span %ld, smile ring %ld, block test %ld\n", face_why[0] / (frames + 1),
               face_why[1] / (frames + 1), face_why[2] / (frames + 1), face_why[3] / (frames + 1));
    }
#endif
    printf("pet: hour=%d days=%d food=%.2f energy=%.2f fun=%.2f love=%.2f walk=%.2f day_mood=%.2f\n", in.hour, in.days, in.food,
           in.energy, in.fun, in.love, in.walk, in.day_mood);
    pet_why_t why;
    int base = pet_mood_why(pip_pet(), 0, &why);
    printf("status: mood=%s reaction=%s steps=%d face_down=%d asleep=%d auto=%d screen=%s theme=%d eyes=%d care=%d met=%d phone=%s shots=%d base=%s why=%d.%d display=%d eco=%d mic=%d clean=%.2f washing=%d voices=%d music=%.2f bpm=%.0f dead=%d trust=%.3f grudge=%.3f fun=%.3f love=%.3f food=%.3f snake=%s repeat=%s flight=%s locate=%s wet=%.2f chat=%d %s\n",
           in.mood, in.reaction, in.steps, in.face_down, in.asleep, in.auto_mood, shell_where(), theme_index(), pip_eyes_get(), pip_care_get(), !pip_unmet(),
           *fake_phone_last() ? fake_phone_last() : "-", fake_phone_shots(), pip_mood_name(base), why.kind, why.which, pip_display_on(), pip_eco_on(), pip_mic_wanted(),
           in.clean, in.washing, voice_started(), mic_music(), mic_bpm(), pip_dead_days(), ((const pet_t *)pip_pet())->trust, ((const pet_t *)pip_pet())->grudge,
           ((const pet_t *)pip_pet())->fun, ((const pet_t *)pip_pet())->love, ((const pet_t *)pip_pet())->food, apps_snake(), apps_repeat(), apps_flight(), phone_locate_status(), sim_wet(), pip_chat_level(), fake_link_report());
    return pclose(ff) ? 1 : 0;
}
