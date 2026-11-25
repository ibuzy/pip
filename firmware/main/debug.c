/* The debug link over USB: a status line a second and the day's history out, text commands in.
   tools/pipdebug.py talks to it. Lines out start with '@' so they stand out of the log:
     @S {json}          now: mood, why, needs, valence, features, the worst frame of the last 5 s (fw)...
     @H t mood f e fu l w day v asleep why   one history row (a minute each, the last 24 h; kept in flash)
     @N a,b,c           mood names, in order
     @K name value min max   one knob of the mood tree (pet_knobs[]), a line each, then
     @T {json}          the urges [need, line, mood, other face]
     @A db/low/zc/floor/on ...   with "mictrace 1": every mic block (25 ms) of the last second, USB only: dBFS, % low,
                        zero crossings a second / 40, the room's level, 1 heard as breath + 2 blowing (tuning breath)
   @S "bl": [loudest dBFS, its low share, share blowing, his go of breaths (s), its step, in a go, wet, quietest, room, spread, zcr]
   why = [kind, which, value]: the branch of the tree that picked the mood (ui/pet.h pet_mood_why).
   Commands in: mood <i> | auto | react <i> <s> | need <0-5> <v> | day <v> | annoy <v> | grudge <v> | urge <i> <line>
                set <knob> <v> | time <unix> [tz min] | bday <d> <m> | feat <f> <0|1> | calls <0|1> | die | eco <0|1> | abright <0|1> | bat [clear] | demo <s> [i ...] | hist | names | tune
                open <app> | home | btn <0|1> | meet | feed | play | sleep | wake | mictrace <0|1> | rare [mood] | game <0 won|1 lost|2 record> */
#include "board.h"
#include "esp_pm.h"
#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mic.h"
#include "pet.h"
#include "pip.h"
#include "phone.h"
#include "shell.h"
#include "theme.h"
#include "voice.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HIST_N (24 * 60)
typedef struct { int32_t t; uint8_t mood, need[5], why; int8_t day, v; uint8_t asleep, tag[2]; } hist_t; /* 16 bytes */
#define HIST_TAG(h) ((h).tag[0] == 'P' && (h).tag[1] == 'h') /* ours: the partition may hold another firmware's bytes */
_Static_assert(sizeof(hist_t) == 16, "a flash slot");
static hist_t *hist;
static int hist_n, hist_at;

/* In flash: a ring of 16-byte slots at the start of the "storage" partition, a 4 KB sector erased as the ring
   enters it (once every ~4 h: no wear to speak of). After a restart the newest 24 h come back in order. */
#define RING_SECTORS 6
#define RING_SLOTS (RING_SECTORS * 4096 / 16)
static const esp_partition_t *ring;
static int ring_next;

static void hist_load(void)
{
    if (!(ring = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage"))) return;
    hist_t *all = heap_caps_malloc(RING_SLOTS * sizeof *all, MALLOC_CAP_SPIRAM);
    if (!all) return;
    /* through a small buffer on the stack: read straight into PSRAM, esp_flash_read takes a bounce buffer of the
       largest free internal block (up to 16 KB) - at boot all 7.7 KB left, while Bluetooth was starting (least 64 B) */
    for (int i = 0; i < RING_SLOTS; i += 16) {
        hist_t chunk[16];
        if (esp_partition_read(ring, (size_t)i * sizeof *all, chunk, sizeof chunk) != ESP_OK) { free(all); return; }
        memcpy(all + i, chunk, sizeof chunk);
    }
    int newest = -1;
    for (int i = 0; i < RING_SLOTS; i++)
        if (HIST_TAG(all[i]) && all[i].t > 1735689600 && (newest < 0 || all[i].t > all[newest].t)) newest = i;
    ring_next = newest < 0 ? 0 : (newest + 1) % RING_SLOTS;
    /* the slots before the newest, oldest first, as long as time runs forward */
    int n = 0, start = newest;
    while (newest >= 0 && n + 1 < HIST_N) {
        int prev = (start - 1 + RING_SLOTS) % RING_SLOTS;
        if (n + 1 >= RING_SLOTS || !HIST_TAG(all[prev]) || all[prev].t <= 1735689600 || all[prev].t >= all[start].t) break;
        start = prev, n++;
    }
    if (newest >= 0 && (hist = heap_caps_calloc(HIST_N, sizeof *hist, MALLOC_CAP_SPIRAM)))
        for (int k = 0; k <= n; k++) hist[hist_n++] = all[(start + k) % RING_SLOTS], hist_at = hist_n % HIST_N;
    free(all);
}

static void ring_put(const hist_t *h)
{
    if (!ring || h->t <= 1735689600) return; /* no real time yet: nothing worth keeping */
    if (ring_next % 256 == 0) esp_partition_erase_range(ring, (size_t)ring_next * 16, 4096);
    esp_partition_write(ring, (size_t)ring_next * 16, h, sizeof *h);
    ring_next = (ring_next + 1) % RING_SLOTS;
}
static int demo_s, demo_n, demo_k;
static uint8_t demo_list[PIP_MOOD_COUNT]; /* internal RAM: a byte a mood */
static int64_t demo_next;

static bool mic_tracing; /* "mictrace 1": @A lines */
static bool imu_tracing; /* "imutrace 1": @I lines, ~20 a second - how hard a hand shakes him */
static bool touch_tracing; /* "touchtrace 1": @F lines, ~20 a second - the finger as touch_cb sees it (tickling) */
static QueueHandle_t ble_lines; /* commands from the BLE link, run here rather than in the BLE host task */

static void put(const char *s) /* to USB, and to the page over Bluetooth when it is there */
{
    fputs(s, stdout), fflush(stdout);
    if (ble_debug_on()) ble_debug_send(s, (int)strlen(s));
}

void debug_line(char *line)
{
    char l[160];
    snprintf(l, sizeof l, "%s", line);
    if (ble_lines) xQueueSend(ble_lines, l, 0);
}

static const char *status(void) /* the line, made under the UI lock; sent after it (a send may wait on Bluetooth) */
{
    pip_info_t in;
    pip_get_info(&in);
    pet_t *p = pip_pet();
    pet_why_t w;
    pet_mood_why(p, tz_min, &w);
    bool ch;
    int bat = shell_battery_level(&ch);
    unsigned feat = 0;
    for (int f = 0; f < PIP_F_COUNT; f++) feat |= pip_feature_on(f) << f;
    const char *fw_mood, *fw_react;
    float fw = pip_frame_worst(5, &fw_mood, &fw_react); /* the slowest of Pip's frames lately */
    float bl[8], bl_t, wet;
    int bl_stage, bl_go;
    mic_blow_stats(bl);
    pip_blow_info(&bl_t, &bl_stage, &bl_go, &wet);
    static char b[1000]; /* static: the task's stack had ~0.5 KB to spare with it there */
    int k = snprintf(b, sizeof b,
             "@S {\"m\":\"%s\",\"mi\":%d,\"r\":\"%s\",\"auto\":%d,\"sl\":%d,\"n\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f],\"day\":%.3f,"
             "\"an\":%.2f,\"gr\":%.2f,\"v\":%.3f,\"h\":%d,\"why\":[%d,%d,%.3f],\"st\":%d,\"bat\":%d,\"ch\":%d,\"db\":%.0f,\"fms\":%.1f,"
             "\"feat\":%u,\"scr\":\"%s\",\"t\":%lld,\"demo\":%d,\"af\":[%d,%d,%lld],\"rare\":[%d,%lld],\"un\":%.2f,\"wk\":%.1f,"
             "\"wake\":%lld,\"pool\":%d,\"hold\":[%d,%lld],\"fw\":[%.0f,\"%s\",\"%s\"]}\n",
             in.mood, pip_find_mood(in.mood), in.reaction, in.auto_mood, in.asleep, p->food, p->energy, p->fun, p->love, p->walk, p->clean,
             p->day_mood, p->annoy, p->grudge, pet_valence(p), in.hour, w.kind, w.which, w.value, in.steps, bat, ch, mic_on() ? mic_db() : -1,
             in.frame_ms, feat, shell_where(), (long long)time(NULL), demo_s,
             p->after_mood == PET_NO_MOOD ? -1 : p->after_mood, p->after_why, (long long)(p->after_until - p->last), p->rare_mood == PET_NO_MOOD ? -1 : p->rare_mood, (long long)(p->rare_at - p->last),
             p->unseen, p->stride, (long long)(p->woke_at ? p->last - p->woke_at : -1), p->pool, p->hold,
             (long long)(p->hold >= 0 ? p->last - p->hold_since : 0), fw, fw_mood, fw_react);
    /* breath: the loudest mic block this second (dBFS), its low share, the share heard as breath; his go of it, step, wet */
    if (k > 2 && k < (int)sizeof b - 120)
        snprintf(b + k - 2, sizeof b - k + 2, ",\"bl\":[%.1f,%.2f,%.2f,%.1f,%d,%d,%.2f,%.1f,%.1f,%.1f,%.0f]}\n", bl[0], bl[1], bl[2], bl_t,
                 bl_stage, bl_go, wet, bl[3], bl[4], bl[5], bl[6]);
    k = (int)strlen(b); /* the last touch's spiral reading: samples, start-overs, most half turns, sharpest turn, on, loops */
    if (k > 2 && k < (int)sizeof b - 110) snprintf(b + k - 2, sizeof b - k + 2, ",\"sp\":\"%s\"}\n", pip_spiral_note());
    k = (int)strlen(b); /* his age for the panel's top line: the day of his life, 0 not woken yet (dormant), -1 dead */
    if (k > 2 && k < (int)sizeof b - 20)
        snprintf(b + k - 2, sizeof b - k + 2, ",\"age\":%d}\n", pip_dead_days() ? -1 : !p->met ? 0 : pet_age_days(p, tz_min));
    return b;
}

static void hist_add(void)
{
    if (!hist && !(hist = heap_caps_calloc(HIST_N, sizeof *hist, MALLOC_CAP_SPIRAM))) return;
    pip_info_t in;
    pip_get_info(&in);
    pet_t *p = pip_pet();
    pet_why_t w;
    pet_mood_why(p, tz_min, &w);
    hist_t *h = &hist[hist_at];
    *h = (hist_t){(int32_t)time(NULL), (uint8_t)pip_find_mood(in.mood), {0}, (uint8_t)(w.kind * 32 + (w.which & 31)), /* kind 0..7, which 0..31: aftertastes go up to 18 */
                  (int8_t)(p->day_mood * 100), (int8_t)(pet_valence(p) * 100), p->asleep, {'P', 'h'}};
    const float n[5] = {p->food, p->energy, p->fun, p->love, p->walk};
    for (int i = 0; i < 5; i++) h->need[i] = (uint8_t)(n[i] * 255);
    ring_put(h);
    hist_at = (hist_at + 1) % HIST_N;
    if (hist_n < HIST_N) hist_n++;
}

static void hist_dump(void)
{
    char b[96];
    for (int k = 0; k < hist_n; k++) {
        hist_t *h = &hist[(hist_at - hist_n + k + HIST_N) % HIST_N];
        snprintf(b, sizeof b, "@H %ld %d %d %d %d %d %d %d %d %d %d\n", (long)h->t, h->mood, h->need[0], h->need[1], h->need[2],
                 h->need[3], h->need[4], h->day, h->v, h->asleep, h->why);
        put(b);
        if (k % 32 == 31) vTaskDelay(1); /* let the USB drain */
    }
    put("@H end\n");
}

static void names(void)
{
    const int cap = 2100 + PIP_MOOD_COUNT * 4; /* ~9 bytes a name; over 2 KB: PSRAM, and only while it is sent */
    char *b = malloc((size_t)cap);
    if (!b) return;
    int n = snprintf(b, (size_t)cap, "@N ");
    for (int i = 0; i < PIP_MOOD_COUNT && n < cap - 32; i++) n += snprintf(b + n, (size_t)(cap - n), "%s%s", i ? "," : "", pip_mood_name(i));
    snprintf(b + n, (size_t)(cap - n), "\n");
    put(b);
    free(b);
}

static void tune(void)
{
    char b[400];
    for (int i = 0; i < pet_knob_count; i++) {
        const pet_knob_t *k = &pet_knobs[i];
        snprintf(b, sizeof b, "@K %s %.4g %.4g %.4g\n", k->name, *k->v, k->min, k->max);
        put(b);
    }
    int n = snprintf(b, sizeof b, "@T {\"urges\":[");
    for (int i = 0; i < PET_URGES; i++) {
        const pet_urge_t *u = &pet_tune.urges[i];
        n += snprintf(b + n, sizeof b - n, "%s[%d,%.3f,%d,%d]", i ? "," : "", u->need, u->line, u->mood, u->alt);
    }
    snprintf(b + n, sizeof b - n, "]}\n");
    put(b);
}

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static void config(void) /* @C: everything the settings page shows */
{
    static char b[420];
    int bd, bm;
    pip_birthday_get(&bd, &bm);
    int n = snprintf(b, sizeof b, "@C {\"ver\":\"%s\",\"clk\":%d,\"bday\":[%d,%d],\"power\":%d,\"bright\":%d,\"eco\":%d,\"theme\":%d,\"privacy\":%d,\"calls\":%d,\"eyes\":%d,\"care\":%d,\"chat\":%d,\"abright\":%d,\"dead\":%d,\"sound\":[%d,%d],\"feat\":[",
                     esp_app_get_description()->version, settings_clock_known(), bd, bm, pip_power_after(), settings_brightness(), pip_eco_on(), theme_index(),
                     phone_privacy(), phone_calls(), pip_eyes_get(), pip_care_get(), pip_chat_level(), settings_auto_bright(), pip_dead_days(), voice_pip_level(), voice_ui_level());
    for (int f = 0; f < PIP_F_COUNT; f++) n += snprintf(b + n, sizeof b - n, "%s%d", f ? "," : "", pip_feature_on(f));
    snprintf(b + n, sizeof b - n, "]}\n");
    put(b);
}

/* The menu's layout from the panel, in pieces short enough for a line: "menu new", then "menu put <top|block> ids…"
   and "menu name <id> <text>" gather here, "menu apply" sets them all at once (shell_menu_set, main.c keeps it).
   "menu reset" is the default menu; "menu" alone answers @M with the layout as it is. */
static char *menu_stage;
static void menu_dump(void)
{
    char *b = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    if (!b) return;
    memcpy(b, "@M ", 3);
    ui_lock();
    int n = shell_menu_json(b + 3, 4096 - 5);
    ui_unlock();
    if (n > 0) memcpy(b + 3 + n, "\n", 2), put(b);
    free(b);
}
static void menu_cmd(char *rest)
{
    if (!menu_stage && !(menu_stage = heap_caps_calloc(1, MENU_TEXT_MAX, MALLOC_CAP_SPIRAM))) return;
    size_t len = strlen(menu_stage), end = strlen(rest);
    while (end && (rest[end - 1] == '\r' || rest[end - 1] == ' ')) rest[--end] = 0; /* a terminal's \r */
    while (*rest == ' ') rest++;
    if (!*rest) { menu_dump(); return; }
    if (!strcmp(rest, "new")) menu_stage[0] = 0;
    else if (!strncmp(rest, "put ", 4) || !strncmp(rest, "name ", 5)) {
        size_t k = strlen(rest);
        if (len + k + 2 < MENU_TEXT_MAX) memcpy(menu_stage + len, rest, k), menu_stage[len + k] = '\n', menu_stage[len + k + 1] = 0;
    } else if (!strcmp(rest, "apply") || !strcmp(rest, "reset")) {
        ui_lock();
        shell_menu_set(rest[0] == 'a' ? menu_stage : "");
        ui_unlock();
        menu_stage[0] = 0;
        menu_dump();
    }
}

static void command(char *line)
{
    if (!strncmp(line, "menu", 4) && (!line[4] || line[4] == ' ' || line[4] == '\r')) { menu_cmd(line + 4); return; } /* names keep their spaces */
    char *arg[20];
    int n = 0;
    for (char *t = strtok(line, " \t\r"); t && n < 20; t = strtok(NULL, " \t\r")) arg[n++] = t;
    if (!n) return;
    const char *c = arg[0];
    float a1 = n > 1 ? strtof(arg[1], NULL) : 0, a2 = n > 2 ? strtof(arg[2], NULL) : 0;
    pet_t *p = pip_pet();
    if (!strcmp(c, "hist")) { hist_dump(); return; }
    if (!strcmp(c, "names")) { names(); return; }
    if (!strcmp(c, "tune")) { tune(); return; }
    if (!strcmp(c, "config")) { config(); return; }
    if (!strcmp(c, "pm")) { esp_pm_dump_locks(stdout); return; } /* who keeps the chip awake */
    if (!strcmp(c, "perf")) { /* the benchmark (tools/bench.py): times since the last perf, outside the UI lock; "perf lit" keeps the screen on */
        static char *o; /* in PSRAM, made on the first call: internal RAM is the scarce one */
        if (!o && !(o = heap_caps_malloc(1400, MALLOC_CAP_SPIRAM))) return;
        if (n > 1 && !strcmp(arg[1], "lit")) ui_lock(), pip_keep_lit(), ui_unlock();
        board_perf(o, 1400);
        put(o);
        return;
    }
    if (!strcmp(c, "bat")) {
        if (n > 1 && !strcmp(arg[1], "clear")) batlog_clear();
        if (n > 2 && !strcmp(arg[1], "fake")) batlog_fake((int)a2); /* the panel's chart, tried without waiting an hour */
        batlog_dump(put);
        return;
    }
    ui_lock();
    if (!strcmp(c, "mood") && n > 1) { /* an index or a name; an unknown one does nothing (not another mood) */
        int m = arg[1][0] >= '0' && arg[1][0] <= '9' ? (int)a1 : pip_find_mood(arg[1]);
        if (m >= 0 && m < PIP_MOOD_COUNT) pip_set_mood(m);
    }
    else if (!strcmp(c, "key") && n > 1) pip_key(arg[1][0]); /* as the sim's keys: z a sneeze, x a burp, b a blink */
    else if (!strcmp(c, "wash") && n > 1) pip_wash(a1 != 0); /* the sponge on (1) / off (0), as the bottom key's tap and hold */
    else if (!strcmp(c, "auto")) pip_auto(true), demo_s = 0;
    else if (!strcmp(c, "react") && n > 1 && a1 >= 0 && a1 < PIP_MOOD_COUNT) pip_react((int)a1, n > 2 ? a2 : 3, pip_mood_name((int)a1)); /* out of range read past moods[] */
    else if (!strcmp(c, "need") && n > 2) {
        float *v[PET_NEEDS] = {&p->food, &p->energy, &p->fun, &p->love, &p->walk, &p->clean};
        if (a1 >= 0 && a1 < PET_NEEDS) *v[(int)a1] = clamp01(a2);
    } else if (!strcmp(c, "day") && n > 1) p->day_mood = a1 < -1 ? -1 : a1 > 1 ? 1 : a1;
    else if (!strcmp(c, "annoy") && n > 1) p->annoy = a1 < 0 ? 0 : a1;
    else if (!strcmp(c, "grudge") && n > 1) p->grudge = a1 < 0 ? 0 : a1 > 1 ? 1 : a1;
    else if (!strcmp(c, "urge") && n > 2 && a1 >= 0 && a1 < PET_URGES) pet_tune.urges[(int)a1].line = a2 < 0.01f ? 0.01f : a2;
    else if (!strcmp(c, "set") && n > 2 && pet_knob(arg[1])) *pet_knob(arg[1]) = a2;
    else if (!strcmp(c, "rare")) pet_rare_now(p, tz_min, n > 1 ? pip_find_mood(arg[1]) : -1);
    else if (!strcmp(c, "game") && n > 1 && a1 >= 0 && a1 <= 2) pip_game((int)a1);
    else if (!strcmp(c, "die")) pip_die(); /* a test: he dies now, for good (only a clean flash brings another) */
    else if (!strcmp(c, "feat") && n > 2) pip_feature((int)a1, a2 != 0), settings_save();
    else if (!strcmp(c, "power") && n > 1) pip_power((int)a1), settings_save();      /* seconds to sleep, 0 never */
    else if (!strcmp(c, "eco") && n > 1) pip_eco(a1 != 0), settings_save();              /* energy saving */
    else if (!strcmp(c, "bright") && n > 1) settings_bright((int)a1), settings_save(); /* % */
    else if (!strcmp(c, "abright") && n > 1) settings_set_auto_bright(a1 != 0), settings_save(); /* by the battery */
    else if (!strcmp(c, "privacy") && n > 1) phone_set_privacy((int)a1), settings_save();
    else if (!strcmp(c, "calls") && n > 1) phone_set_calls(a1 != 0), settings_save(); /* calls on the keychain or not */
    else if (!strcmp(c, "eyes") && n > 1) pip_eyes((int)a1), settings_save();          /* eye colours, 0 standard */
    else if (!strcmp(c, "care") && n > 1) pip_care((int)a1), settings_save();          /* how much care he needs, 0..2 */
    else if (!strcmp(c, "chat") && n > 1) pip_chat((int)a1), settings_save();          /* how much he talks, 0..2 */
    else if (!strcmp(c, "theme") && n > 1) shell_set_theme((int)a1);                   /* kept by main.c theme_keep */
    else if (!strcmp(c, "bday") && n > 2) pip_birthday((int)a1, (int)a2), settings_save(); /* the owner's birthday, day month; 0 0 off */
    else if (!strcmp(c, "sound") && n > 2) voice_levels((int)a1, (int)a2); /* how loud his voice and the menu's clicks are, 0..VOICE_STEPS (the Звуки app); main.c sound_keep */
    else if (!strcmp(c, "panel") && n > 1) settings_panel(a1 != 0); /* the panel's heartbeat: "РЕЖИМ НАСТРОЙКИ" while it lasts */
    else if (!strcmp(c, "time") && n > 1) settings_time(strtoll(arg[1], NULL, 10), n > 2 ? (int)a2 : tz_min); /* the panel's clock: unix s, zone in min east of UTC */
    else if (!strcmp(c, "demo")) {
        demo_s = a1 > 0 ? (int)a1 : 0, demo_n = 0, demo_k = 0, demo_next = 0; /* 0 or less: no show (not a mood a loop) */
        for (int i = 2; i < n && demo_n < PIP_MOOD_COUNT; i++) demo_list[demo_n++] = (uint8_t)atoi(arg[i]);
        if (!demo_n) for (int i = 0; i < PIP_MOOD_COUNT; i++) demo_list[demo_n++] = (uint8_t)i;
        if (!demo_s) pip_auto(true);
        else shell_home();
    } else if (!strcmp(c, "open") && n > 1) shell_open(arg[1]);
    else if (!strcmp(c, "home")) shell_home();
    else if (!strcmp(c, "btn") && n > 1) shell_button((int)a1), shell_button_up((int)a1); /* a press, let go at once */
    else if (!strcmp(c, "birth")) pip_birth_show(n > 1 ? (float)a1 : 0); /* his birth again (bench.py), the pet as it is */
    else if (!strcmp(c, "flauto")) apps_flight_auto(n < 2 || a1 != 0); /* Flappy Pip plays itself: a frame-rate bench */
    else if (!strcmp(c, "meet")) pip_meet();
    else if (!strcmp(c, "update")) shell_updating(); /* the panel flashes next */
    else if (!strcmp(c, "dev")) pip_dev(n > 1 ? a1 != 0 : !pip_dev_on()); /* the developer overlay */
    else if (!strcmp(c, "feed")) pip_action(PIP_ACT_FEED);
    else if (!strcmp(c, "play")) pip_action(PIP_ACT_PLAY);
    else if (!strcmp(c, "sleep")) pet_event(p, PET_SLEEP);
    else if (!strcmp(c, "wake")) pet_event(p, PET_WAKE);
    else if (!strcmp(c, "mictrace")) mic_tracing = n < 2 || a1 != 0; /* @A: the mic's blocks every second (tuning breath) */
    else if (!strcmp(c, "touchtrace")) touch_tracing = n < 2 || a1 != 0;
    else if (!strcmp(c, "touchraw")) { extern bool touch_raw_trace; touch_raw_trace = n < 2 || a1 != 0; } /* @R: the panel's whole report, hex, when it changes */
    else if (!strcmp(c, "imutrace")) imu_tracing = n < 2 || a1 != 0; /* @I: energy, its peak, the peak |a|, shake s, shaking */
    ui_unlock();
    if (!strcmp(c, "urge") || !strcmp(c, "set")) tune();
}

static void debug_task(void *arg)
{
    (void)arg;
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = 512;
    /* 32 lines: the panel's apply (settings, then a menu of ~30 lines) must not overrun it while this task waits on a
       Bluetooth send or relays out a menu; 5 KB in PSRAM (internal RAM is the scarce one) */
    ble_lines = xQueueCreateWithCaps(32, 160, MALLOC_CAP_SPIRAM);
    hist_load();
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) vTaskDelete(NULL);
    static char line[200];
    int len = 0;
    int64_t next_status = 0, next_hist = 0;
    for (;;) {
        uint8_t buf[64];
        /* on battery no USB host can be there: no polling of it 20 times a second, the Bluetooth page's lines wake us */
        bool usb = board_usb();
        int r = usb ? usb_serial_jtag_read_bytes(buf, sizeof buf, pdMS_TO_TICKS(50)) : 0;
        for (int i = 0; i < r; i++) {
            if (buf[i] == '\n') line[len] = 0, command(line), len = 0;
            else if (len < (int)sizeof line - 1) line[len++] = (char)buf[i];
        }
        char bl[160];
        if (!usb && xQueueReceive(ble_lines, bl, pdMS_TO_TICKS(1000))) command(bl);
        while (xQueueReceive(ble_lines, bl, 0)) command(bl);
        int64_t now = esp_timer_get_time();
        if (touch_tracing && usb) {
            static char ft[96], fm[40], fl[176]; /* static: the task's stack is tight */
            extern uint8_t touch_raw5; /* main.c: the panel's count byte, bit 7 its own flag */
            ui_lock(), pip_touch_trace(ft, sizeof ft), pip_fingers_trace(fm, sizeof fm), ui_unlock();
            snprintf(fl, sizeof fl, "@F %lld %s | %s raw%02X\n", (long long)(now / 1000), ft, fm, touch_raw5);
            fputs(fl, stdout), fflush(stdout);
        }
        if (imu_tracing && usb) {
            float e, em, am, st;
            int sh;
            static char rn[24], t[96]; /* static: the task's 4 KB stack has ~600 B to spare */
            ui_lock(), snprintf(rn, sizeof rn, "%s", pip_imu_trace(&e, &em, &am, &st, &sh)), ui_unlock();
            snprintf(t, sizeof t, "@I %lld %.2f %.2f %.2f %.2f %d %s\n", (long long)(now / 1000), e, em, am, st, sh, rn);
            fputs(t, stdout), fflush(stdout);
        }
        if (now >= next_status && (usb || ble_debug_on())) { /* nobody listening: nothing made. The pet and the face as one moment, sent with the lock let go: the BLE host task
                                     takes the UI lock too, and a full notification queue waits on it */
            next_status = now + 1000000;
            ui_lock();
            const char *st = status();
            ui_unlock();
            put(st);
            static char *tr; /* breath tuning: the mic's blocks, USB only (PSRAM: internal RAM is the scarce one) */
            if (!mic_tracing) mic_trace(NULL, 0); /* nothing kept */
            else if (tr || (tr = heap_caps_malloc(1400, MALLOC_CAP_SPIRAM))) {
                memcpy(tr, "@A", 2);
                int k = 2 + mic_trace(tr + 2, 1400 - 4);
                tr[k] = '\n', tr[k + 1] = 0;
                fputs(tr, stdout), fflush(stdout);
            }
        }
        if (now >= next_hist && time(NULL) > 1735689600) { next_hist = now + 60000000; ui_lock(), hist_add(), ui_unlock(); }
        if (demo_s && now >= demo_next) { /* the show: one mood after another */
            demo_next = now + (int64_t)demo_s * 1000000;
            ui_lock(), pip_set_mood(demo_list[demo_k++ % demo_n]), ui_unlock();
        }
    }
}

void debug_start(void) { xTaskCreate(debug_task, "debug", 4096, NULL, 3, NULL); }
