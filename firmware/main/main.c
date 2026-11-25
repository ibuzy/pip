/* KeyPet firmware for the Waveshare ESP32-S3-Touch-AMOLED-1.75C.
   The UI (ui/) is the same code as the simulator; this file only wires the board to it. */
#include "bsp/esp-bsp.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_lcd_io_i2c.h"
#include "driver/spi_master.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_private/esp_clk.h"
#include "esp_pm.h"
#include "esp_rom_sys.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "board.h"
#include "pet.h"
#include "cosmos.h"
#include "face.h"
#include "pip.h"
#include "shell.h"
#include "theme.h"
#include "phone.h"
#include "mic.h"
#include "src/display/lv_display_private.h" /* the invalidated areas, for `perf` */
#include "voice.h"

static const char *TAG = "keypet";

#define BOOT_GPIO GPIO_NUM_0
int tz_min = 180; /* UTC+3 until the iPhone tells the real zone (ble.c, Current Time Service) */
#define SAVE_EVERY_S 300

/* Wall clock for the pet. Until the RTC is wired or the phone sets the time, the system clock starts in 1970:
   then Pip simply continues from his last save, without catching up the time he was off. */
static int64_t clock_base;
static int64_t pet_clock(void)
{
    int64_t t = time(NULL);
    return t > 1735689600 ? t : clock_base + esp_timer_get_time() / 1000000; /* after 2025-01-01 = real time */
}

static bool dead_kept; /* "dead" is in NVS: nothing more to save */
static void pet_load(void)
{
    nvs_handle_t h;
    uint8_t buf[256] __attribute__((aligned(8))); /* read as pet_t below: Xtensa faults on misaligned loads */
    size_t n = sizeof buf;
    if (nvs_open("keypet", NVS_READONLY, &h) != ESP_OK) return;
    int16_t dead;
    if (nvs_get_i16(h, "dead", &dead) == ESP_OK) { /* he died: an update does not bring him back, only a clean flash */
        pip_set_dead(dead), dead_kept = true;
        ESP_LOGI(TAG, "Pip is dead (lived %d days)", dead);
    } else if (nvs_get_blob(h, "pet", buf, &n) == ESP_OK && pip_pet_restore(buf, n)) {
        clock_base = ((const pet_t *)buf)->last;
        ESP_LOGI(TAG, "pet restored (%u bytes)", (unsigned)n);
    }
    uint8_t ny[32] __attribute__((aligned(8))); /* New Year's marks, beside the pet (pip_ny_state) */
    size_t nn = sizeof ny;
    if (nvs_get_blob(h, "ny", ny, &nn) == ESP_OK) pip_ny_restore(ny, nn);
    uint8_t drained = 0;
    bool was_drained = nvs_get_u8(h, "drained", &drained) == ESP_OK && drained;
    nvs_close(h);
    if (was_drained) { /* the last power-off was the battery running flat (and only that sets it): once, then gone */
        if (nvs_open("keypet", NVS_READWRITE, &h) == ESP_OK) nvs_erase_key(h, "drained"), nvs_commit(h), nvs_close(h);
        if (!dead_kept) pip_drained_wake();
        ESP_LOGI(TAG, "woke after running flat");
    }
}

/* the theme set on the panel ("theme i"), kept across restarts */
static int theme_saved = -1;
static void theme_load(void)
{
    nvs_handle_t h;
    int8_t v;
    if (nvs_open("keypet", NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_i8(h, "theme", &v) == ESP_OK && v >= 0 && v < TH_COUNT) theme_select(v), theme_saved = v;
    nvs_close(h);
}

/* what notifications may show (phone.h PHONE_PRIV_*): kept in NVS when it changes, read back at boot */
static void privacy_save(int mode)
{
    nvs_handle_t h;
    if (nvs_open("keypet", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_i8(h, "privacy", (int8_t)mode) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

static void calls_save(bool on) /* calls on the keychain or not (Приватность > Звонки) */
{
    nvs_handle_t h;
    if (nvs_open("keypet", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_u8(h, "calls", on) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

static void privacy_load(void)
{
    nvs_handle_t h;
    int8_t v;
    uint8_t c;
    if (nvs_open("keypet", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_i8(h, "privacy", &v) == ESP_OK) phone_set_privacy(v);
        if (nvs_get_u8(h, "calls", &c) == ESP_OK) phone_set_calls(c != 0); /* none yet: calls come */
        nvs_close(h);
    }
    phone_on_privacy(privacy_save);
    phone_on_calls(calls_save);
}

/* the menu's layout set on the panel (shell_menu_set): a text kept as it came, "" = the default menu */
static void menu_save(const char *text)
{
    nvs_handle_t h;
    if (nvs_open("keypet", NVS_READWRITE, &h) != ESP_OK) return;
    if ((*text ? nvs_set_str(h, "menu", text) : nvs_erase_key(h, "menu")) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

static void menu_load(void) /* before shell_create: the list is built in it */
{
    nvs_handle_t h;
    if (nvs_open("keypet", NVS_READONLY, &h) == ESP_OK) {
        size_t n = 0;
        char *t = nvs_get_str(h, "menu", NULL, &n) == ESP_OK && n <= MENU_TEXT_MAX ? malloc(n) : NULL;
        if (t && nvs_get_str(h, "menu", t, &n) == ESP_OK) shell_menu_set(t);
        free(t);
        nvs_close(h);
    }
    shell_menu_on_change(menu_save);
}

static int sound_saved = VOICE_STEPS | VOICE_UI_DEFAULT << 4; /* the Звуки app: his voice's level, the interface's << 4 */
static int eyes_saved; /* the eye colours last saved: the Глаза app changes them on the board too */

/* The owner's settings, kept in NVS: what the panel changes */
static int bright = 60; /* the panel's full brightness, %: by day when it is automatic */
static bool auto_bright;  /* the panel's "Автояркость": follows the battery (auto_level) */
static volatile int bright_shown = 60; /* what the panel is lit at (not counting the dim before sleep) */
static volatile bool bright_changed; /* the panel task applies it */
static volatile bool bright_recalc;  /* a setting changed: work the level out again now */
int settings_brightness(void) { return bright; }
bool settings_auto_bright(void) { return auto_bright; }
void settings_bright(int pct) /* the Яркость app's copy first: the main loop, seeing them differ, takes the app's */
{
    int v = pct < 10 ? 10 : pct > 100 ? 100 : pct;
    shell_brightness(v), bright = v, bright_recalc = true;
}
void settings_set_auto_bright(bool on) { auto_bright = on, bright_recalc = true; }


/* The panel's clock (debug.c "time"): without it the real date comes only from the iPhone (ble.c CTS), and power off
   loses it. The main loop sees the jump past 2025 and rebases a pet that lived on the made-up clock. */
static int tz_saved = -10000;
static volatile int64_t panel_until; /* esp_timer us: the panel counts as linked until then */
#define PANEL_HOLD_US 6000000LL      /* its heartbeat comes every 2 s: three missed (a cable pulled, the tab shut) and it is gone */
void settings_panel(bool on) { panel_until = on ? esp_timer_get_time() + PANEL_HOLD_US : 0; }
bool settings_clock_known(void) { return time(NULL) > 1735689600; }
void settings_time(int64_t unix_s, int tz)
{
    if (unix_s > 1735689600) settimeofday(&(struct timeval){.tv_sec = unix_s}, NULL);
    if (tz >= -12 * 60 && tz <= 14 * 60) tz_min = tz;
    ESP_LOGI(TAG, "time from the panel: %lld, UTC%+d min", (long long)unix_s, tz_min);
    if (tz_min != tz_saved) settings_save();
}

void settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open("keypet", NVS_READWRITE, &h) != ESP_OK) return;
    uint16_t off = 0; /* the ones switched off: a feature added later comes on by itself */
    for (int f = 0; f < PIP_F_COUNT && f < 16; f++) off |= (uint16_t)(!pip_feature_on(f) << f);
    nvs_set_i16(h, "power", (int16_t)pip_power_after());
    nvs_set_u16(h, "featoff", off);
    nvs_set_u8(h, "bright", (uint8_t)bright);
    nvs_set_u8(h, "eco", pip_eco_on());
    nvs_set_i8(h, "privacy", (int8_t)phone_privacy());
    nvs_set_u8(h, "calls", phone_calls());
    nvs_set_u8(h, "eyes", (uint8_t)pip_eyes_get());
    nvs_set_u8(h, "care", (uint8_t)pip_care_get());
    nvs_set_u8(h, "chat", (uint8_t)pip_chat_level());
    nvs_set_u8(h, "abright", auto_bright);
    nvs_set_i16(h, "tz", (int16_t)tz_min), tz_saved = tz_min;
    int bd, bm;
    pip_birthday_get(&bd, &bm);
    nvs_set_u16(h, "bday", (uint16_t)(bd | bm << 8)); /* the owner's birthday, 0: off (as after a flash) */
    nvs_commit(h);
    nvs_close(h);
}

static void settings_load(void)
{
    nvs_handle_t h;
    if (nvs_open("keypet", NVS_READONLY, &h) == ESP_OK) {
        int16_t power;
        uint16_t feat;
        uint8_t b, eco, eyes, care;
        if (nvs_get_i16(h, "power", &power) == ESP_OK) pip_power(power);
        if (nvs_get_u16(h, "featoff", &feat) == ESP_OK)
            for (int f = 0; f < PIP_F_COUNT && f < 16; f++) pip_feature(f, !(feat >> f & 1));
        if (nvs_get_u8(h, "bright", &b) == ESP_OK) bright = b < 10 ? 10 : b > 100 ? 100 : b;
        shell_brightness(bright); /* the Яркость app shows it */
        bright_recalc = true; /* the saved level (and auto) now, not 15 s after power-on */
        if (nvs_get_u8(h, "eco", &eco) == ESP_OK) pip_eco(eco);
        if (nvs_get_u8(h, "eyes", &eyes) == ESP_OK) pip_eyes(eyes); /* none yet (a fresh board): the standard ones */
        eyes_saved = pip_eyes_get();
        if (nvs_get_u8(h, "care", &care) == ESP_OK) pip_care(care); /* none yet: Уравновешенный */
        uint8_t chat;
        if (nvs_get_u8(h, "chat", &chat) == ESP_OK) pip_chat(chat); /* none yet: as he is */
        int16_t tz;
        if (nvs_get_i16(h, "tz", &tz) == ESP_OK) tz_min = tz_saved = tz; /* the last zone the phone or the panel told */
        uint8_t ab, snd;
        if (nvs_get_u8(h, "abright", &ab) == ESP_OK) auto_bright = ab;
        if (nvs_get_u8(h, "sounds", &snd) == ESP_OK) voice_levels(snd & 15, snd >> 4), sound_saved = snd; /* none yet: him full, the interface at 60% */
        uint16_t bday;
        if (nvs_get_u16(h, "bday", &bday) == ESP_OK) pip_birthday(bday & 255, bday >> 8); /* none yet: off */
        nvs_close(h);
    }
}

static void theme_keep(void)
{
    int v = theme_index();
    if (v == theme_saved) return;
    nvs_handle_t h;
    if (nvs_open("keypet", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_i8(h, "theme", (int8_t)v) == ESP_OK) nvs_commit(h), theme_saved = v;
    nvs_close(h);
}

/* the sound levels (the Звуки app, the panel's "sound p u"), kept across restarts */
static void sound_keep(void)
{
    int v = voice_pip_level() | voice_ui_level() << 4;
    if (v == sound_saved) return;
    nvs_handle_t h;
    if (nvs_open("keypet", NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_u8(h, "sounds", (uint8_t)v) == ESP_OK) nvs_commit(h), sound_saved = v;
    nvs_close(h);
}

static void pet_save(void)
{
    nvs_handle_t h;
    size_t n;
    if (dead_kept) return;
    if (nvs_open("keypet", NVS_READWRITE, &h) != ESP_OK) return;
    if (pip_dead()) { /* gone: his data is erased for good, and the board remembers only that he lived so many days */
        nvs_erase_key(h, "pet");
        if (nvs_set_i16(h, "dead", (int16_t)pip_dead_days()) == ESP_OK && nvs_commit(h) == ESP_OK) dead_kept = true;
        nvs_close(h);
        return;
    }
    const void *p = pip_pet_state(&n);
    if (nvs_set_blob(h, "pet", p, n) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}
static void ny_save(void) /* New Year's marks, when they change (a few times a year) */
{
    nvs_handle_t h;
    size_t n;
    if (nvs_open("keypet", NVS_READWRITE, &h) != ESP_OK) return;
    const void *p = pip_ny_state(&n);
    if (nvs_set_blob(h, "ny", p, n) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

/* QMI8658 accelerometer: register setup from the datasheet */
#define QMI_WHO_AM_I 0x00 /* reads 0x05 */
#define QMI_CTRL1 0x02    /* 0x40: register auto-increment, little endian */
#define QMI_CTRL2 0x03    /* accel: bits 6..4 range (001 = ±4 g), bits 3..0 rate (0110 = ~117 Hz) */
#define QMI_CTRL3 0x04    /* gyro: bits 6..4 range (101 = ±512 deg/s), bits 3..0 rate, as the accel's */
#define QMI_CTRL7 0x08    /* bit 0: accel enable, bit 1: gyro enable */
#define QMI_AX_L 0x35     /* accel x, y, z, then gyro x, y, z (0x3B): 12 bytes, little endian */
#define QMI_LSB_PER_G 8192.0f
#define QMI_LSB_PER_DPS 64.0f /* at ±512 deg/s */
/* Calibration knob: board axes -> device axes (x right, y toward the top of the screen, z out of the glass).
   Check on the real board: hold it upright facing you, the log must show about (0, +1, 0). */
static const struct { int src; float sign; } AXIS[3] = {{1, -1}, {0, 1}, {2, 1}}; /* sensor turned 90 deg on the board */

static i2c_master_dev_handle_t qmi;
static float stream_rows_per_ms, te_wait_ms, stream_mb_s;
static volatile int frames_sent; /* frames that reached the panel, for the status line */
static int64_t render_start;
static float render_ms; /* LVGL's own drawing, without waiting for the panel */
static float refr_ms; /* LVGL refresh time, see refr_cb */
static volatile int panel_want = 100, panel_lit = 100; /* the screen: 0 off (asleep), 25 dimmed, 100; the panel task applies it */
/* Dark, nothing polls: the touch controller's INT line and the bottom key wake the chip (GPIO wake-up + an interrupt
   that wakes the task concerned), the accelerometer collects in its FIFO and is read twice a second, the panel's and
   the audio's tasks sleep until told. Lit off his face, the CPU runs at full clock only while drawing or touched. */
static TaskHandle_t lvgl_handle, sens_handle, audio_handle, panel_handle;
/* the panel task's notification bits: a frame to stream (flush_cb), the screen wanted (pm_lit) */
#define PANEL_STREAM 1u
#define PANEL_WAKE 2u
static lv_indev_t *touch_indev;
static bool touch_paused; /* dark: the touch is not polled, its INT line wakes us (touch_gate) */
static volatile bool touch_irq, spk_warm; /* a touch woke us; a sound is likely soon: open the speaker */
static volatile int64_t boost_until;                /* lit off his face: full clock until then */
static volatile uint32_t wk_touch, wk_press; /* touch interrupts, touch readings with a finger down: for perf and the log */
#define DARK_POLL_MS 500  /* dark: LVGL's task, a wake each */
#define DARK_SENS_MS 40   /* dark: the sensors' task (the accelerometer, the top key, the battery) */
#define BOOST_MS 400      /* after a refresh or a touch off his face, the full clock stays this long */
#define SPK_IDLE_MS 4000  /* lit and silent this long (and the mic shut): the speaker's codec and amplifier go off */
static void boot_isr(void *arg);
static void wake_arm(gpio_num_t pin, bool low);
static void wake_off(gpio_num_t pin);
static float face_ms[3]; /* face draw: whole, this core's band, the worker's band (ms, smoothed) */
static float refr_worst, face_worst; /* the slowest refresh and face draw since the last status line, ms */
static const char *refr_worst_on = ""; /* the screen it was on */
static volatile float face_last_worker_us;
/* the benchmark's sums since the last `perf` (debug.c): count, sum and worst, ms */
typedef struct { int n; float sum, max; } acc_t;
static acc_t pf_face, pf_band0, pf_band1, pf_refr, pf_render, pf_stream, pf_te, pf_rows, pf_areas, pf_px, pf_rects;
static void acc(acc_t *a, float v) { a->n++, a->sum += v, a->max = v > a->max ? v : a->max; }
/* a yardstick for `perf`: a fixed float loop (the CPU, and what interrupts take of it) and a 32 KB copy within PSRAM
   (its bandwidth, shared with the face, LVGL and the panel), us each */
static void IRAM_ATTR yardstick(int *cpu_us, int *psram_us)
{
    static uint8_t *a, *b;
    if (!a) a = heap_caps_malloc(32768, MALLOC_CAP_SPIRAM), b = heap_caps_malloc(32768, MALLOC_CAP_SPIRAM);
    volatile float x = 1.0001f, y = 0;
    int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < 20000; i++) y = y * 0.9999f + x * 1.0001f;
    int64_t t1 = esp_timer_get_time();
    if (a && b) memcpy(b, a, 32768);
    int64_t t2 = esp_timer_get_time();
    *cpu_us = (int)(t1 - t0), *psram_us = (int)(t2 - t1);
}

/* how often each task wakes (perf "wk": a second's worth since the last call): what keeps the chip from its sleep */
static volatile uint32_t wk_lvgl, wk_sens, wk_mic, wk_audio;
int board_perf(char *out, int cap)
{
    int pn;
    float psum, pmax;
    ui_lock();
    pip_perf_take(&pn, &psum, &pmax);
    ui_unlock();
#define A(x) x.n, x.n ? x.sum / x.n : 0, x.max
    static int64_t wk_t;
    static uint32_t wk_prev[6];
    int64_t wk_now = esp_timer_get_time();
    float wk_s = wk_t ? (wk_now - wk_t) / 1e6f : 1;
    uint32_t wk_cur[6] = {wk_lvgl, wk_sens, wk_mic, wk_audio, wk_touch, wk_press};
    float wk[6];
    for (int i = 0; i < 6; i++) wk[i] = (wk_cur[i] - wk_prev[i]) / wk_s, wk_prev[i] = wk_cur[i];
    wk_t = wk_now;
    /* in three pieces: one call with all ~30 doubles put ~0.3 KB of arguments on the debug task's 4 KB stack under
       printf's own ~1.5 KB, and an interrupt at the deepest point overran it (a reset mid-bench) */
    int n = snprintf(out, cap, "@P {\"wk\":[%.0f,%.0f,%.0f,%.0f,%.0f,%.0f],\"pip\":[%d,%.1f,%.1f],\"face\":[%d,%.1f,%.1f],\"b0\":[%d,%.1f,%.1f],",
                     wk[0], wk[1], wk[2], wk[3], wk[4], wk[5], pn, pn ? psum / pn : 0, pmax, A(pf_face), A(pf_band0));
    if (n < cap) n += snprintf(out + n, cap - n, "\"b1\":[%d,%.1f,%.1f],\"refr\":[%d,%.1f,%.1f],\"render\":[%d,%.1f,%.1f],\"stream\":[%d,%.1f,%.1f],",
                               A(pf_band1), A(pf_refr), A(pf_render), A(pf_stream));
    if (n < cap) n += snprintf(out + n, cap - n, "\"te\":[%d,%.1f,%.1f],\"rows\":[%d,%.0f,%.0f],\"tint\":[%d,%d],\"scr\":\"%s\",\"mhz\":%d,\"t\":%lld,\"heap\":%d,\"dstack\":%d}",
                               A(pf_te), A(pf_rows), gpio_get_level(BSP_LCD_TOUCH_INT), touch_paused, shell_where(), (int)(esp_clk_cpu_freq() / 1000000),
                               (long long)(esp_timer_get_time() / 1000000), (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                               (int)uxTaskGetStackHighWaterMark(NULL)); /* the least stack this (debug) task has had left, bytes */
    if (n >= cap) n = cap - 1;
#undef A
    extern long face_prof[16][2] __attribute__((weak)); /* only in a profiling build of face.c */
    long (*volatile prof)[2] = face_prof; /* NULL unless face.c was built with FACE_PROFILE */
    if (prof && n < cap - 40 && pf_face.n) {
        n--; /* over the closing brace */
        n += snprintf(out + n, cap - n, ",\"prof\":[");
        for (int k = 0; k < 16 && n < cap - 30; k++)
            n += snprintf(out + n, cap - n, "%s[%ld,%ld]", k ? "," : "", face_prof[k][0] / pf_face.n, face_prof[k][1] / pf_face.n), face_prof[k][0] = face_prof[k][1] = 0;
        n += snprintf(out + n, cap - n, "]}");
    }
    { /* each task's share of a core since the last call, %, the busiest first: what else wants the CPU */
        typedef struct { TaskHandle_t h; uint32_t t; } seen_t;
        static seen_t *prev;     /* in PSRAM, made on the first `perf`: internal RAM is the scarce one */
        static TaskStatus_t *st;
        static int prev_n, *order; /* order and pct too: on the debug task's 4 KB stack they and the snprintfs ran it over */
        static float *pct;
        static uint32_t prev_total;
        if (!st) st = heap_caps_malloc(40 * sizeof *st, MALLOC_CAP_SPIRAM), prev = heap_caps_calloc(40, sizeof *prev, MALLOC_CAP_SPIRAM),
                 order = heap_caps_malloc(40 * sizeof *order, MALLOC_CAP_SPIRAM), pct = heap_caps_malloc(40 * sizeof *pct, MALLOC_CAP_SPIRAM);
        if (!st || !prev || !order || !pct) goto no_tasks;
        uint32_t total;
        int k = (int)uxTaskGetSystemState(st, 40, &total), m = 0;
        uint32_t dtot = total - prev_total;
        for (int i = 0; i < k; i++) {
            uint32_t was = 0;
            for (int j = 0; j < prev_n; j++)
                if (prev[j].h == st[i].xHandle) was = prev[j].t;
            pct[i] = dtot ? (st[i].ulRunTimeCounter - was) * 100.0f / dtot : 0;
            int j = m++;
            for (; j > 0 && pct[order[j - 1]] < pct[i]; j--) order[j] = order[j - 1];
            order[j] = i;
        }
        for (int i = 0; i < k; i++) prev[i].h = st[i].xHandle, prev[i].t = st[i].ulRunTimeCounter;
        prev_n = k, prev_total = total;
        if (n < cap - 40) {
            n--;
            n += snprintf(out + n, cap - n, ",\"tasks\":{");
            for (int i = 0; i < m && i < 10 && n < cap - 40; i++)
                n += snprintf(out + n, cap - n, "%s\"%s\":%.0f", i ? "," : "", st[order[i]].pcTaskName, pct[order[i]]);
            n += snprintf(out + n, cap - n, "}}");
        }
    }
no_tasks:
    if (n < cap - 40) {
        int cu, pu;
        yardstick(&cu, &pu);
        n--;
        n += snprintf(out + n, cap - n, ",\"cal\":[%d,%d],\"areas\":[%.1f,%.0f],\"px\":[%.0f,%.0f]}", cu, pu,
                      pf_areas.n ? pf_areas.sum / pf_areas.n : 0, pf_areas.max, pf_px.n ? pf_px.sum / pf_px.n : 0, pf_px.max);
        if (n < cap - 30) n--, n += snprintf(out + n, cap - n, ",\"rects\":[%d,%.1f]}", pf_rects.n, pf_rects.n ? pf_rects.sum / pf_rects.n : 0);
    }
    pf_face = pf_band0 = pf_band1 = pf_refr = pf_render = pf_stream = pf_te = pf_rows = pf_areas = pf_px = pf_rects = (acc_t){0};
    return n;
}

static esp_err_t qmi_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(qmi, buf, 2, 50);
}

static bool qmi_init(i2c_master_bus_handle_t bus)
{
    for (uint8_t addr = 0x6B; addr >= 0x6A; addr--) {
        if (i2c_master_probe(bus, addr, 20) != ESP_OK) continue;
        i2c_device_config_t cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = 400000};
        ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &cfg, &qmi));
        uint8_t reg = QMI_WHO_AM_I, id = 0;
        i2c_master_transmit_receive(qmi, &reg, 1, &id, 1, 50);
        ESP_LOGI(TAG, "QMI8658 at 0x%02X, WHO_AM_I=0x%02X (expect 0x05)", addr, id);
        return qmi_write(QMI_CTRL1, 0x40) == ESP_OK && qmi_write(QMI_CTRL2, 0x16) == ESP_OK &&
               qmi_write(QMI_CTRL3, 0x56) == ESP_OK && qmi_write(QMI_CTRL7, 0x03) == ESP_OK;
    }
    ESP_LOGW(TAG, "QMI8658 not found at 0x6A/0x6B, running without motion");
    return false;
}

/* Dark: the gyro off (it is for tumbles, his face only) and the accelerometer read 25 times a second instead of 50 - steps,
   a lift, a shake all still seen. (Its FIFO was tried, read twice a second: on this part it handed samples over a word out
   of step now and then, its count is in words where the datasheet says bytes, a transfer's last byte came back wrong - Pip
   woke lying on the table, or slept through being picked up. Plain reads are what the lit screen has always used.) */
/* Switched, the sensor puts out a few saturated samples (4 g on every axis) while it starts again: seen as Pip being
   picked up, the screen lit at once, and a step counted. So it is stopped, set, started and given time to settle, and the
   caller drops the first readings as well */
#define QMI_SETTLE_MS 60
static void qmi_dark(bool dark)
{
    qmi_write(QMI_CTRL7, 0x00);                 /* both off while the rate changes */
    qmi_write(QMI_CTRL2, dark ? 0x17 : 0x16);   /* 4 g range; 62.5 Hz dark, ~117 Hz lit */
    qmi_write(QMI_CTRL7, dark ? 0x01 : 0x03);   /* dark: the accelerometer alone */
    vTaskDelay(pdMS_TO_TICKS(QMI_SETTLE_MS));
}

static bool qmi_read(float a[3], float g[3]) /* accel in g, gyro in deg/s, both in device axes (the gyro turns with the chip too) */
{
    uint8_t reg = QMI_AX_L, raw[12];
    if (i2c_master_transmit_receive(qmi, &reg, 1, raw, 12, 50) != ESP_OK) return false;
    float v[6];
    for (int i = 0; i < 6; i++) v[i] = (int16_t)(raw[2 * i] | raw[2 * i + 1] << 8) / (i < 3 ? QMI_LSB_PER_G : QMI_LSB_PER_DPS);
    for (int i = 0; i < 3; i++) a[i] = AXIS[i].sign * v[AXIS[i].src], g[i] = AXIS[i].sign * v[3 + AXIS[i].src];
    return true;
}

/* ---------- AXP2101 power manager (0x34): battery gauge and the PWR key ---------- */
#define AXP_STATUS1 0x00      /* bit 5 VBUS good, bit 3 battery present */
#define AXP_STATUS2 0x01      /* bits 6..5: 01 charging */
#define AXP_GAUGE_CTRL 0x18   /* bit 3: fuel gauge on */
#define AXP_ADC_CTRL 0x30     /* bit 0: battery voltage ADC on */
#define AXP_IRQ_EN2 0x41      /* bit 3: PWR key short press */
#define AXP_IRQ_STATUS2 0x49  /* same bit, write 1 to clear */
#define AXP_BAT_PERCENT 0xA4
#define AXP_VBAT_H 0x34       /* battery voltage, mV: 6 bits here, 8 in the next register */
#define AXP_COMMON 0x10       /* bit 0: power off now (as a long press of the top key) */
#define AXP_VOFF 0x24         /* bits 2..0: the PMU's own power-off voltage, 2600 + 100 n mV */
/* Empty: off before the cell is run down. At 0% the gauge reads ~3.2 V and the voltage falls off a cliff; left on, it
   ran to 2.94 V and a brownout. Under EMPTY_MV for EMPTY_S (a lit screen's dip passes) with no USB: the pet is saved
   and the PMU switches off; the top key or the cable brings it back. VOFF_MV is the PMU's backstop if we miss it. */
#define EMPTY_MV 3300
#define EMPTY_S 30
#define VOFF_MV 3000
static i2c_master_dev_handle_t pmu;

static int pmu_get(uint8_t reg)
{
    uint8_t v;
    return i2c_master_transmit_receive(pmu, &reg, 1, &v, 1, 50) == ESP_OK ? v : -1;
}

static void pmu_set(uint8_t reg, uint8_t v)
{
    uint8_t buf[2] = {reg, v};
    i2c_master_transmit(pmu, buf, 2, 50);
}

static bool pmu_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x34, .scl_speed_hz = 400000};
    if (i2c_master_bus_add_device(bus, &cfg, &pmu) != ESP_OK) return false;
    int g = pmu_get(AXP_GAUGE_CTRL), adc = pmu_get(AXP_ADC_CTRL), irq = pmu_get(AXP_IRQ_EN2);
    if (g < 0) return false;
    pmu_set(AXP_GAUGE_CTRL, (uint8_t)(g | 0x08));
    pmu_set(AXP_ADC_CTRL, (uint8_t)(adc | 0x01));
    pmu_set(AXP_IRQ_EN2, (uint8_t)(irq | 0x08));
    pmu_set(AXP_IRQ_STATUS2, 0xFF); /* forget presses from before we started */
    int voff = pmu_get(AXP_VOFF);
    if (voff >= 0) pmu_set(AXP_VOFF, (uint8_t)((voff & ~0x07) | (VOFF_MV - 2600) / 100));
    return true;
}

static struct { int mv, pct; bool charging, usb; } bat = {.pct = -1};
bool board_usb(void) { return bat.usb; } /* the debug link listens on USB only while a cable is in */

/* Automatic brightness, by the battery: the dimmer the panel, the less it draws. The set level down to half the
   charge, then down with it to a third of the level at 10% and below; on the charger (or the level unknown) the set
   level. Never under AUTO_FLOOR % (it never goes dark), never over the set level. Worked out a few times a minute,
   sent to the panel only on a change. */
#define AUTO_FLOOR 20
static bool torch_on; /* the Фонарик app is open: the panel at full, whatever the settings */
static int auto_level(void)
{
    if (torch_on) return 100;
    if (!auto_bright || bat.pct < 0 || bat.charging) return bright;
    const float low = 0.33f;
    float k = bat.pct >= 50 ? 1 : bat.pct <= 10 ? low : low + (1 - low) * (bat.pct - 10) / 40.0f;
    int v = (int)(bright * k + 0.5f), lo = bright < AUTO_FLOOR ? bright : AUTO_FLOOR;
    return v < lo ? lo : v;
}

static void bright_update(void) /* from the sensors task: a new level only when it differs */
{
    int v = auto_level();
    if (v != bright_shown) bright_shown = v, bright_changed = true;
}


static void pmu_battery(void)
{
    int s1 = pmu_get(AXP_STATUS1), s2 = pmu_get(AXP_STATUS2), pct = pmu_get(AXP_BAT_PERCENT);
    int vh = pmu_get(AXP_VBAT_H), vl = pmu_get(AXP_VBAT_H + 1);
    if (s1 < 0) return;
    bool present = s1 & 0x08, charging = s2 >= 0 && ((s2 >> 5) & 0x03) == 0x01;
    bat.pct = present && pct >= 0 && pct <= 100 ? pct : -1, bat.charging = charging, bat.usb = s1 & 0x20;
    bat.mv = present && vh >= 0 && vl >= 0 ? (vh & 0x3F) << 8 | vl : 0;
    /* on the charger = the cable in: once full the AXP2101 stops charging (bits 6..5 read standby), but he is still on
       it - the menu's bolt stays, and the pet's overcharge (>=99% on the charger) can happen at all */
    shell_battery(bat.pct, charging || (bat.usb && present));
}

/* ---------- the battery log: a row a minute in flash, to measure what a charge lasts ----------
   Charge him full, unplug, let him run down, plug in and read it with "bat" (the panel saves it as CSV). A ring of
   16-byte rows behind the mood history in the "storage" partition: ~17 h, kept across restarts; the uptime starts
   again at 0 on every boot, so each run is easy to tell apart. */
#define BATLOG_AT (64 * 1024) /* the mood history (debug.c) has the first 24 KB */
#define BATLOG_SECTORS 4
#define BATLOG_SLOTS (BATLOG_SECTORS * 4096 / 16)
/* t: the real clock (unix s) when the row was written, 0 while it was not set since power-on (the panel works it out
   from the run's other rows); seq: 24 bits, the ring's order (31 years of minutes) */
typedef struct { uint32_t t, up_s; uint16_t mv; uint8_t pct, flags, lit, seq[3]; } batrow_t; /* flags: 1 charging, 2 USB, 4 eco */
_Static_assert(sizeof(batrow_t) == 16, "a flash slot");
#define BATROW_OK(r) ((r).mv != 0xFFFF && (r).pct <= 100) /* erased flash reads 0xFF */
#define BATROW_SEQ(r) ((uint32_t)(r).seq[0] | (uint32_t)(r).seq[1] << 8 | (uint32_t)(r).seq[2] << 16)
static const esp_partition_t *batlog;
static int batlog_next;
static uint32_t batlog_seq;

static void batlog_load(void)
{
    batlog = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    if (!batlog) return;
    batrow_t r;
    for (int i = 0; i < BATLOG_SLOTS; i++)
        if (esp_partition_read(batlog, BATLOG_AT + i * 16, &r, 16) == ESP_OK && BATROW_OK(r) && BATROW_SEQ(r) >= batlog_seq)
            batlog_seq = BATROW_SEQ(r) + 1, batlog_next = (i + 1) % BATLOG_SLOTS;
}

static void batlog_put(int lit_pct)
{
    if (!batlog || bat.pct < 0) return;
    time_t now = time(NULL);
    uint32_t seq = batlog_seq++;
    batrow_t r = {now > 1735689600 ? (uint32_t)now : 0, (uint32_t)(esp_timer_get_time() / 1000000), (uint16_t)bat.mv, (uint8_t)bat.pct,
                  (uint8_t)(bat.charging | bat.usb << 1 | pip_eco_on() << 2), (uint8_t)lit_pct, {(uint8_t)seq, (uint8_t)(seq >> 8), (uint8_t)(seq >> 16)}};
    if (batlog_next % 256 == 0) esp_partition_erase_range(batlog, BATLOG_AT + batlog_next * 16, 4096);
    esp_partition_write(batlog, BATLOG_AT + batlog_next * 16, &r, 16);
    batlog_next = (batlog_next + 1) % BATLOG_SLOTS;
}

void batlog_dump(void (*out)(const char *))
{
    char b[64];
    batrow_t r;
    for (int k = 0; batlog && k < BATLOG_SLOTS; k++) { /* oldest first: from the slot after the newest, round the ring */
        int i = (batlog_next + k) % BATLOG_SLOTS;
        if (esp_partition_read(batlog, BATLOG_AT + i * 16, &r, 16) != ESP_OK || !BATROW_OK(r)) continue;
        snprintf(b, sizeof b, "@B %lu %lu %u %u %u %u %lu\n", (unsigned long)BATROW_SEQ(r), (unsigned long)r.up_s, r.mv, r.pct, r.flags,
                 r.lit, (unsigned long)r.t);
        out(b);
        if (k % 32 == 31) vTaskDelay(1); /* let the USB drain */
    }
    out("@B end\n");
}

/* a test for the panel's chart: `minutes` made-up rows, a minute apart, ending now (the clock must be set): an
   hour off the cable running down from 100%, the screen lit now and then */
void batlog_fake(int minutes)
{
    time_t now = time(NULL);
    if (!batlog || now < 1735689600 || minutes <= 0) return;
    if (minutes > BATLOG_SLOTS) minutes = BATLOG_SLOTS;
    for (int i = 0; i < minutes; i++) {
        float k = (float)i / minutes;
        int pct = (int)(100 - 30 * k - 8 * k * k), lit = i % 7 < 2 ? 60 + i % 40 : 0;
        uint32_t seq = batlog_seq++;
        batrow_t r = {(uint32_t)(now - (minutes - i) * 60), (uint32_t)(60 + i * 60), (uint16_t)(3450 + pct * 7), (uint8_t)pct, 0,
                      (uint8_t)lit, {(uint8_t)seq, (uint8_t)(seq >> 8), (uint8_t)(seq >> 16)}};
        if (batlog_next % 256 == 0) esp_partition_erase_range(batlog, BATLOG_AT + batlog_next * 16, 4096);
        esp_partition_write(batlog, BATLOG_AT + batlog_next * 16, &r, 16);
        batlog_next = (batlog_next + 1) % BATLOG_SLOTS;
    }
}

void batlog_clear(void)
{
    if (batlog) esp_partition_erase_range(batlog, BATLOG_AT, BATLOG_SECTORS * 4096);
    batlog_next = 0, batlog_seq = 0;
}

static bool pmu_pwr_pressed(void)
{
    int st = pmu_get(AXP_IRQ_STATUS2);
    if (st < 0 || !(st & 0x08)) return false;
    pmu_set(AXP_IRQ_STATUS2, 0x08);
    return true;
}

/* 50 Hz: accelerometer and the BOOT key into the UI, plus a status line every 5 s */
/* While the screen is lit the chip runs flat out (the face needs both cores at 240 MHz); dark, these locks go and
   it drops to 80 MHz and light-sleeps between wakeups, unless something else holds it (Bluetooth between its radio
   events, an open I2S, the USB host on the debug port). */
/* On USB the clocks stay as an open I2S used to keep them (until the dark screen shut the mic too): with them let go,
   a debug command arriving on a dark screen wedged the USB link until the cable was pulled out. The cable's power
   pays for it. */
static esp_pm_lock_handle_t lit_cpu, lit_awake, usb_hold;
static void pm_start(void)
{
    esp_pm_config_t pm = {.max_freq_mhz = 240, .min_freq_mhz = 80, .light_sleep_enable = true};
    esp_err_t e = esp_pm_configure(&pm);
    if (e != ESP_OK) ESP_LOGW(TAG, "power management off: %s", esp_err_to_name(e));
    esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "usb", &usb_hold);
    esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "lit", &lit_cpu);
    esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "lit", &lit_awake);
    esp_pm_lock_acquire(lit_cpu), esp_pm_lock_acquire(lit_awake);
}

/* light sleep isolates every pin (an S3 workaround, not optional); these must hold through it: the panel's and the
   touch's resets (floating, they reset the chips), the panel's select, the amplifier, the I2C pull-ups, the bottom key */
static void pm_keep_pins(void)
{
    const gpio_num_t keep[] = {BSP_LCD_RST, BSP_LCD_TOUCH_RST, BSP_LCD_TOUCH_INT, BSP_LCD_CS, BSP_POWER_AMP_IO, BSP_I2C_SCL, BSP_I2C_SDA, BOOT_GPIO};
    for (int k = 0; k < sizeof keep / sizeof keep[0]; k++) gpio_sleep_sel_dis(keep[k]);
}

/* The full clock as a lock is held by LVGL's task alone (cpu_update): on his face (FACE_HOLDS_CPU), for BOOST_MS after the
   screen lights or a key, dark never. Otherwise ESP-IDF's own per-core locks (rtos0/rtos1) give 240 MHz to whatever runs
   and 80 MHz to idle: the menu or an app waits at 80 between its frames (measured with CONFIG_PM_PROFILING: the menu 61%
   of its time at 80 MHz, before 0%). */
static bool cpu_held = true; /* pm_start took it */
static void cpu_boost(void) { boost_until = esp_timer_get_time() + BOOST_MS * 1000LL; }
#ifndef FACE_HOLDS_CPU
#define FACE_HOLDS_CPU 0
#endif
static void cpu_update(bool lit, bool face)
{
    bool want = lit && ((face && FACE_HOLDS_CPU) || esp_timer_get_time() < boost_until);
    if (want == cpu_held) return;
    cpu_held = want;
    if (want) esp_pm_lock_acquire(lit_cpu);
    else esp_pm_lock_release(lit_cpu);
}

static void pm_lit(bool lit)
{
    if (lit) esp_pm_lock_acquire(lit_awake), cpu_boost();
    else esp_pm_lock_release(lit_awake);
    if (lvgl_handle) xTaskNotifyGive(lvgl_handle);           /* LVGL's task: its clock, its touch polling */
    /* the panel's: on at once. Not xTaskAbortDelay: landing while the task was blocked inside the SPI driver (the bus
       lock's semaphore, the transaction queues, all portMAX_DELAY) it broke the driver's state and the next acquire
       waited for good - the screen froze on its last frame while sounds went on, until another light-up aborted it */
    if (lit && panel_handle) xTaskNotify(panel_handle, PANEL_WAKE, eSetBits);
    ESP_LOGI(TAG, "chip %s", lit ? "awake" : "sleeping between wakeups");
}

static void sensors_task(void *arg)
{
    bool imu = qmi_init(bsp_i2c_get_handle());
    bool power = pmu_init(bsp_i2c_get_handle());
    if (!power) ESP_LOGW(TAG, "AXP2101 not answering: no battery level, no PWR key");
    int64_t last_bat = 0, last_batlog = esp_timer_get_time(), last_bright = 0;

    bool ble_off = false, lit_held = true;
    batlog_load();
    gpio_config_t io = {.pin_bit_mask = 1ULL << BOOT_GPIO, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&io);
    sens_handle = xTaskGetCurrentTaskHandle();
    gpio_isr_handler_add(BOOT_GPIO, boot_isr, NULL), gpio_intr_disable(BOOT_GPIO);
    bool dark_imu = false;
    float lit_s = 0, all_s = 0; /* the battery log's "lit" share, by time (the loop's pace changes with the screen) */
    int64_t last = esp_timer_get_time(), last_log = last, last_save = last;
    int boot_prev = 1;
    float mic_peak = 0;
    for (;;) {
        bool dark = !panel_want;
        int settle = 0; /* rounds of samples to drop: the sensor just switched */
        if (imu && dark != dark_imu) dark_imu = dark, qmi_dark(dark), settle = 3; /* the gyro off and back: a few readings dropped */
        if (dark) wake_arm(BOOT_GPIO, gpio_get_level(BOOT_GPIO)); /* the key's next change wakes us (pressed: low) */
        else wake_off(BOOT_GPIO);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(dark ? DARK_SENS_MS : 20)); /* dark: 25 times a second, or the key */
        wk_sens++;
        int64_t now = esp_timer_get_time();
        float dt = (now - last) / 1e6f, a[3] = {0}, g[3] = {0};
        last = now;
        int boot = gpio_get_level(BOOT_GPIO);
        static int skip;
        if (settle) skip = settle;
        bool have_a = imu && !skip && qmi_read(a, g);
        if (skip > 0) skip--; /* just switched: nothing of the start-up */
        bool pwr = power && pmu_pwr_pressed();

        ui_lock();
        static bool real_time;
        if (!real_time && time(NULL) > 1735689600) { /* the iPhone or the panel just set the real clock */
            real_time = true;
            size_t n;
            const pet_t *pet = pip_pet_state(&n);
            if (pet->last < 1735689600) pip_pet_rebase(time(NULL)); /* he lived on a made-up clock: no jump in age */
        }
        if (tz_min != tz_saved) settings_save(); /* the iPhone told another zone: keep it for the next power-on */
        shell_clock_known(real_time); /* until then the clock app asks for a phone or the panel */
        shell_setup(esp_timer_get_time() < panel_until); /* the panel linked: "РЕЖИМ НАСТРОЙКИ", no input */
        if (have_a) shell_imu(a[0], a[1], a[2], dt);
        if (mic_db() > mic_peak) mic_peak = mic_db();
        shell_time(pet_clock(), tz_min);
        if (now - last_save > SAVE_EVERY_S * 1000000LL || pip_pet_changed()) last_save = now, pet_save();
        if (pip_ny_changed()) ny_save(); /* New Year's: greeted, a snowfall had - not again after a restart */
        /* the bottom key is GPIO 0; the top one reaches us as the AXP2101's short-press interrupt
           (a long press is the PMU's own power off) */
        if (boot != boot_prev || pwr) { /* a key: full clock and the speaker ready for what it brings */
            cpu_boost();
            if (audio_handle) spk_warm = true, xTaskNotifyGive(audio_handle);
            if (lvgl_handle) xTaskNotifyGive(lvgl_handle);
        }
        if (boot_prev && !boot) shell_button(PIP_BTN_BOOT);
        if (!boot_prev && boot) shell_button_up(PIP_BTN_BOOT); /* let go: a tap on Pip, a hold feeds, a tap then a hold washes */
        if (pwr) shell_button(PIP_BTN_PWR), shell_button_up(PIP_BTN_PWR);
        if (power && now - last_bat > 2000000) {
            last_bat = now, pmu_battery();
            static int64_t low_since;
            static bool drained; /* once a run flat: if the PMU did not switch off, only that is tried again, not the cost to him */
            bool low = bat.mv > 0 && bat.mv < EMPTY_MV && !bat.usb;
            if (!low) low_since = 0, drained = false;
            else if (!low_since) low_since = now;
            else if (now - low_since > EMPTY_S * 1000000LL) {
                if (!drained) {
                    drained = true;
                    ESP_LOGW(TAG, "battery empty (%d mV): saving the pet and powering off", bat.mv);
                    pip_drained(); /* run flat (the UI lock is held here): it costs him food and energy, and he will be cross */
                    pet_save();
                    nvs_handle_t h; /* read back (and erased) at the next boot: the intro ends angry. Only this power-off sets it */
                    if (nvs_open("keypet", NVS_READWRITE, &h) == ESP_OK) nvs_set_u8(h, "drained", 1), nvs_commit(h), nvs_close(h);
                    vTaskDelay(pdMS_TO_TICKS(100)); /* the log line out */
                }
                int common = pmu_get(AXP_COMMON); /* -1 on an I2C failure: never write 0xFF */
                if (common >= 0) pmu_set(AXP_COMMON, (uint8_t)(common | 0x01));
            }
        }
        theme_keep();
        sound_keep();
        if (pip_eyes_get() != eyes_saved) eyes_saved = pip_eyes_get(), settings_save(); /* picked in Глаза */
        if (shell_brightness_get() != bright) settings_bright(shell_brightness_get()), settings_save(); /* set in Яркость */
        const char *where = shell_where(); /* a banner over the torch leaves it at full */
        if (strcmp(where, "banner") && torch_on != !strcmp(where, "torch")) torch_on = !torch_on, bright_recalc = true; /* the torch: full */
        if (now - last_bright > 15000000 || bright_recalc) last_bright = now, bright_recalc = false, bright_update();
        pip_info_t in;
        pip_get_info(&in);
        panel_want = pip_display_level();
        bool quiet = pip_eco_on() && !panel_want; /* saving energy: Bluetooth sleeps with the screen */
        ui_unlock();
        boot_prev = boot;
        if (quiet != ble_off) ble_off = quiet, ble_quiet(quiet);
        if (lit_held != (panel_want > 0)) {
            lit_held = panel_want > 0, pm_lit(lit_held);
            if (lit_held) { /* what lit it: pip.c's line (light_up's caller), the touch interrupts and presses meanwhile */
                ui_lock();
                int line = pip_lit_why(NULL);
                ui_unlock();
                ESP_LOGI(TAG, "lit by pip.c:%d (touch irq %u, presses %u, key %d, top %d)", line, (unsigned)wk_touch, (unsigned)wk_press, !boot, pwr);
            }
        }
        static bool usb_held;
        if (usb_held != bat.usb) usb_held = bat.usb, usb_held ? esp_pm_lock_acquire(usb_hold) : esp_pm_lock_release(usb_hold);
        lit_s += panel_want > 0 ? dt : 0, all_s += dt;
        if (now - last_batlog > 60000000) last_batlog = now, batlog_put(all_s > 0 ? (int)(lit_s * 100 / all_s) : 0), lit_s = all_s = 0;

        if (now - last_log > 5000000) {
            last_log = now;
            char note[96]; /* the developer HUD's last line: what only the board knows */
            snprintf(note, sizeof note, "озу %u К · psram %.1f М · лицо %.0f мс", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                     heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1048576.0f, face_ms[0]);
            pip_dev_note(note);
            ESP_LOGI(TAG, "mood=%s reaction=%s steps=%d face=%.1f ms refresh=%.1f ms accel=(%.2f %.2f %.2f) free psram=%u internal=%u (least %u, block %u)",
                     in.mood, in.reaction, in.steps, in.frame_ms, refr_ms, have_a ? a[0] : 0, have_a ? a[1] : 0,
                     have_a ? a[2] : 0, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     /* the lowest it has been since power-on, and the biggest piece left: what runs out first */
                     (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            ESP_LOGI(TAG, "face draw %.1f ms on two cores (bands %.1f / %.1f ms), battery %s %d mV%s%s", face_ms[0], face_ms[1], face_ms[2],
                     shell_battery_text(), bat.mv, bat.usb ? ", USB" : "", pip_eco_on() ? ", eco" : "");
            ESP_LOGI(TAG, "screen %s: %d frames/s to the panel, render %.1f ms", shell_where(), frames_sent / 5, render_ms);
            frames_sent = 0;
            ESP_LOGI(TAG, "panel: TE wait %.1f ms (in parallel), stream %.0f rows/ms (%.1f MB/s)", te_wait_ms, stream_rows_per_ms, stream_mb_s);
            /* the averages above hide a single slow frame: the worst of the last 5 s, and what was on */
            const char *wm, *wr;
            ui_lock();
            float wf = pip_frame_worst(5, &wm, &wr);
            ui_unlock();
            ESP_LOGI(TAG, "worst in 5 s: Pip's frame %.0f ms (%s%s%s), refresh %.0f ms (%s), face draw %.0f ms", wf, wm, *wr ? ", " : "", wr,
                     refr_worst, refr_worst_on, face_worst);
            refr_worst = face_worst = 0;
            if (mic_on()) ESP_LOGI(TAG, "mic: %.0f dB, loudest %.0f dB in 5 s%s", mic_db(), mic_peak, mic_blowing() ? ", blowing" : "");
            mic_peak = 0;
            ESP_LOGI(TAG, "pet: %02d h food=%.2f energy=%.2f fun=%.2f love=%.2f walk=%.2f day=%+.2f %s", in.hour, in.food,
                     in.energy, in.fun, in.love, in.walk, in.day_mood, in.asleep ? "asleep" : "awake");
        }
    }
}

/* Pip's voice through the ES8311 codec: when he is quiet the task just sleeps instead of streaming silence. */
#define AUDIO_RATE 16000
#define MIC_GAIN_DB 24 /* ui/mic.c's MIC_SPL_OFFSET assumes this */

/* Codecs opened and closed thump through the speaker: the ES8311 powering its DAC up or down, and the shared I2S
   stopped and restarted when the mic (RX) comes or goes under a running speaker (TX). Since the dark screen shuts both,
   each waking did. So the speaker is muted around every such change, and opened it plays silence before it unmutes.
   spk_mx: the two tasks never mute and unmute it across each other. */
static esp_codec_dev_handle_t spk_dev;
static volatile bool spk_open;
static SemaphoreHandle_t spk_mx;
/* codec_mx: held while the codecs are being set up or switched (their I2C writes). The touch's bus reset (touch_read)
   takes it without waiting and is put off while it is held: a reset in the middle of the ES8311's or the ES7210's
   init failed it, the BSP asserted and the board rebooted (2 boots in 4 on 1.3.31, Nov 2026). Never held by LVGL's task */
static SemaphoreHandle_t codec_mx;
static void codec_hold(bool on) { if (codec_mx) on ? xSemaphoreTake(codec_mx, portMAX_DELAY) : xSemaphoreGive(codec_mx); }
#define SPK_SETTLE_MS 40

static void mic_switch(esp_codec_dev_handle_t mic, esp_codec_dev_sample_info_t *fs, bool on, bool *open)
{
    xSemaphoreTake(spk_mx, portMAX_DELAY);
    codec_hold(true);
    if (spk_open) esp_codec_dev_set_out_mute(spk_dev, true), vTaskDelay(pdMS_TO_TICKS(SPK_SETTLE_MS));
    if (on) *open = esp_codec_dev_open(mic, fs) == ESP_CODEC_DEV_OK, esp_codec_dev_set_in_gain(mic, MIC_GAIN_DB);
    else esp_codec_dev_close(mic), mic_stop(), *open = false;
    if (spk_open) vTaskDelay(pdMS_TO_TICKS(SPK_SETTLE_MS)), esp_codec_dev_set_out_mute(spk_dev, false);
    codec_hold(false);
    xSemaphoreGive(spk_mx);
}

/* the ES7210 microphones on the same I2S as the speaker (so the same rate): 20 ms at a time into the meter */
static void mic_task(void *arg)
{
    codec_hold(true);
    esp_codec_dev_handle_t mic = bsp_audio_codec_microphone_init();
    esp_codec_dev_sample_info_t fs = {.sample_rate = AUDIO_RATE, .channel = 1, .bits_per_sample = 16};
    bool ok = mic && esp_codec_dev_open(mic, &fs) == ESP_CODEC_DEV_OK;
    if (ok) esp_codec_dev_set_in_gain(mic, MIC_GAIN_DB);
    codec_hold(false);
    if (!ok) {
        ESP_LOGW(TAG, "microphone init failed: no noise meter, no blowing");
        vTaskDelete(NULL);
    }
    static int16_t buf[AUDIO_RATE / 50];
    bool open = true;
    for (int k = 0;; k++) {
        wk_mic++;
        if (!open || k % 25 == 0) { /* open: every 25 blocks (half a second); shut: each time round, twice a second */ /* twice a second: does anyone listen? (Pip's breath and bangs, the noise meter) */
            ui_lock();
            int mode = shell_mic(); /* Pip listening (all of it), the noise meter (the level alone), or nobody */
            ui_unlock();
            bool want = mode > 0;
            mic_listen(mode == 2);
            if (want != open) {
                mic_switch(mic, &fs, want, &open);
                ESP_LOGI(TAG, "microphone %s", open ? "on" : "off");
            }
        }
        if (!open) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }
        if (esp_codec_dev_read(mic, buf, sizeof buf) == ESP_CODEC_DEV_OK) mic_feed(buf, AUDIO_RATE / 50, AUDIO_RATE);
        else vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* opened muted, silence through it while the DAC settles, then heard; closed the other way round */
static void spk_switch(bool on)
{
    static int16_t quiet[256]; /* zeros */
    static esp_codec_dev_sample_info_t fs = {.sample_rate = AUDIO_RATE, .channel = 1, .bits_per_sample = 16};
    xSemaphoreTake(spk_mx, portMAX_DELAY);
    codec_hold(true);
    if (on) {
        esp_codec_dev_open(spk_dev, &fs), esp_codec_dev_set_out_mute(spk_dev, true), esp_codec_dev_set_out_vol(spk_dev, 70);
        for (int i = 0; i < 4; i++) esp_codec_dev_write(spk_dev, quiet, sizeof quiet); /* 64 ms */
        esp_codec_dev_set_out_mute(spk_dev, false);
    } else {
        esp_codec_dev_set_out_mute(spk_dev, true);
        for (int i = 0; i < 2; i++) esp_codec_dev_write(spk_dev, quiet, sizeof quiet);
        esp_codec_dev_close(spk_dev);
    }
    spk_open = on;
    codec_hold(false);
    xSemaphoreGive(spk_mx);
    ESP_LOGI(TAG, "speaker %s", on ? "on" : "off");
}

#define SOUND_TAIL_MS 400
static void audio_wake(void) { if (audio_handle) xTaskNotifyGive(audio_handle); }
static void audio_task(void *arg)
{
    codec_hold(true);
    esp_codec_dev_handle_t spk = bsp_audio_codec_speaker_init();
    codec_hold(false);
    if (!spk) {
        ESP_LOGW(TAG, "speaker init failed, Pip stays silent");
        vTaskDelete(NULL);
    }
    static int16_t buf[256];
    spk_dev = spk, spk_mx = xSemaphoreCreateMutex();
    spk_switch(true);
    xTaskCreate(mic_task, "mic", 3072, NULL, 4, NULL); /* after the speaker: it set up the I2S both share */
    int64_t last_sound = 0, warm_at = 0;
    audio_handle = xTaskGetCurrentTaskHandle();
    voice_on_say(audio_wake);
    for (;;) {
        wk_audio++;
        /* A sound started after the stream stopped lands in DMA buffers that the driver's queue hands out stale (auto
           clear runs over them meanwhile): a short click came out chopped, and the menu's wheel, a click every few tens
           of ms, rasped. So for SOUND_TAIL_MS after a sound the stream goes on with silence: a fast series is one stream
           (a click in it is late by at most the DMA's ~90 ms) */
        bool open = spk_open;
        if (!voice_busy() && open && panel_want && esp_timer_get_time() - last_sound < SOUND_TAIL_MS * 1000LL) {
            memset(buf, 0, sizeof buf);
            esp_codec_dev_write(spk, buf, sizeof buf); /* blocks at the DMA's pace */
            continue;
        }
        if (!voice_busy()) {
            /* an open I2S keeps the chip awake: shut it while dark and silent (and the mic, sharing it, is shut); lit, after
               SPK_IDLE_MS of silence too (the codec and the amplifier draw while they wait) - a finger or a key opens it
               again ahead of the sound it is likely to bring (spk_warm), so the wheel's first click is not late */
            int64_t now = esp_timer_get_time();
            bool idle = now - last_sound > SPK_IDLE_MS * 1000LL && now - warm_at > SPK_IDLE_MS * 1000LL;
            if (open && (!panel_want || idle) && !mic_on()) spk_switch(false), open = false;
            if (spk_warm) {
                if (!open && panel_want) spk_switch(true), open = true;
                warm_at = esp_timer_get_time(), spk_warm = false;
            }
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(open ? 200 : 2000)); /* a sound (voice_on_say) or a finger wakes it */
            continue;
        }
        if (!open) spk_switch(true);
        voice_render(buf, 256);
        esp_codec_dev_write(spk, buf, sizeof buf);
        last_sound = esp_timer_get_time();
    }
}

/* ---------- the display: our own LVGL port, synced to the panel's scan ----------
   The panel's TE (tearing effect) pulse is wired to GPIO 13 (found by probing: 60 Hz, high in vertical blanking);
   the BSP does not use it. LVGL draws only what changed into a full frame buffer in PSRAM (direct mode). When a
   frame is done the changed rows wait for TE and then stream to the panel without a pause, byte-swapped through two
   internal DMA bounce buffers at 80 MHz QSPI: faster than the scan, so the scan, starting right behind us, never
   overtakes a half-sent eye. LVGL draws byte-swapped, so the rows go straight from the frame. */
#define LCD_TE_GPIO 13
#define LCD_QSPI_HZ (80 * 1000 * 1000)
#define LCD_W BSP_LCD_H_RES
#define LCD_H BSP_LCD_V_RES
#define LCD_CHUNK 8192 /* rows go out in chunks this big through two internal DMA buffers (12 KB: barely faster) */
#define LCD_QUEUE 3     /* SPI transactions in flight (the two chunks and the window commands) */
static esp_lcd_panel_io_handle_t lcd_io;
static esp_lcd_panel_handle_t lcd_panel;
static esp_lcd_touch_handle_t lcd_touch;
static SemaphoreHandle_t te_sem, chunk_free, ui_mutex, flushed;
static uint8_t *chunk_buf[2]; /* internal DMA buffers the rows go out through, allocated once */
static lv_area_t box, dirty = {0, LCD_H, LCD_W - 1, -1}; /* the rows to send, the rows changed so far */
/* the changed areas themselves, as LVGL flushed them (x even..odd, y even..odd, the panel's 2-pixel steps): sent as their
   own windows when they cover much less than their whole rows (Flappy Pip's full-height columns: all 466 rows each frame
   took ~16.5 ms, as long as the panel's scan, and the scan overtook the write near the top - the columns tore there) */
#define RECT_MAX 24
static lv_area_t rect_in[RECT_MAX], rect_out_l[RECT_MAX];
static int rect_in_n, rect_out_n; /* rect_in_n > RECT_MAX: too many, whole rows */
static const uint16_t *frame;
static lv_display_t *disp_flushing;
/* stream_rows_per_ms must stay well above the scan's ~28 rows/ms */

/* LVGL's task takes the lock again the moment it lets it go when the next face frame is already done (a cheap face, drawn
   faster than a refresh: ouch, a rare card pinned): it never blocks between passes, and a task waiting for the lock (the
   status line, the debug link's commands, BLE) starved for seconds. Waiters say so; LVGL's task then steps aside a tick */
static volatile int ui_waiting;
void ui_lock(void)
{
    if (xTaskGetCurrentTaskHandle() == lvgl_handle) { xSemaphoreTakeRecursive(ui_mutex, portMAX_DELAY); return; }
    __atomic_add_fetch(&ui_waiting, 1, __ATOMIC_RELAXED);
    xSemaphoreTakeRecursive(ui_mutex, portMAX_DELAY);
    __atomic_sub_fetch(&ui_waiting, 1, __ATOMIC_RELAXED);
}
void ui_unlock(void) { xSemaphoreGiveRecursive(ui_mutex); }

/* the wake-ups: a level interrupt (light sleep wakes on a level, not an edge) that switches itself off and wakes its task;
   the task arms it again for the next change */
static void IRAM_ATTR touch_isr(void *arg)
{
    gpio_intr_disable(BSP_LCD_TOUCH_INT);
    touch_irq = true, wk_touch++;
    BaseType_t w = pdFALSE;
    if (lvgl_handle) vTaskNotifyGiveFromISR(lvgl_handle, &w);
    portYIELD_FROM_ISR(w);
}
static void IRAM_ATTR boot_isr(void *arg)
{
    gpio_intr_disable(BOOT_GPIO);
    BaseType_t w = pdFALSE;
    if (sens_handle) vTaskNotifyGiveFromISR(sens_handle, &w);
    portYIELD_FROM_ISR(w);
}
static void wake_arm(gpio_num_t pin, bool low)
{
    gpio_wakeup_enable(pin, low ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
    gpio_intr_enable(pin);
}
static void wake_off(gpio_num_t pin) { gpio_intr_disable(pin), gpio_wakeup_disable(pin); }

/* dark: LVGL stops asking the touch controller (an I2C read 60 times a second); its INT line wakes us instead. A touch
   resumes the polling (the press then lights the screen, as ever); a second with no finger and it stops again */
static void touch_gate(bool dark)
{
    bool paused = touch_paused;
    static int64_t hold_until;
    lv_timer_t *rt = touch_indev ? lv_indev_get_read_timer(touch_indev) : NULL;
    if (!rt) return;
    int64_t now = esp_timer_get_time();
    if (!dark || touch_irq) {
        if (touch_irq) touch_irq = false, hold_until = now + 1000000;
        if (paused) touch_paused = false, wake_off(BSP_LCD_TOUCH_INT), lv_timer_resume(rt), lv_timer_ready(rt);
        return;
    }
    if (!paused && now > hold_until && gpio_get_level(BSP_LCD_TOUCH_INT)) {
        touch_paused = true, lv_timer_pause(rt);
        wake_arm(BSP_LCD_TOUCH_INT, true);
    }
}

static void brightness(int percent) /* the CO5300's 0x51, as the BSP sends it */
{
    uint8_t v = (uint8_t)(percent * 255 / 100);
    esp_lcd_panel_io_tx_param(lcd_io, 0x02 << 24 | 0x51 << 8, &v, 1);
}

/* both edges of TE: high is the vertical blanking, so the falling edge is the scan starting at row 0. Their times give
   where the scan is at any moment (scan_plan) */
static volatile int64_t te_rise, te_fall;
static volatile int32_t te_period, te_blank; /* us: from fall to fall, and the blanking */
static void IRAM_ATTR te_isr(void *arg)
{
    int64_t now = esp_timer_get_time();
    if (gpio_get_level(LCD_TE_GPIO)) { /* the blanking begins */
        te_rise = now;
        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(te_sem, &woken);
        if (woken) portYIELD_FROM_ISR();
        return;
    }
    int32_t p = (int32_t)(now - te_fall), b = (int32_t)(now - te_rise);
    if (p > 14000 && p < 20000) te_period = te_period ? te_period + (p - te_period) / 8 : p; /* a missed edge: skipped */
    if (b > 0 && b < 4000) te_blank = te_blank ? te_blank + (b - te_blank) / 8 : b;
    te_fall = now;
}

/* When to start sending rows y1..y2 (`bytes` of them): the earliest moment in the next frame at which the write and the
   panel's scan never meet - neither the scan catching the write nor the write catching the scan, with a margin, for the
   write 15% faster or slower than the stream's measured rate. The scan runs row 0..465 from TE's falling edge at
   LCD_H / (period - blank). Then no tear (a column broken where the scan passed it) and, mostly, no wait for TE: on
   average the TE wait was half a frame (~8 ms) of every frame. -1: TE's timing unknown (then wait for TE as before). */
#define SCAN_MARGIN 14 /* rows */
static bool scan_clear(int64_t t0, int64_t fall, int32_t P, int32_t active, float y1, float rows, float t_us, float vs)
{
    /* times in us from t0 (floats lose whole ms on a clock of hours) */
    int32_t since = (int32_t)(t0 - fall) % P; /* the last scan began `since` before t0 */
    for (int s = 0; s < 2; s++) {
        float T = t_us * (s ? 1.15f : 0.85f), vw = rows / T; /* the write from 0 to T */
        for (int k = -1; (float)(k * P - since) < T; k++) {
            float fk = (float)(k * P - since), a = fmaxf(0, fk), b = fminf(T, fk + active);
            if (a >= b) continue; /* that scan does not overlap the write */
            float da = (a - fk) * vs - (y1 + a * vw), db = (b - fk) * vs - (y1 + b * vw);
            if ((da > 0) != (db > 0) || fabsf(da) < SCAN_MARGIN || fabsf(db) < SCAN_MARGIN) return false;
        }
    }
    return true;
}
static int64_t scan_plan(int y1, int y2, size_t bytes)
{
    int64_t now = esp_timer_get_time(), fall = te_fall;
    int32_t P = te_period, active = te_period - te_blank;
    if (!P || !te_blank || !fall || now - fall > 100000 || active < 10000) return -1;
    float vs = (float)LCD_H / active;                  /* rows a us */
    float rate = stream_mb_s > 5 ? stream_mb_s : 25;   /* bytes a us */
    float t_us = bytes / rate + 400;                   /* the window commands and the first chunk */
    for (int c = 0; c < 36; c++) {
        int64_t t0 = now + 300 + c * 500;
        if (scan_clear(t0, fall, P, active, (float)y1, (float)(y2 - y1 + 1), t_us, vs)) return t0;
    }
    return -1;
}
static void wait_until(int64_t t)
{
    int64_t d = t - esp_timer_get_time();
    if (d > 1500) vTaskDelay(pdMS_TO_TICKS((d - 1000) / 1000)); /* a tick is 1 ms: the rest spun, under a ms */
    while ((d = t - esp_timer_get_time()) > 0) esp_rom_delay_us(d > 200 ? 200 : (uint32_t)d);
}

static bool IRAM_ATTR chunk_sent(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(chunk_free, &woken);
    return woken == pdTRUE;
}

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    /* direct mode, drawn byte-swapped as the panel wants it: px is the whole frame. Collect the changed rows and,
       when the frame is complete, hand them to the panel task; LVGL may not draw into the frame again until they
       are out (flush_ready comes from there), but the face can render the next frame meanwhile. */
    if (a->y1 < dirty.y1) dirty.y1 = a->y1;
    if (a->y2 > dirty.y2) dirty.y2 = a->y2;
    if (a->x2 >= a->x1 && a->y2 >= a->y1) {
        if (rect_in_n < RECT_MAX) {
            lv_area_t r = {a->x1 & ~1, a->y1 & ~1, a->x2 | 1, a->y2 | 1};
            if (r.x2 >= LCD_W) r.x2 = LCD_W - 1;
            if (r.y2 >= LCD_H) r.y2 = LCD_H - 1;
            rect_in[rect_in_n] = r;
        }
        rect_in_n++;
    }
    if (!lv_display_flush_is_last(d)) { lv_display_flush_ready(d); return; }
    render_ms += ((esp_timer_get_time() - render_start) / 1000.0f - render_ms) * 0.1f;
    acc(&pf_render, (esp_timer_get_time() - render_start) / 1000.0f);
    box = (lv_area_t){0, dirty.y1 & ~1, LCD_W - 1, dirty.y2 | 1}; /* whole rows: contiguous in the frame */
    if (box.y2 >= LCD_H) box.y2 = LCD_H - 1;
    dirty = (lv_area_t){0, LCD_H, LCD_W - 1, -1};
    /* areas that overlap across and join with little waste become one (a column's body, ledges and lit strip: one
       full-height window, not four): each window costs its commands in every band it crosses */
    int nr = rect_in_n < RECT_MAX ? rect_in_n : RECT_MAX;
    for (bool joined = true; joined;) {
        joined = false;
        for (int i = 0; i < nr && !joined; i++)
            for (int j = i + 1; j < nr && !joined; j++) {
                lv_area_t *a = &rect_in[i], *b = &rect_in[j];
                if (a->x2 < b->x1 || b->x2 < a->x1) continue; /* side by side: never */
                lv_area_t u = {LV_MIN(a->x1, b->x1), LV_MIN(a->y1, b->y1), LV_MAX(a->x2, b->x2), LV_MAX(a->y2, b->y2)};
                if (lv_area_get_size(&u) * 10 > (lv_area_get_size(a) + lv_area_get_size(b)) * 13) continue;
                *a = u, *b = rect_in[--nr], joined = true;
            }
    }
    if (rect_in_n <= RECT_MAX) rect_in_n = nr;
    /* windows, if the areas are few and cover under 60% of their rows; else the rows as one window */
    int32_t cover = 0;
    for (int i = 0; i < rect_in_n && i < RECT_MAX; i++) cover += lv_area_get_size(&rect_in[i]);
    bool rects = rect_in_n <= RECT_MAX && box.y2 >= box.y1 && cover * 10 < (int32_t)(box.y2 - box.y1 + 1) * LCD_W * 6;
    rect_out_n = rects ? rect_in_n : 0;
    if (rects) memcpy(rect_out_l, rect_in, sizeof(lv_area_t) * rect_in_n);
    rect_in_n = 0;
    frame = (const uint16_t *)px, disp_flushing = d;
    xSemaphoreTake(flushed, 0); /* a "done" left over from a flush nobody waited for */
    xTaskNotify(panel_handle, PANEL_STREAM, eSetBits);
}

/* One address window for the rows, then the pixels in chunks: copied from the PSRAM frame into one internal
   buffer while the other goes out by DMA (RAMWR, then "memory write continue"). No allocation per frame: the SPI
   driver's own bounce buffers came from the little internal RAM left with BLE on and slowed the stream. */
static void rect_out(int x1, int x2, int y1, int y2) /* a window of the frame: x even..odd, y even..odd */
{
    const int ox = 6; /* the panel's column offset, as the BSP sets it */
    uint8_t ca[4] = {(x1 + ox) >> 8, (x1 + ox) & 0xFF, (x2 + ox) >> 8, (x2 + ox) & 0xFF}, ra[4] = {y1 >> 8, y1 & 0xFF, y2 >> 8, y2 & 0xFF};
    esp_lcd_panel_io_tx_param(lcd_io, 0x02 << 24 | 0x2A << 8, ca, 4);
    esp_lcd_panel_io_tx_param(lcd_io, 0x02 << 24 | 0x2B << 8, ra, 4);
    size_t row = (size_t)(x2 - x1 + 1) * 2, total = row * (y2 - y1 + 1), off = 0;
    const uint8_t *src = (const uint8_t *)(frame + y1 * LCD_W + x1);
    bool whole = x1 == 0 && x2 == LCD_W - 1; /* whole rows lie one after another in the frame */
    size_t per = whole ? LCD_CHUNK : LCD_CHUNK / row * row; /* else whole rows of the window a chunk */
    for (int n = 0; off < total; n ^= 1) {
        size_t len = total - off < per ? total - off : per;
        if (xSemaphoreTake(chunk_free, pdMS_TO_TICKS(100)) != pdTRUE) break; /* a lost frame beats a frozen screen */
        if (whole) memcpy(chunk_buf[n], src + off, len);
        else
            for (size_t k = 0, r = off / row; k < len; k += row, r++) memcpy(chunk_buf[n] + k, src + r * LCD_W * 2, row);
        esp_lcd_panel_io_tx_color(lcd_io, 0x32 << 24 | (off ? 0x3C : 0x2C) << 8, chunk_buf[n], len);
        off += len;
    }
    for (int k = 0; k < 2; k++) xSemaphoreTake(chunk_free, pdMS_TO_TICKS(100)); /* all out */
    for (int k = 0; k < 2; k++) xSemaphoreGive(chunk_free);
}

/* the changed windows, top to bottom in bands of RECT_BAND rows, every window's part of a band in turn: the write runs
   down the screen far faster than the scan (Flappy's three columns: ~80 rows a ms against its ~30), so the scan,
   starting right behind it at the blanking, never catches a column half sent */
#define RECT_BAND 64
static void rects_out(int y1, int y2)
{
    for (int y = y1; y <= y2; y += RECT_BAND)
        for (int i = 0; i < rect_out_n; i++) {
            const lv_area_t *r = &rect_out_l[i];
            int a = r->y1 > y ? r->y1 : y, b = r->y2 < y + RECT_BAND - 1 ? r->y2 : y + RECT_BAND - 1;
            if (a <= b) rect_out(r->x1, r->x2, a, b);
        }
}

/* sends the changed rows: on the next blanking, in one go (one address window, the SPI driver streams the rest).
   Above LVGL's priority so it starts the moment the pulse comes. (Pausing the face meanwhile was tried: the stream
   got no faster, the face 8 ms slower.) */
static void panel_task(void *arg)
{
    for (;;) {
        /* dark: nothing to stream; pm_lit wakes it (PANEL_WAKE) the moment the screen is wanted again */
        uint32_t got = 0;
        xTaskNotifyWait(0, UINT32_MAX, &got, pdMS_TO_TICKS(panel_lit || panel_want ? 50 : 2000));
        bool req = got & PANEL_STREAM;
        if (panel_want != panel_lit || bright_changed) { /* on or off: from here, so nothing else talks to the panel meanwhile */
            bright_changed = false;
            int was = panel_lit;
            panel_lit = panel_want;
            if (!panel_lit) brightness(0), esp_lcd_panel_disp_on_off(lcd_panel, false);
            else {
                if (!was) esp_lcd_panel_disp_on_off(lcd_panel, true);
                brightness(bright_shown * panel_lit / 100);
            }
            ESP_LOGI(TAG, "screen %s, %d%%", !panel_lit ? "off" : panel_lit < 100 ? "dimmed" : "on", bright_shown * panel_lit / 100);
        }
        if (!req) continue;
        int64_t t0 = esp_timer_get_time();
        lv_area_t r = box;
        size_t bytes = (size_t)(r.y2 - r.y1 + 1) * LCD_W * 2;
        if (rect_out_n) {
            bytes = 0;
            for (int i = 0; i < rect_out_n; i++) bytes += (size_t)lv_area_get_size(&rect_out_l[i]) * 2;
        }
        int64_t start = scan_plan(r.y1, r.y2, bytes); /* when the write cannot meet the scan */
        if (start >= 0) wait_until(start);
        else {
            xSemaphoreTake(te_sem, 0);                 /* an old pulse does not count */
            xSemaphoreTake(te_sem, pdMS_TO_TICKS(25)); /* go on regardless if TE ever stops */
        }
        int64_t t1 = esp_timer_get_time();
        frames_sent++;
        if (rect_out_n) rects_out(r.y1, r.y2), acc(&pf_rects, (float)rect_out_n);
        else rect_out(0, LCD_W - 1, r.y1, r.y2);
        int64_t t2 = esp_timer_get_time();
        lv_display_flush_ready(disp_flushing);
        xSemaphoreGive(flushed);
        te_wait_ms += ((t1 - t0) / 1000.0f - te_wait_ms) * 0.1f;
        acc(&pf_te, (t1 - t0) / 1000.0f), acc(&pf_stream, (t2 - t1) / 1000.0f), acc(&pf_rows, (float)(r.y2 - r.y1 + 1));
        if (t2 > t1) stream_rows_per_ms += ((r.y2 - r.y1 + 1) * 1000.0f / (t2 - t1) - stream_rows_per_ms) * 0.1f;
        if (t2 > t1 && bytes > 20000) stream_mb_s += ((float)bytes / (t2 - t1) - stream_mb_s) * 0.1f; /* what scan_plan expects */
    }
}

/* LVGL waits for a flush by spinning unless given this: block instead, and leave core 0 to the face and BLE */
static void flush_wait(lv_display_t *d)
{
    (void)d;
    xSemaphoreTake(flushed, pdMS_TO_TICKS(200)); /* given by the panel task right after flush_ready */
}

/* The touch as bsp_touch_new makes it, but with a finite I2C timeout. The BSP leaves it 0 = for ever: a read the
   CST9217 NACKed while the bus stayed busy spun in the I2C driver's wait (i2c_master.c, `while (i2c_ll_is_bus_busy)`
   bounded only by that timeout) - LVGL's task never came back, his face froze and nothing answered until the top key
   powered him off (the task watchdog named lvgl in touch_read every 5 s). */
#define TOUCH_I2C_MS 30
static uint32_t touch_errs, touch_resets; /* reads failed in a row; bus resets done (the log) */

/* Every finger. The component reads the CST9217's report for one point only (10 bytes, CST9217_MAX_TOUCH_POINTS 1):
   its read is swapped for this one, the whole report - byte 5 the count, byte 6 the 0xAB ack, then 5 bytes a point (the
   first at 0, the rest after the two header bytes). Measured (debug `touchraw`): this panel reports two points at most (a
   third finger never shows), and a palm sets bit 7 of a point's last byte (touch_rep[4], [11]) - never a finger.
   The same register write, 2 ms, read as the component's; two tries, then the error goes to touch_read's count. */
#define TOUCH_PTS 5
static uint8_t touch_rep[TOUCH_PTS * 5 + 5];
uint8_t touch_raw5; /* the last report's byte 5 (debug.c: @F) */
static bool touch_palm; /* the panel says a palm is on it */
bool touch_raw_trace;
static esp_err_t touch_read_all(esp_lcd_touch_handle_t tp)
{
    uint8_t reg[2] = {0xD0, 0x00};
    esp_err_t r = ESP_FAIL;
    for (int k = 0; k < 2 && r != ESP_OK; k++) {
        if ((r = esp_lcd_panel_io_tx_param(tp->io, reg[0], &reg[1], 1)) != ESP_OK) continue;
        vTaskDelay(pdMS_TO_TICKS(2));
        r = esp_lcd_panel_io_rx_param(tp->io, -1, touch_rep, sizeof touch_rep);
    }
    if (r != ESP_OK) return r;
    if (touch_rep[6] != 0xAB) return ESP_ERR_INVALID_RESPONSE;
    touch_raw5 = touch_rep[5];
    if (touch_raw_trace) { /* debug `touchraw 1`: the whole report when it changes (a palm: what does the chip say?) */
        static uint8_t was[sizeof touch_rep];
        if (memcmp(was, touch_rep, sizeof was)) {
            char h[sizeof touch_rep * 3 + 8];
            int k = snprintf(h, sizeof h, "@R");
            for (unsigned i = 0; i < sizeof touch_rep; i++) k += snprintf(h + k, sizeof h - k, " %02X", touch_rep[i]);
            puts(h), memcpy(was, touch_rep, sizeof was);
        }
    }
    int n = touch_rep[5] & 0x7F;
    n = n > TOUCH_PTS ? TOUCH_PTS : n;
    touch_palm = n > 0 && ((touch_rep[4] & 0x80) || (n > 1 && (touch_rep[11] & 0x80))); /* lifted, the flag lingers a report: not then */
    portENTER_CRITICAL(&tp->data.lock);
    tp->data.points = 0;
    for (int i = 0; i < n; i++) {
        const uint8_t *q = &touch_rep[i * 5 + (i ? 2 : 0)];
        if ((q[0] & 0x0F) != 0x06) continue; /* 6: down; anything else is a lifted one */
        int k = tp->data.points++;
        tp->data.coords[k].x = (uint16_t)(q[1] << 4 | q[3] >> 4);
        tp->data.coords[k].y = (uint16_t)(q[2] << 4 | (q[3] & 0x0F));
    }
    portEXIT_CRITICAL(&tp->data.lock);
    return ESP_OK;
}

static void touch_new(void)
{
    const esp_lcd_touch_config_t cfg = {
        .x_max = BSP_LCD_H_RES, .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = BSP_LCD_TOUCH_RST, .int_gpio_num = BSP_LCD_TOUCH_INT,
        .levels = {.reset = 0, .interrupt = 0},
        .flags = {.swap_xy = 0, .mirror_x = 1, .mirror_y = 1},
    };
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    io_cfg.scl_speed_hz = CONFIG_BSP_I2C_CLK_SPEED_HZ;
    io_cfg.transaction_timeout_ms = TOUCH_I2C_MS;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(bsp_i2c_get_handle(), &io_cfg, &io));
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst9217(io, &cfg, &lcd_touch));
    lcd_touch->read_data = touch_read_all; /* every finger, not just the first */
}

static void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_point_data_t p;
    uint8_t n = 0;
    if (esp_lcd_touch_read_data(lcd_touch) != ESP_OK) { /* timed out or NACKed: no finger this round */
        /* the third failure in a row: the bus stuck - clock it free (nine pulses, a stop), once a run of failures (a read
           that works ends the run). Runs of failures are ordinary (at boot ~100 in a row: the CST9217 not answering yet).
           Reset again and again within a run (every third failure, 1.3.25..1.3.29; tried every 50th in 1.3.30) the resets
           kept the reads failing, ~15 a second for good, on a bus the IMU, the PMU and the codecs share: at boot the ES7210
           failed to open, the BSP asserted and the board rebooted again and again (Nov 2026, bench logs). The freeze
           ("nothing but the top key") is TOUCH_I2C_MS's to prevent: no read waits longer than that */
        if (++touch_errs == 3) {
            if (codec_mx && xSemaphoreTake(codec_mx, 0) != pdTRUE) touch_errs = 2; /* a codec being set up: at the next failure */
            else {
                i2c_master_bus_reset(bsp_i2c_get_handle());
                if (codec_mx) xSemaphoreGive(codec_mx);
                if (++touch_resets <= 3 || touch_resets % 100 == 0) /* not a line each */
                    ESP_LOGW(TAG, "touch: I2C failed 3 times, bus reset (%u)", (unsigned)touch_resets);
            }
        }
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    touch_errs = 0;
    esp_lcd_touch_point_data_t pts[TOUCH_PTS];
    esp_lcd_touch_get_data(lcd_touch, pts, &n, TOUCH_PTS); /* mirrored as the config says, every point */
    int16_t xs[TOUCH_PTS], ys[TOUCH_PTS];
    for (int i = 0; i < n; i++) xs[i] = (int16_t)pts[i].x, ys[i] = (int16_t)pts[i].y;
    pip_fingers(n, xs, ys, touch_palm && n > 0); /* two, three fingers, a palm: before LVGL hears of the first */
    p = pts[0];
    if (n) data->point.x = p.x, data->point.y = p.y;
    data->state = n ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (n) wk_press++;
    if (n && !spk_warm && audio_handle) spk_warm = true, xTaskNotifyGive(audio_handle); /* the speaker ready for the wheel's clicks */
}

static uint32_t tick_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void lvgl_task(void *arg)
{
    bool kicked = false;
    for (;;) {
        wk_lvgl++;
        bool dark = !panel_lit && !panel_want;
        ui_lock();
        touch_gate(dark);
        if (kicked) pip_frame_ready(); /* the face's next frame is done: swap it in now, not at the tick's next 4 ms */
        uint32_t wait = lv_timer_handler();
        bool face = pip_drawing();
        ui_unlock();
        cpu_update(!dark, face);
        for (int i = 0; i < 20 && ui_waiting; i++) vTaskDelay(1); /* someone is waiting for the lock: let them have it */
        /* Pip's tick asks every 4 ms (a finished frame wakes us anyway); off his face nothing needs us oftener than every
           8 ms (LVGL's refresh and the touch are 16 ms); dark, nothing is drawn: twice a second, or a touch, or the light */
        uint32_t floor = face ? 1 : 8, cap = dark ? DARK_POLL_MS : 10;
        if (dark) wait = cap;
        kicked = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait < floor ? floor : wait > cap ? cap : wait)) > 0;
    }
}

static lv_display_t *display_start(void)
{
    /* the panel as bsp_display_new() sets it up, but at 80 MHz instead of 40 */
    const co5300_lcd_init_cmd_t init[] = { /* used during esp_lcd_panel_init only */
        {0xFE, (uint8_t[]){0x20}, 1, 0}, {0x19, (uint8_t[]){0x10}, 1, 0}, {0x1C, (uint8_t[]){0xA0}, 1, 0},
        {0xFE, (uint8_t[]){0x00}, 1, 0}, {0xC4, (uint8_t[]){0x80}, 1, 0}, {0x3A, (uint8_t[]){0x55}, 1, 0},
        {0x35, (uint8_t[]){0x00}, 1, 0}, /* TE on: the pulse on GPIO 13 */
        {0x53, (uint8_t[]){0x20}, 1, 0}, {0x51, (uint8_t[]){0xFF}, 1, 0}, {0x63, (uint8_t[]){0xFF}, 1, 0},
        {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0}, {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600},
        {0x11, NULL, 0, 600}, {0x29, NULL, 0, 0},
    };
    const spi_bus_config_t bus = CO5300_PANEL_BUS_QSPI_CONFIG(BSP_LCD_PCLK, BSP_LCD_DATA0, BSP_LCD_DATA1, BSP_LCD_DATA2, BSP_LCD_DATA3,
                                                              LCD_CHUNK);
    ESP_ERROR_CHECK(spi_bus_initialize(BSP_LCD_SPI_NUM, &bus, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io_cfg = CO5300_PANEL_IO_QSPI_CONFIG(BSP_LCD_CS, chunk_sent, NULL);
    io_cfg.pclk_hz = LCD_QSPI_HZ;
    io_cfg.trans_queue_depth = LCD_QUEUE;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_NUM, &io_cfg, &lcd_io));
    co5300_vendor_config_t vendor = {.init_cmds = init, .init_cmds_size = sizeof init / sizeof init[0], .flags = {.use_qspi_interface = 1}};
    const esp_lcd_panel_dev_config_t pcfg = {.reset_gpio_num = BSP_LCD_RST, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
                                             .bits_per_pixel = BSP_LCD_BITS_PER_PIXEL, .vendor_config = &vendor};
    ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(lcd_io, &pcfg, &lcd_panel));
    esp_lcd_panel_set_gap(lcd_panel, 0x06, 0);
    esp_lcd_panel_reset(lcd_panel);
    esp_lcd_panel_init(lcd_panel);
    esp_lcd_panel_disp_on_off(lcd_panel, true);

    te_sem = xSemaphoreCreateBinary();
    chunk_free = xSemaphoreCreateCounting(2, 2);
    for (int k = 0; k < 2; k++) chunk_buf[k] = heap_caps_malloc(LCD_CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(chunk_buf[0] && chunk_buf[1]);
    flushed = xSemaphoreCreateBinary();
    ui_mutex = xSemaphoreCreateRecursiveMutex();
    gpio_config_t te = {.pin_bit_mask = 1ULL << LCD_TE_GPIO, .mode = GPIO_MODE_INPUT, .intr_type = GPIO_INTR_ANYEDGE};
    gpio_config(&te);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(LCD_TE_GPIO, te_isr, NULL);

    lv_init();
    lv_tick_set_cb(tick_ms);
    lv_display_t *disp = lv_display_create(LCD_W, LCD_H);
    size_t fb_size = LCD_W * LCD_H * 2;
    void *fb = heap_caps_aligned_alloc(64, fb_size, MALLOC_CAP_SPIRAM);
    assert(fb);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED); /* big-endian, as the panel takes it */
    lv_display_set_buffers(disp, fb, NULL, fb_size, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_flush_wait_cb(disp, flush_wait);

    touch_new();
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read);
    touch_indev = indev;
    gpio_isr_handler_add(BSP_LCD_TOUCH_INT, touch_isr, NULL), gpio_intr_disable(BSP_LCD_TOUCH_INT);
    esp_sleep_enable_gpio_wakeup(); /* the touch INT and the bottom key, armed while dark */
    xTaskCreatePinnedToCore(panel_task, "panel", 4096, NULL, 7, &panel_handle, 0); /* the handle before any flush_cb */
    return disp;
}

/* Pip's eyes on both cores: the face task and this worker take rows from one queue (face.c), so whichever core is
   free does more; a fixed split left core 1 idle a third of the time while LVGL held core 0 */
static TaskHandle_t face_worker;
static SemaphoreHandle_t face_done;
static struct { void (*job)(void *, int); void *ctx; } face_band;

static void face_worker_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int64_t t = esp_timer_get_time();
        face_band.job(face_band.ctx, 1);
        face_last_worker_us = (float)(esp_timer_get_time() - t);
        face_ms[2] += (face_last_worker_us / 1000.0f - face_ms[2]) * 0.1f;
        acc(&pf_band1, face_last_worker_us / 1000.0f);
        xSemaphoreGive(face_done);
    }
}

#define FACE_PRIO 4       /* drawing: under LVGL (6) and the panel (7) on core 0, which take their turns */
#define FACE_START_PRIO 7 /* starting a frame: over LVGL, so the worker on core 1 is not left waiting for its refresh */
static void face_two_cores(void (*job)(void *, int), void *ctx)
{
    face_band.job = job, face_band.ctx = ctx;
    int64_t t0 = esp_timer_get_time();
    xTaskNotifyGive(face_worker);
    vTaskPrioritySet(NULL, FACE_PRIO); /* the worker is off: from here LVGL and the panel go first on this core */
    job(ctx, 0);
    int64_t t1 = esp_timer_get_time();
    xSemaphoreTake(face_done, portMAX_DELAY);
    int64_t t2 = esp_timer_get_time();
    face_ms[0] += ((t2 - t0) / 1000.0f - face_ms[0]) * 0.1f;
    if ((t2 - t0) / 1000.0f > face_worst) face_worst = (t2 - t0) / 1000.0f;
    face_ms[1] += ((t1 - t0) / 1000.0f - face_ms[1]) * 0.1f;
    acc(&pf_face, (t2 - t0) / 1000.0f), acc(&pf_band0, (t1 - t0) / 1000.0f);
}

/* his birth's sky (cosmos.c), drawn by LVGL's task: the worker takes half of it - the face is not drawn meanwhile */
static void cosmos_two_cores(void (*job)(void *, int), void *ctx)
{
    if (face_busy) { job(ctx, 0); return; }
    face_band.job = job, face_band.ctx = ctx;
    xTaskNotifyGive(face_worker);
    job(ctx, 0);
    xSemaphoreTake(face_done, portMAX_DELAY);
}

/* The face renders in the background: this task draws the next frame (with the worker, on both cores) while LVGL
   shows the current one. Below LVGL's priority on core 0, so LVGL's own drawing goes first there. */
static TaskHandle_t face_master;
static struct { face_t *f; uint16_t *buf; int w, h; } face_job;

static void face_master_task(void *arg)
{
    for (;;) {
        vTaskPrioritySet(NULL, FACE_START_PRIO);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        face_draw(face_job.f, face_job.buf, face_job.w, face_job.h);
        face_busy = 0;
        if (lvgl_handle) xTaskNotifyGive(lvgl_handle); /* LVGL takes it at once */
    }
}

static void face_start_bg(face_t *f, uint16_t *buf, int w, int h)
{
    face_job.f = f, face_job.buf = buf, face_job.w = w, face_job.h = h;
    face_busy = 1;
    xTaskNotifyGive(face_master);
}

/* how long LVGL spends on a refresh (render + flush to the panel), averaged, for the status line */
static int64_t refr_start;
static void refr_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_RENDER_START) { /* the areas are joined by now: how many, how many px */
        render_start = esp_timer_get_time();
        lv_display_t *d = lv_event_get_current_target(e);
        int n = 0, px = 0;
        for (uint32_t i = 0; i < d->inv_p; i++)
            if (!d->inv_area_joined[i]) n++, px += lv_area_get_size(&d->inv_areas[i]);
        acc(&pf_areas, (float)n), acc(&pf_px, (float)px);
        return;
    }
    if (lv_event_get_code(e) == LV_EVENT_REFR_START) refr_start = esp_timer_get_time();
    else {
        float ms = (esp_timer_get_time() - refr_start) / 1000.0f;
        refr_ms += (ms - refr_ms) * 0.1f;
        acc(&pf_refr, ms);
        if (ms > refr_worst) refr_worst = ms, refr_worst_on = shell_where();
    }
}

void app_main(void)
{
    pm_start();
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    face_done = xSemaphoreCreateBinary();
    codec_mx = xSemaphoreCreateMutex(); /* before the touch and the audio task: see codec_hold */
    /* on the core LVGL is not pinned to; same priority so it gets the CPU as soon as a band is ready */
    xTaskCreatePinnedToCore(face_worker_task, "face", 6144, NULL, 6, &face_worker, 1);
    face_parallel = face_two_cores;
    cosmos_parallel = cosmos_two_cores;
    xTaskCreatePinnedToCore(face_master_task, "face bg", 6144, NULL, FACE_START_PRIO, &face_master, 0);
    face_start = face_start_bg;
    lv_display_t *disp = display_start();
    brightness(bright);
    bright_shown = bright;
    pm_keep_pins(); /* after the drivers set them up */

    ui_lock();
    lv_rand_set_seed(esp_random());
    theme_load(); /* before the UI is built in it */
    privacy_load();
    settings_load();
    menu_load();
    lv_display_add_event_cb(disp, refr_cb, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(disp, refr_cb, LV_EVENT_REFR_READY, NULL);
    lv_display_add_event_cb(disp, refr_cb, LV_EVENT_RENDER_START, NULL);
    shell_create();
    pip_wake_up(); /* out of black, sleepy, a shake */
    pet_load();
    shell_time(pet_clock(), tz_min);
    ui_unlock();
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 6, &lvgl_handle, 0); /* core 0; the face worker has core 1 */

    voice_init(AUDIO_RATE);
    xTaskCreate(sensors_task, "sensors", 4096, NULL, 5, NULL);
    xTaskCreate(audio_task, "audio", 4096, NULL, 6, NULL);
    ble_start();
    debug_start();
}
