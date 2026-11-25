/* The menu's apps. Each one builds its screen in open() and cleans its timers in close().
   Colours, fonts, the clock face and the meters come from the current theme (theme.h). */
#include "maze.h"
#include "mic.h"
#include "pet.h"
#include "phone.h"
#include "pip.h"
#include "shell.h"
#include "theme.h"
#include "voice.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SCR 466

static lv_obj_t *dot(lv_obj_t *parent, int x, int y, int size, uint32_t color)
{
    return th_box(parent, x - size / 2, y - size / 2, size, size, LV_RADIUS_CIRCLE, color);
}


static struct tm local_now(void)
{
    time_t now = (time_t)shell_local_time();
    struct tm tm;
    gmtime_r(&now, &tm);
    return tm;
}

static const char *const WD[] = {"вс", "пн", "вт", "ср", "чт", "пт", "сб"};
static const char *const MON[] = {"янв", "фев", "мар", "апр", "мая", "июн", "июл", "авг", "сен", "окт", "ноя", "дек"};

/* ================= clock: a dial, or the terminal's block digits ================= */

static struct {
    lv_obj_t *scr, *date, *window, *hands[3];
    bool unknown; /* built as "время не настроено": no dial until a phone or the panel tells the time */
    lv_obj_t *grid; /* the terminal face: one object for all the cells */
    th_cell_t tc[7 * 25 + 60]; /* "HH:MM" in 5x7 blocks, the seconds ring */
    int nd;
    lv_point_precise_t pts[3][2], ticks[60][2];
    lv_timer_t *timer;
} ck;

static void clock_build(lv_obj_t *scr);
static void clock_tick(lv_timer_t *t)
{
    (void)t;
    if (ck.unknown == shell_clock_is_known()) { lv_obj_clean(ck.scr); clock_build(ck.scr); return; } /* the time came (or went) */
    if (ck.unknown) return;
    struct tm tm = local_now();
    char d[48];
    snprintf(d, sizeof d, "%s  %d %s", WD[tm.tm_wday], tm.tm_mday, MON[tm.tm_mon]);
    lv_label_set_text(ck.date, th_label(d));
    if (TH_TERMINAL) {
        snprintf(d, sizeof d, "%02d:%02d", tm.tm_hour, tm.tm_min);
        th_blocks_show(ck.grid, ck.tc, d, th->text);
        for (int i = 0; i < 60; i++) th_cell_set(ck.grid, &ck.tc[ck.nd + i], i < tm.tm_sec ? th->text : th->off); /* a cell a second */
        return;
    }
    float ang[3] = {(tm.tm_hour % 12 + tm.tm_min / 60.0f) * 30, (tm.tm_min + tm.tm_sec / 60.0f) * 6, tm.tm_sec * 6.0f};
    const float len[3] = {112, 168, 186}, tail[3] = {16, 20, 36};
    for (int i = 0; i < 3; i++) {
        float a = ang[i] * 3.14159f / 180, sx = sinf(a), cy = -cosf(a);
        ck.pts[i][0] = (lv_point_precise_t){SCR / 2 - sx * tail[i], SCR / 2 - cy * tail[i]};
        ck.pts[i][1] = (lv_point_precise_t){SCR / 2 + sx * len[i], SCR / 2 + cy * len[i]};
        lv_line_set_points(ck.hands[i], ck.pts[i], 2);
    }
    lv_label_set_text_fmt(ck.window, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
}

/* Switched off or run flat since a phone or the panel last told the time: there is no time to show, only that */
static void clock_unknown(lv_obj_t *scr)
{
    lv_obj_t *l = th_text_at(scr, th->body, th->text, "Время не\nсинхронизировано", -1, 160); /* one line is wider than the round */
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(l, 4, 0);
    th_text_at(scr, th->small, th->dim, "Нужно Bluetooth подключение", -1, 252);
}

static void clock_build(lv_obj_t *scr)
{
    ck.unknown = !shell_clock_is_known();
    if (ck.unknown) { clock_unknown(scr); return; }
    if (TH_TERMINAL) {
        ck.nd = th_blocks(ck.tc, 97, 195, "00:00", 11, 9); /* the digits in the middle, the date under them */
        th_ring(ck.tc + ck.nd, 60, 214, 8);
        ck.grid = th_cells(scr, ck.tc, ck.nd + 60);
        ck.date = th_text_at(scr, th->small, th->dim, "", -1, 300);
    } else { /* a dial: ticks, the four numbers, a window with the time and the date under it */
        for (int i = 0; i < 60; i++) {
            float a = i * 6 * 3.14159f / 180, r0 = i % 5 ? 202 : 186;
            ck.ticks[i][0] = (lv_point_precise_t){SCR / 2 + sinf(a) * r0, SCR / 2 - cosf(a) * r0};
            ck.ticks[i][1] = (lv_point_precise_t){SCR / 2 + sinf(a) * 214, SCR / 2 - cosf(a) * 214};
            lv_obj_t *l = lv_line_create(scr);
            lv_line_set_points(l, ck.ticks[i], 2);
            lv_obj_set_style_line_width(l, i % 5 ? 2 : 4, 0);
            lv_obj_set_style_line_color(l, lv_color_hex(i % 15 == 0 ? th->accent : i % 5 ? th->faint : th->text), 0);
        }
        static const char *const nums[4] = {"12", "3", "6", "9"};
        for (int i = 0; i < 4; i++) {
            float a = i * 90 * 3.14159f / 180;
            lv_obj_t *n = th_text(scr, th->body, th->text, nums[i]);
            lv_obj_align(n, LV_ALIGN_CENTER, (int)(sinf(a) * 156), (int)(-cosf(a) * 156));
        }
        ck.window = th_text(scr, th->small, th->dim, "");
        lv_obj_set_style_border_width(ck.window, 2, 0);
        lv_obj_set_style_border_color(ck.window, lv_color_hex(th->faint), 0);
        lv_obj_set_style_radius(ck.window, th->radius, 0);
        lv_obj_set_style_pad_hor(ck.window, 12, 0);
        lv_obj_set_style_pad_ver(ck.window, 4, 0);
        lv_obj_align(ck.window, LV_ALIGN_CENTER, 0, 78);
        ck.date = th_text(scr, th->small, th->dim, ""); /* the date under the time, over the 6 */
        lv_obj_align(ck.date, LV_ALIGN_CENTER, 0, 114);
        const int width[3] = {10, 6, 3};
        const uint32_t color[3] = {th->text, th->text, th->accent};
        for (int i = 0; i < 3; i++) {
            ck.hands[i] = lv_line_create(scr);
            lv_obj_set_style_line_width(ck.hands[i], width[i], 0);
            lv_obj_set_style_line_rounded(ck.hands[i], true, 0);
            lv_obj_set_style_line_color(ck.hands[i], lv_color_hex(color[i]), 0);
        }
        dot(scr, SCR / 2, SCR / 2, 18, th->accent);
    }
    clock_tick(NULL);
}

static void clock_open(lv_obj_t *scr)
{
    ck.scr = scr;
    clock_build(scr);
    ck.timer = lv_timer_create(clock_tick, 250, NULL);
}

static void clock_close(void) { lv_timer_delete(ck.timer); }

/* ================= timer: drag the arc to set minutes, it keeps running when you leave ================= */

static struct {
    int minutes;
    bool running, open;
    uint32_t end_tick, left_ms; /* left_ms when paused */
    lv_obj_t *arc, *time, *start;
    lv_timer_t *ui;
    /* the terminal face: a ring of 60 one-minute cells with a knob, block digits, two buttons */
    lv_obj_t *grid, *sub, *go, *pause, *reset;
    th_cell_t tc[70 + 147 + 60 + 2]; /* "MM" while setting, "MM:SS" while counting, the ring, the knob */
} tm_ = {.minutes = 5};
#define TM_MM 0
#define TM_MMSS 70
#define TM_RING (70 + 147)
#define TM_KNOB (70 + 147 + 60)

static uint32_t timer_left(void)
{
    if (!tm_.running) return tm_.left_ms;
    int32_t left = (int32_t)(tm_.end_tick - lv_tick_get());
    return left > 0 ? (uint32_t)left : 0;
}

static void timer_ui_term(void)
{
    uint32_t left = timer_left();
    bool fresh = !tm_.running && left == (uint32_t)tm_.minutes * 60000;
    char t[32];
    if (fresh) {
        snprintf(t, sizeof t, "%02d", tm_.minutes);
        th_blocks_hide(tm_.grid, tm_.tc + TM_MMSS, 147);
        th_blocks_show(tm_.grid, tm_.tc + TM_MM, t, th->text);
        lv_label_set_text(tm_.sub, th_label("минут"));
    } else {
        snprintf(t, sizeof t, "%02u:%02u", (unsigned)(left / 60000), (unsigned)(left / 1000 % 60));
        th_blocks_hide(tm_.grid, tm_.tc + TM_MM, 70);
        th_blocks_show(tm_.grid, tm_.tc + TM_MMSS, t, tm_.running ? th->text : th->dim);
        lv_label_set_text(tm_.sub, th_label("осталось"));
    }
    /* a cell a minute: set, or still to go; the running minute blinks */
    int lit = fresh ? tm_.minutes : (int)((left + 59999) / 60000);
    bool blink = tm_.running && lv_tick_get() / 500 % 2;
    for (int i = 0; i < 60; i++)
        th_cell_set(tm_.grid, &tm_.tc[TM_RING + i], i < lit - 1 || (i == lit - 1 && !blink) ? th->text : i == lit - 1 ? th->dim : th->off);
    th_cell_t *k = &tm_.tc[TM_KNOB];
    float a = (tm_.minutes - 0.5f) * 6.2831853f / 60 - 1.5707963f;
    int kx = (int)lroundf(SCR / 2 + cosf(a) * 196), ky = (int)lroundf(SCR / 2 + sinf(a) * 196);
    if (k[0].x != kx - 11 || k[0].y != ky - 11 || (k[0].color == TH_NONE) == fresh) {
        k[0] = (th_cell_t){(int16_t)(kx - 11), (int16_t)(ky - 11), 22, 22, fresh ? th->text : TH_NONE};
        k[1] = (th_cell_t){(int16_t)(kx - 4), (int16_t)(ky - 4), 8, 8, fresh ? th->bg : TH_NONE};
        lv_obj_invalidate(tm_.grid);
    }
    lv_obj_set_flag(tm_.go, LV_OBJ_FLAG_HIDDEN, tm_.running);
    lv_obj_set_flag(tm_.pause, LV_OBJ_FLAG_HIDDEN, !tm_.running);
    lv_obj_set_flag(tm_.reset, LV_OBJ_FLAG_HIDDEN, fresh); /* a fresh timer: start alone, in the middle */
    lv_obj_set_x(tm_.go, fresh ? 163 : 93), lv_obj_set_x(tm_.pause, 93);
}

static void timer_ui(lv_timer_t *t)
{
    (void)t;
    if (!tm_.open) return;
    if (TH_TERMINAL) { timer_ui_term(); return; }
    uint32_t left = timer_left();
    bool fresh = !tm_.running && left == (uint32_t)tm_.minutes * 60000;
    lv_label_set_text_fmt(tm_.time, "%02u:%02u", (unsigned)(left / 60000), (unsigned)((left / 1000) % 60));
    lv_label_set_text(lv_obj_get_child(tm_.start, 0), th_label(tm_.running ? "пауза" : "старт"));
    if (!fresh) lv_arc_set_value(tm_.arc, (int32_t)((left + 59999) / 60000));
    /* a fresh timer has nothing to reset: start alone, in the middle (as Ретро has it) */
    lv_obj_set_flag(tm_.reset, LV_OBJ_FLAG_HIDDEN, fresh);
    lv_obj_align(tm_.start, LV_ALIGN_CENTER, fresh ? 0 : -70, 88);
    lv_obj_set_state(tm_.arc, LV_STATE_DISABLED, !fresh); /* the knob only sets a fresh timer */
}

/* time's up: a card over everything, whatever is on screen; a tap or any button puts it away and hushes it (even the
   button that lights a dark screen: shell.c). Until then the timer's
   song rings again and again, as a call does, and keeps the screen lit */
#define ALARM_EVERY_MS 2400 /* the song is 1.65 s: a breath between */
static lv_obj_t *alert;
static lv_timer_t *alert_ring;
bool apps_alert_on(void) { return alert != NULL; }
void apps_alert_close(void)
{
    if (alert_ring) lv_timer_delete(alert_ring), alert_ring = NULL, voice_hush(); /* the song stops with it, not at its end */
    if (alert) lv_obj_delete_async(alert), alert = NULL;
}
static void alert_again(lv_timer_t *t)
{
    (void)t;
    pip_wake(); /* no dimming while it rings */
    if (!shell_in_setup()) voice_say(VOICE_ALARM); /* under the setup screen nothing can put it away: it rings after */
}
static void alert_tap(lv_event_t *e) { (void)e; apps_alert_close(); }

static void alert_show(int minutes)
{
    apps_alert_close();
    alert = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(alert);
    lv_obj_set_size(alert, SCR, SCR);
    th_screen(alert);
    lv_obj_add_flag(alert, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(alert, alert_tap, LV_EVENT_CLICKED, NULL);
    th_header(alert, shell_title("timer"), 40);
    const lv_font_t *big = th->title ? th->title : th->body;
    int h = lv_font_get_line_height(big), y = 200 - h / 2; /* the words around the middle, the button under them */
    th_text_at(alert, big, th->text, th_label("Время вышло"), -1, y);
    char t[32];
    snprintf(t, sizeof t, "%02d:00 прошло", minutes);
    th_text_at(alert, th->small, th->dim, th_label(t), -1, y + h + 4);
    th_button_at(alert, 163, 290, 140, th_label("ок"), true, alert_tap);
    alert_ring = lv_timer_create(alert_again, ALARM_EVERY_MS, NULL); /* the first time is his "time's up" */
}

static void timer_watch(lv_timer_t *t) /* runs always: rings even when the app is closed */
{
    (void)t;
    if (tm_.running && timer_left() == 0) {
        tm_.running = false;
        tm_.left_ms = (uint32_t)tm_.minutes * 60000;
        if (pip_dead()) return; /* he is gone: no card no button could put away, no alarm lighting his screen */
        shell_home();
        pip_react(PIP_SURPRISED, 4, "time's up");
        pip_wake();
        alert_show(tm_.minutes);
    }
}

static void timer_set(lv_event_t *e)
{
    (void)e;
    tm_.minutes = lv_arc_get_value(tm_.arc);
    tm_.left_ms = (uint32_t)tm_.minutes * 60000;
    timer_ui(NULL);
}

static void timer_start(lv_event_t *e)
{
    (void)e;
    if (tm_.running) tm_.left_ms = timer_left(), tm_.running = false;
    else tm_.end_tick = lv_tick_get() + tm_.left_ms, tm_.running = true;
    timer_ui(NULL);
}

static void timer_reset(lv_event_t *e)
{
    (void)e;
    tm_.running = false;
    tm_.left_ms = (uint32_t)tm_.minutes * 60000;
    if (tm_.arc) lv_arc_set_value(tm_.arc, tm_.minutes);
    timer_ui(NULL);
}

/* the timer is worked by taps only: the top button does nothing here, the bottom one goes back (it keeps running) */

static void timer_drag(lv_event_t *e) /* the terminal ring: drag round it to set the minutes */
{
    (void)e;
    if (tm_.running || timer_left() != (uint32_t)tm_.minutes * 60000) return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    float dx = p.x - SCR / 2, dy = p.y - SCR / 2;
    if (hypotf(dx, dy) < 140) return; /* the middle is for the button */
    float a = atan2f(dx, -dy) * 57.2958f;
    int m = (int)ceilf((a < 0 ? a + 360 : a) / 6);
    tm_.minutes = m < 1 ? 1 : m > 60 ? 60 : m;
    tm_.left_ms = (uint32_t)tm_.minutes * 60000;
    timer_ui(NULL);
}

static void timer_open_term(lv_obj_t *scr)
{
    th_blocks(tm_.tc + TM_MM, 174, 163, "00", 11, 9);
    th_blocks(tm_.tc + TM_MMSS, 97, 163, "00:00", 11, 9);
    th_ring(tm_.tc + TM_RING, 60, 196, 9);
    tm_.tc[TM_KNOB] = tm_.tc[TM_KNOB + 1] = (th_cell_t){0, 0, 1, 1, TH_NONE};
    tm_.grid = th_cells(scr, tm_.tc, TM_KNOB + 2);
    tm_.sub = th_text_at(scr, th->tiny, th->dim, "", -1, 251);
    lv_obj_set_style_text_letter_space(tm_.sub, 2, 0);
    tm_.go = th_button_at(scr, 163, 286, 140, "старт", true, timer_start);
    tm_.pause = th_button_at(scr, 93, 286, 136, "пауза", false, timer_start);
    tm_.reset = th_button_at(scr, 237, 286, 136, "сброс", false, timer_reset);
    lv_obj_set_width(tm_.go, 136);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, timer_drag, LV_EVENT_PRESSING, NULL);
}

static void timer_open(lv_obj_t *scr)
{
    if (!tm_.running && !tm_.left_ms) tm_.left_ms = (uint32_t)tm_.minutes * 60000;
    tm_.open = true;
    if (TH_TERMINAL) {
        timer_open_term(scr);
        timer_ui(NULL);
        tm_.ui = lv_timer_create(timer_ui, 100, NULL);
        return;
    }
    tm_.arc = lv_arc_create(scr);
    lv_obj_set_size(tm_.arc, 400, 400);
    lv_obj_center(tm_.arc);
    lv_arc_set_range(tm_.arc, 1, 60);
    lv_arc_set_value(tm_.arc, tm_.minutes);
    lv_arc_set_bg_angles(tm_.arc, 135, 45);
    lv_obj_set_style_arc_width(tm_.arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(tm_.arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(tm_.arc, th->radius != 0, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(tm_.arc, th->radius != 0, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(tm_.arc, lv_color_hex(th->faint), LV_PART_MAIN);
    lv_obj_set_style_arc_color(tm_.arc, lv_color_hex(th->accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(tm_.arc, lv_color_hex(th->text), LV_PART_KNOB);
    lv_obj_set_style_radius(tm_.arc, th->radius, LV_PART_KNOB);
    lv_obj_set_style_pad_all(tm_.arc, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(tm_.arc, timer_set, LV_EVENT_VALUE_CHANGED, NULL);
    tm_.time = th_text(scr, th->digits, th->text, "");
    lv_obj_align(tm_.time, LV_ALIGN_CENTER, 0, -30);
    tm_.start = th_button(scr, "", true, timer_start);
    lv_obj_set_size(tm_.start, 128, 52);
    lv_obj_align(tm_.start, LV_ALIGN_CENTER, -70, 88);
    tm_.reset = th_button(scr, "сброс", false, timer_reset);
    lv_obj_set_size(tm_.reset, 128, 52);
    lv_obj_align(tm_.reset, LV_ALIGN_CENTER, 70, 88);
    timer_ui(NULL);
    tm_.ui = lv_timer_create(timer_ui, 200, NULL);
}

static void timer_close(void)
{
    lv_timer_delete(tm_.ui);
    tm_.arc = NULL;
    tm_.open = false;
}

/* ================= torch: the whole AMOLED as a white lamp; drag left/right for brightness ================= */

static struct {
    lv_obj_t *scr, *hint;
    float level;
    int32_t drag_x;
} tc = {.level = 1};

static void torch_paint(void)
{
    uint8_t v = (uint8_t)(255 * tc.level);
    lv_obj_set_style_bg_image_src(tc.scr, NULL, 0);
    lv_obj_set_style_bg_color(tc.scr, lv_color_make(v, v, v), 0);
    lv_obj_set_style_bg_opa(tc.scr, LV_OPA_COVER, 0); /* a grid theme leaves the screen's own fill transparent */
}

static void torch_input(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (code == LV_EVENT_PRESSED) tc.drag_x = p.x;
    else if (code == LV_EVENT_PRESSING) { /* sideways, the brightness */
        tc.level = fmaxf(0.15f, fminf(1, tc.level + (p.x - tc.drag_x) / 300.0f));
        tc.drag_x = p.x;
        torch_paint();
    }
}

static void torch_open(lv_obj_t *scr)
{
    tc.scr = scr;
    torch_paint();
    tc.hint = th_text(scr, th->small, 0x787878, "влево/вправо — яркость"); /* 0x777777 tints green in RGB565 */
    lv_obj_center(tc.hint);
    lv_obj_fade_out(tc.hint, 600, 1800);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, torch_input, LV_EVENT_ALL, NULL);
}

/* ================= Pip: needs, feed and play; the meters follow the theme ================= */

/* cells: lit one at a time up (or down) to the value by care_fill, as the terminal's meters do */
/* The cells are th_cells in one object for all the meters (care.grid), not an object each: 8 x 16 small objects ate
   ~20 KB of internal RAM, and the Bluetooth controller waking from modem sleep then failed its esp_timer_create (abort) */
#define METER_MAX 16
typedef struct { th_cell_t *c; lv_obj_t *val; int n, shown, want; uint32_t color, ink; } meter_t;

static void meter_make(meter_t *m, lv_obj_t *scr, int x, int y, uint32_t color, th_cell_t *cells)
{
    memset(m, 0, sizeof *m);
    m->color = color, m->c = cells;
    bool dots = th->meter == METER_DOTS;
    m->n = dots ? 10 : METER_MAX;
    for (int i = 0; i < METER_MAX; i++)
        cells[i] = i >= m->n ? (th_cell_t){0, 0, 1, 1, TH_NONE}
                 : dots      ? (th_cell_t){(int16_t)(x + i * 13), (int16_t)(y + 4), 10, 10, th->faint}
                             : (th_cell_t){(int16_t)(x + i * 9), (int16_t)y, 6, 18, th->faint};
    m->val = th_text(scr, th->small, th->text, ""); /* the number after the meter: in every theme */
    lv_obj_set_pos(m->val, x + (dots ? 10 * 13 + 4 : 16 * 9 + 4), y - 2);
}

static lv_obj_t *meter_grid; /* the cells of every meter on the screen */
static void meter_cell(meter_t *m, int i, uint32_t color) { th_cell_set(meter_grid, &m->c[i], color); }

static void meter_set(meter_t *m, float v)
{
    v = v > 0 ? v < 1 ? v : 1 : 0; /* NaN too: never a cell past the meter */
    bool low = v < 0.25f;
    uint32_t c = low ? th->accent : m->color;
    if (c != m->ink) /* crossed the low line: what is lit takes the new ink at once */
        for (int i = 0; i < m->shown; i++) meter_cell(m, i, c);
    m->ink = c, m->want = (int)roundf(v * m->n);
    if (m->val) lv_label_set_text_fmt(m->val, "%d", (int)(v * 100));
}

#define CARE_X 210 /* where the meters start */
#define CARE_N 8 /* his mood on the whole first; food, energy, fun, love, walks, clean, and trust (no need: how sure of you he has grown) */
static struct {
    meter_t m[CARE_N];
    lv_obj_t *grid, *vals[CARE_N], *days, *days_l, *steps, *steps_l;
    th_cell_t tc[CARE_N * METER_MAX]; /* the meters' cells: the terminal's 12 each, the others' METER_MAX */
    int shown[CARE_N], want[CARE_N];
    lv_timer_t *timer, *fill;
} care;

/* terminal meters: 12 cells each, filling a cell every 40 ms up (or down) to the value */
static void care_fill(lv_timer_t *t)
{
    (void)t;
    if (!TH_TERMINAL) {
        for (int i = 0; i < CARE_N; i++) {
            meter_t *m = &care.m[i];
            if (m->shown == m->want) continue;
            if (m->shown < m->want) meter_cell(m, m->shown++, m->ink);
            else meter_cell(m, --m->shown, th->faint);
        }
        return;
    }
    for (int i = 0; i < CARE_N; i++) {
        if (care.shown[i] == care.want[i]) continue;
        care.shown[i] += care.shown[i] < care.want[i] ? 1 : -1;
        for (int k = 0; k < 12; k++) th_cell_set(care.grid, &care.tc[i * 12 + k], k < care.shown[i] ? th->text : th->off);
    }
}

static const char *days_word(int n) /* 1 день, 2 дня, 5 дней, 21 день */
{
    int t = n % 100, u = n % 10;
    return t >= 11 && t <= 14 ? "дней" : u == 1 ? "день" : u >= 2 && u <= 4 ? "дня" : "дней";
}

static const char *steps_word(int n) /* 1 шаг, 2 шага, 5 шагов */
{
    int t = n % 100, u = n % 10;
    return t >= 11 && t <= 14 ? "шагов" : u == 1 ? "шаг" : u >= 2 && u <= 4 ? "шага" : "шагов";
}

static void care_tick(lv_timer_t *t)
{
    (void)t;
    pip_info_t in;
    pip_get_info(&in);
    float v[CARE_N] = {in.mood_level, in.food, in.energy, in.fun, in.love, in.walk, in.clean, in.trust};
    if (TH_TERMINAL) {
        for (int i = 0; i < CARE_N; i++) {
            care.want[i] = (int)lroundf((v[i] > 0 ? v[i] < 1 ? v[i] : 1 : 0) * 12);
            lv_label_set_text_fmt(care.vals[i], "%d%%", (int)(v[i] * 100));
            lv_obj_set_style_text_color(care.vals[i], lv_color_hex(v[i] < 0.25f ? th->dim : th->text), 0);
        }
    } else
        for (int i = 0; i < CARE_N; i++) meter_set(&care.m[i], v[i]);
    /* his age and today's walk: a big number each, the word under it */
    lv_label_set_text_fmt(care.days, "%d", in.days);
    lv_label_set_text(care.days_l, th_label(TH_TERMINAL ? (in.days == 1 ? "день" : days_word(in.days)) : days_word(in.days)));
    lv_label_set_text_fmt(care.steps, "%d", in.steps);
    lv_label_set_text(care.steps_l, th_label(steps_word(in.steps)));
}

/* a number over its word, centred on x */
static int care_stat(lv_obj_t *scr, int x, int y, lv_obj_t **num, lv_obj_t **word) /* returns the y under it */
{
    const lv_font_t *big = !TH_TERMINAL && th->title && lv_font_get_line_height(th->title) <= 48 ? th->title : th->body;
    *num = th_text(scr, big, th->text, "0");
    lv_obj_set_width(*num, 150);
    lv_obj_align(*num, LV_ALIGN_TOP_MID, x - SCR / 2, y);
    *word = th_text(scr, TH_TERMINAL ? th->tiny : th->small, th->dim, "");
    lv_obj_set_width(*word, 150);
    const lv_font_t *small = TH_TERMINAL ? th->tiny : th->small;
    lv_obj_align(*word, LV_ALIGN_TOP_MID, x - SCR / 2, y + lv_font_get_line_height(big) + 2);
    return y + lv_font_get_line_height(big) + 2 + lv_font_get_line_height(small);
}

static void care_open_term(lv_obj_t *scr, const char *const *names)
{
    th_header(scr, shell_title("care"), 40); /* as the other themes title it */
    for (int i = 0; i < CARE_N; i++) {
        int y = 100 + i * 25; /* his mood first, the main one: in the text's ink */
        th_text_at(scr, th->tiny, i ? th->dim : th->text, names[i], 86, y - 2);
        care.vals[i] = th_text_at(scr, th->tiny, th->text, "", 340, y - 2);
        lv_obj_set_width(care.vals[i], 48);
        lv_obj_set_style_text_align(care.vals[i], LV_TEXT_ALIGN_RIGHT, 0);
        for (int k = 0; k < 12; k++) care.tc[i * 12 + k] = (th_cell_t){(int16_t)(206 + k * 11), (int16_t)y, 9, 16, th->off};
        care.shown[i] = 0;
    }
    care_stat(scr, SCR / 2 - 70, 326, &care.days, &care.days_l); /* level with the other themes' (their numbers are bigger) */
    care_stat(scr, SCR / 2 + 70, 326, &care.steps, &care.steps_l);
    th_box(scr, SCR / 2 - 1, 330, 2, 52, 0, th->faint); /* between the two */
    care.grid = th_cells(scr, care.tc, CARE_N * 12);
    care.fill = lv_timer_create(care_fill, 40, NULL);
}

static void care_open(lv_obj_t *scr)
{
    static const char *const names[CARE_N] = {"Настрой", "Еда", "Энергия", "Веселье", "Внимание", "Прогулки", "Чистота", "Доверие"};
    if (TH_TERMINAL) {
        care_open_term(scr, names);
        care_tick(NULL);
        care.timer = lv_timer_create(care_tick, 500, NULL);
        return;
    }
    lv_obj_t *title = th_text(scr, th->body, th->text, shell_title("care")); /* as the menu calls him */
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 50);
    for (int i = 0; i < CARE_N; i++) { /* his mood first, the main one: its word and meter in the text's ink */
        int y = 92 + i * 27;
        lv_obj_t *l = th_text(scr, th->small, i ? th->dim : th->text, names[i]);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_pos(l, 62, y);
        meter_make(&care.m[i], scr, CARE_X, y + 2, i ? th->good : th->text, &care.tc[i * METER_MAX]);
    }
    meter_grid = th_cells(scr, care.tc, CARE_N * METER_MAX);
    th_cells_round(meter_grid, th->meter == METER_DOTS ? LV_RADIUS_CIRCLE : 1);
    /* under the meters with the same gap as between them */
    care_stat(scr, SCR / 2 - 80, 318, &care.days, &care.days_l);
    int under = care_stat(scr, SCR / 2 + 80, 318, &care.steps, &care.steps_l);
    th_box(scr, SCR / 2 - 1, 324, 2, under - 330, th->radius ? 1 : 0, th->faint); /* a hairline between them */
    care_tick(NULL);
    care.timer = lv_timer_create(care_tick, 500, NULL);
    care.fill = lv_timer_create(care_fill, 40, NULL); /* the meters fill up cell by cell */
}

static void care_close(void)
{
    lv_timer_delete(care.timer);
    if (care.fill) lv_timer_delete(care.fill), care.fill = NULL;
}

/* ================= ball: roll onto the hearts before the ring runs out; touching the rim starts over ===== */

#define ROUND_MS 20000 /* a full ring */
#define HEART_MS 2500  /* each heart buys a little time back */
static struct {
    lv_obj_t *ball, *star, *score, *ring, *over, *over_text;
    lv_obj_t *grid, *rows[2]; /* terminal: rings of cells, the end-of-round card */
    th_cell_t tc[60 + 96], ball_tc[9], heart_tc[25];
    int32_t flash_ms;
    lv_timer_t *timer;
    float x, y, vx, vy, sx, sy, gx, gy, bx, by;
    bool calibrated, ended, played; /* played: a real round (3 or more) this time it was open */
    int points, best;
    int32_t left_ms;
    uint32_t last;
} gm;

static void game_score(void)
{
    if (TH_TERMINAL) {
        lv_label_set_text_fmt(gm.score, "%s %04d", th_label("счёт"), gm.points);
        lv_obj_remove_flag(gm.score, LV_OBJ_FLAG_HIDDEN);
    } else lv_label_set_text_fmt(gm.score, "%d", gm.points);
}

static void game_ring(void)
{
    if (!TH_TERMINAL) {
        lv_arc_set_value(gm.ring, gm.left_ms * 1000 / ROUND_MS);
        lv_obj_set_style_arc_color(gm.ring, lv_color_hex(gm.flash_ms > 0 ? 0xFF4245 : th->accent), LV_PART_INDICATOR);
        return;
    }
    int lit = (int)((gm.left_ms * 60 + ROUND_MS - 1) / ROUND_MS);
    bool flash = gm.flash_ms > 0, on = gm.flash_ms / 100 % 2; /* three blinks when the rim is touched */
    for (int i = 0; i < 60; i++) th_cell_set(gm.grid, &gm.tc[i], flash ? (on ? th->text : th->off) : i < lit ? th->text : th->off);
}

static void game_place_star(void)
{
    float a = (float)lv_rand(0, 6283) / 1000, r = (float)lv_rand(30, 150);
    gm.sx = SCR / 2 + cosf(a) * r, gm.sy = SCR / 2 + sinf(a) * r;
    lv_obj_set_pos(gm.star, (int32_t)gm.sx - lv_obj_get_width(gm.star) / 2, (int32_t)gm.sy - lv_obj_get_height(gm.star) / 2);
}

static void game_best(void)
{
    if (gm.points >= 3) gm.played = true;
    if (gm.points <= gm.best) return;
    if (gm.best >= 3) pip_game(PIP_GAME_RECORD); /* beat a real score: he is amazed */
    gm.best = gm.points;
}

static void game_restart(void)
{
    gm.x = gm.y = SCR / 2, gm.vx = gm.vy = 0;
    gm.points = 0, gm.left_ms = ROUND_MS, gm.ended = false;
    game_score();
    lv_obj_add_flag(gm.over, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(gm.ball, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(gm.star, LV_OBJ_FLAG_HIDDEN);
    game_place_star();
}

static void game_tick(lv_timer_t *t)
{
    (void)t;
    uint32_t el = lv_tick_elaps(gm.last);
    gm.last = lv_tick_get();
    if (gm.ended) return;
    float dt = fminf(el / 1000.0f, 0.05f);
    if ((gm.left_ms -= (int32_t)el) <= 0) { /* time's up */
        gm.ended = true, gm.left_ms = 0;
        game_best();
        char s[96];
        if (TH_TERMINAL) {
            snprintf(s, sizeof s, "%04d", gm.points);
            th_leader(gm.rows[0], "счёт", s, 202);
            snprintf(s, sizeof s, "%04d", gm.best);
            th_leader(gm.rows[1], "лучший", s, 202);
            lv_obj_add_flag(gm.score, LV_OBJ_FLAG_HIDDEN);
            gm.flash_ms = 0;
        } else {
            snprintf(s, sizeof s, "Время вышло\nсчёт %d, лучший %d", gm.points, gm.best);
            lv_label_set_text(gm.over_text, th_label(s));
        }
        lv_obj_remove_flag(gm.over, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(gm.ball, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(gm.star, LV_OBJ_FLAG_HIDDEN);
    }
    gm.flash_ms -= (int32_t)el;
    game_ring();
    gm.vx += gm.gx * 1600 * dt, gm.vy += gm.gy * 1600 * dt;
    gm.vx *= 0.985f, gm.vy *= 0.985f;
    gm.x += gm.vx * dt, gm.y += gm.vy * dt;
    float rim = TH_TERMINAL ? 196 - 2 - 16 : SCR / 2 - 18 - 22;
    if (hypotf(gm.x - SCR / 2, gm.y - SCR / 2) > rim) { /* touched the rim: the count starts over */
        game_best();
        game_restart();
        gm.flash_ms = 600;
        return;
    }
    if (hypotf(gm.x - gm.sx, gm.y - gm.sy) < 38) {
        gm.points++;
        gm.left_ms = gm.left_ms + HEART_MS > ROUND_MS ? ROUND_MS : gm.left_ms + HEART_MS;
        game_score();
        game_place_star();
    }
    lv_obj_set_pos(gm.ball, (int32_t)gm.x - lv_obj_get_width(gm.ball) / 2, (int32_t)gm.y - lv_obj_get_height(gm.ball) / 2);
}

static void game_imu(float ax, float ay, float az, float dt)
{
    (void)az;
    if (!gm.calibrated) gm.bx = ax, gm.by = ay, gm.calibrated = true;
    float k = 1 - expf(-dt / 6.0f); /* slowly follow the way the keychain is held */
    gm.bx += (ax - gm.bx) * k, gm.by += (ay - gm.by) * k;
    gm.gx = -(ax - gm.bx), gm.gy = ay - gm.by; /* downhill on the screen */
}

static void game_tap(lv_event_t *e)
{
    (void)e;
    if (gm.ended) game_restart();
}

static bool game_button(int id) /* top starts a new round once time is up */
{
    if (id != PIP_BTN_PWR || !gm.ended) return false;
    game_restart();
    return true;
}

static lv_obj_t *cell_sprite(lv_obj_t *scr, th_cell_t *c, const char *const *rows, int n, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(scr);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, (int)strlen(rows[0]) * 11 - 2, n * 11 - 2);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    th_cells(o, c, th_pattern(c, rows, n, 0, 0, 11, 9, color));
    return o;
}

static void game_open_term(lv_obj_t *scr)
{
    th_ring(gm.tc, 60, 216, 8);
    th_ring(gm.tc + 60, 96, 196, 3); /* the rim: touch it and the run starts over */
    gm.grid = th_cells(scr, gm.tc, 60 + 96);
    gm.score = th_text_at(scr, th->tiny, th->mute, "", -1, 97);
    static const char *const heart[] = {"01110", "11111", "11111", "11111", "01110"}, *const ball[] = {"111", "111", "111"}; /* the target: a round dot */
    gm.star = cell_sprite(scr, gm.heart_tc, heart, 5, th->dim);
    gm.ball = cell_sprite(scr, gm.ball_tc, ball, 3, th->text);
    gm.over = lv_obj_create(scr); /* the end of a round: a plate, the score, again */
    lv_obj_remove_style_all(gm.over);
    lv_obj_set_size(gm.over, SCR, SCR);
    lv_obj_remove_flag(gm.over, LV_OBJ_FLAG_CLICKABLE);
    th_plate(gm.over, 83, 140, 300, 48, th->body, "время вышло", false, NULL);
    for (int i = 0; i < 2; i++) gm.rows[i] = th_text_at(gm.over, th->tiny, i ? th->dim : th->text, "", 132, 213 + i * 26);
    th_button_at(gm.over, 153, 282, 160, "ещё раз", true, game_tap);
}

static void game_open(lv_obj_t *scr)
{
    int best = gm.best;
    memset(&gm, 0, sizeof gm);
    gm.best = best;
    gm.last = lv_tick_get();
    gm.ring = lv_arc_create(scr); /* the time left, running down around the rim */
    lv_obj_set_size(gm.ring, SCR - 16, SCR - 16);
    lv_obj_center(gm.ring);
    lv_arc_set_range(gm.ring, 0, 1000);
    lv_arc_set_bg_angles(gm.ring, 0, 360);
    lv_arc_set_rotation(gm.ring, 270);
    lv_obj_set_style_arc_width(gm.ring, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(gm.ring, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(gm.ring, th->radius != 0, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(gm.ring, lv_color_hex(th->faint), LV_PART_MAIN);
    lv_obj_set_style_opa(gm.ring, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_remove_flag(gm.ring, LV_OBJ_FLAG_CLICKABLE);
    if (TH_TERMINAL) lv_obj_delete(gm.ring), gm.ring = NULL;
    if (TH_TERMINAL) {
        game_open_term(scr);
        lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(scr, game_tap, LV_EVENT_CLICKED, NULL);
        game_restart();
        gm.timer = lv_timer_create(game_tick, 16, NULL);
        return;
    }
    gm.score = th_text(scr, th->digits, th->faint, "0");
    lv_obj_center(gm.score);
    gm.star = th_box(scr, 0, 0, 30, 30, th->radius ? LV_RADIUS_CIRCLE : 0, th->accent); /* the target: a dot to roll onto */
    gm.ball = !TH_TERMINAL ? dot(scr, SCR / 2, SCR / 2, 44, th->text) : th_box(scr, SCR / 2 - 20, SCR / 2 - 20, 40, 40, 0, th->text);
    gm.over = lv_obj_create(scr); /* the end of a round: the score on a card, and the same "ещё раз" as in Ретро */
    lv_obj_remove_style_all(gm.over);
    lv_obj_set_size(gm.over, SCR, SCR);
    lv_obj_remove_flag(gm.over, LV_OBJ_FLAG_CLICKABLE);
    gm.over_text = th_text(gm.over, th->body, th->text, "");
    lv_obj_set_style_bg_color(gm.over_text, lv_color_hex(th->panel), 0);
    lv_obj_set_style_bg_opa(gm.over_text, LV_OPA_90, 0);
    lv_obj_set_style_pad_all(gm.over_text, 18, 0);
    lv_obj_set_style_radius(gm.over_text, th->radius ? 24 : 0, 0);
    lv_obj_align(gm.over_text, LV_ALIGN_CENTER, 0, -36);
    th_button_at(gm.over, 153, 282, 160, "ещё раз", true, game_tap);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, game_tap, LV_EVENT_CLICKED, NULL);
    game_restart();
    gm.timer = lv_timer_create(game_tick, 16, NULL);
}

static void game_close(void)
{
    lv_timer_delete(gm.timer);
    if (gm.played) pip_action(PIP_ACT_PLAY); /* playing with him counts: this time, not a record kept from before */
}

/* ================= maze: tilt the ball from the top left corner to the mark in the bottom right ================= */
/* Four sizes, each solved maze leads to a bigger one; the top button deals a new one. The walls are one A8 image in
   the theme's ink, drawn once a maze; the ball and the mark are small objects. The physics is ui/maze.c. */

#define MZ_SIDE 308                /* the maze's square, centred: its corners stay inside the round glass */
#define MZ_IMG (MZ_SIDE + 12)      /* the walls' image, with room for their thickness round the edge */
#define MZ_PUSH 1100               /* px/s^2 for a tilt of 1 g */
static const int mz_sizes[] = {6, 8, 10, 12};
static struct {
    maze_t m;
    lv_obj_t *walls, *ball, *goal, *label;
    lv_timer_t *timer;
    float bx, by, gx, gy, next_in; /* the hand's usual pose, the push now; next_in > 0: solved, the next one comes */
    bool calibrated, won; /* won: one solved this time it was open */
    int level, solved;
    uint32_t last;
} mz;
static lv_draw_buf_t *mz_buf; /* kept between games: one allocation */

static float mz_cover(float a0, float a1, int p) { return fmaxf(0, fminf(a1, p + 1.0f) - fmaxf(a0, (float)p)); } /* of pixel p */

static void mz_fill(float x0, float y0, float x1, float y1) /* a rectangle, its edges antialiased, image px */
{
    uint8_t *px = mz_buf->data;
    int stride = (int)mz_buf->header.stride;
    for (int y = (int)fmaxf(0, floorf(y0)); y < (int)fminf(MZ_IMG, ceilf(y1)); y++) {
        float cy = mz_cover(y0, y1, y);
        for (int x = (int)fmaxf(0, floorf(x0)); x < (int)fminf(MZ_IMG, ceilf(x1)); x++) {
            int a = px[y * stride + x] + (int)(255 * cy * mz_cover(x0, x1, x) + 0.5f);
            px[y * stride + x] = (uint8_t)(a > 255 ? 255 : a);
        }
    }
}

static void mz_draw(void)
{
    static float r[MAZE_MAX * MAZE_MAX * 2 + MAZE_MAX * 2][4];
    lv_draw_buf_clear(mz_buf, NULL);
    int n = maze_rects(&mz.m, r, sizeof r / sizeof r[0]);
    const float o = (MZ_IMG - MZ_SIDE) / 2.0f; /* the maze's corner in the image */
    for (int k = 0; k < n; k++) {
        float x0 = r[k][0] + o, y0 = r[k][1] + o, x1 = r[k][2] + o, y1 = r[k][3] + o;
        if (!TH_TERMINAL) { mz_fill(x0, y0, x1, y1); continue; }
        /* the terminal: a wall is a row of square cells, crisp, on whole pixels */
        int q = (int)mz.m.t + 2, along = x1 - x0 > y1 - y0;
        float len = along ? x1 - x0 : y1 - y0;
        int cnt = (int)((len - q) / (q + 3)) + 1;
        float gap = cnt > 1 ? (len - q) / (cnt - 1) : 0;
        for (int i = 0; i < cnt; i++) {
            float a = (along ? x0 : y0) + i * gap, b = (along ? (y0 + y1) : (x0 + x1)) / 2 - q / 2.0f;
            float cx = roundf(along ? a : b), cy = roundf(along ? b : a);
            mz_fill(cx, cy, cx + q, cy + q);
        }
    }
    lv_obj_invalidate(mz.walls);
}

static void mz_place(lv_obj_t *o, float x, float y) /* centre an object on a maze point */
{
    lv_obj_set_pos(o, (int32_t)lroundf((SCR - MZ_SIDE) / 2 + x) - lv_obj_get_width(o) / 2,
                   (int32_t)lroundf((SCR - MZ_SIDE) / 2 + y) - lv_obj_get_height(o) / 2);
}

static void mz_new(void)
{
    maze_new(&mz.m, mz_sizes[mz.level], MZ_SIDE, lv_rand(1, 0x7FFFFFFF));
    mz.next_in = 0;
    mz_draw();
    int d = (int)(mz.m.r * 2), g = (int)(mz.m.c * 0.44f);
    lv_obj_set_size(mz.ball, d, d);
    lv_obj_set_size(mz.goal, g, g);
    mz_place(mz.goal, (mz.m.n - 0.5f) * mz.m.c, (mz.m.n - 0.5f) * mz.m.c);
    mz_place(mz.ball, mz.m.x, mz.m.y);
    char s[48];
    snprintf(s, sizeof s, "уровень %d", mz.level + 1); /* the same words in every theme */
    lv_label_set_text(mz.label, th_title(s));
}

static void mz_tick(lv_timer_t *t)
{
    (void)t;
    float dt = fminf(lv_tick_elaps(mz.last) / 1000.0f, 0.05f); /* never more than half a cell a frame: see maze_test */
    mz.last = lv_tick_get();
    if (mz.next_in > 0) { /* solved: a moment to see it, then a bigger one */
        if ((mz.next_in -= dt) <= 0) mz.level = mz.level + 1 < 4 ? mz.level + 1 : 3, mz_new();
        return;
    }
    maze_step(&mz.m, mz.gx * MZ_PUSH, mz.gy * MZ_PUSH, dt);
    mz_place(mz.ball, mz.m.x, mz.m.y);
    if (maze_done(&mz.m)) {
        mz.solved++, mz.won = true, mz.next_in = 0.8f;
        voice_say(VOICE_HAPPY);
    }
}

static void mz_imu(float ax, float ay, float az, float dt)
{
    (void)az;
    if (!mz.calibrated) mz.bx = ax, mz.by = ay, mz.calibrated = true;
    float k = 1 - expf(-dt / 6.0f); /* slowly follow the way the keychain is held, as the ball game does */
    mz.bx += (ax - mz.bx) * k, mz.by += (ay - mz.by) * k;
    mz.gx = -(ax - mz.bx), mz.gy = ay - mz.by; /* downhill on the screen */
}

static bool mz_button(int id) /* top: another maze of this size */
{
    if (id != PIP_BTN_PWR) return false;
    mz_new();
    return true;
}

static void mz_open(lv_obj_t *scr)
{
    int level = mz.level, solved = mz.solved;
    memset(&mz, 0, sizeof mz);
    mz.level = level, mz.solved = solved;
    if (!mz_buf) mz_buf = lv_draw_buf_create(MZ_IMG, MZ_IMG, LV_COLOR_FORMAT_A8, 0);
    LV_ASSERT_MALLOC(mz_buf);
    mz.walls = lv_image_create(scr);
    lv_image_set_src(mz.walls, mz_buf);
    lv_obj_set_pos(mz.walls, (SCR - MZ_IMG) / 2, (SCR - MZ_IMG) / 2);
    lv_obj_set_style_image_recolor(mz.walls, lv_color_hex(TH_TERMINAL ? th->dim : th->text), 0);
    lv_obj_set_style_image_recolor_opa(mz.walls, LV_OPA_COVER, 0);
    if (TH_TERMINAL) {
        mz.label = th_header(scr, "", 40);
        mz.goal = th_cursor(scr, 12, 12, th->text); /* a blinking block, sized per maze below */
        mz.ball = th_box(scr, 0, 0, 10, 10, 0, th->text);
    } else {
        mz.label = th_text(scr, th->small, th->dim, "");
        lv_obj_align(mz.label, LV_ALIGN_TOP_MID, 0, 34);
        mz.goal = dot(scr, 0, 0, 12, th->accent);
        mz.ball = dot(scr, 0, 0, 10, th->text);
    }
    lv_obj_remove_flag(mz.ball, LV_OBJ_FLAG_CLICKABLE);
    mz_new();
    mz.last = lv_tick_get();
    mz.timer = lv_timer_create(mz_tick, 16, NULL);
}

static void mz_close(void)
{
    lv_timer_delete(mz.timer);
    if (mz.won) pip_action(PIP_ACT_PLAY); /* playing with him counts: this time, not one solved before */
}

/* ================= stopwatch: top button starts and marks laps, bottom resets; tap pauses ================= */

/* The time on one line: MM:SS big, .cc smaller on the same baseline right after it, the pair centred. Under it the
   newest SW_SHOWN laps, newest first; older ones are not kept at all (a ring of SW_SHOWN, so a hundred laps cost
   nothing), and their text is rebuilt only when a lap comes or the clock is reset, not every frame. */
#define SW_SHOWN 5
static struct {
    bool running;
    uint32_t start, acc;       /* ms: when it last started, and what came before */
    uint32_t mark;             /* ms on the clock at the last lap mark */
    uint32_t ring[SW_SHOWN];   /* the newest laps' lengths, ms: lap n (1-based) sits at (n - 1) % SW_SHOWN */
    int n;                     /* laps marked so far */
    lv_obj_t *time, *cs, *list;
    lv_timer_t *ui;
    lv_obj_t *grid, *lap_rows[SW_SHOWN]; /* terminal */
    th_cell_t tc[147];
    int drawn;                 /* what the laps were drawn for: -1 = redraw */
    uint32_t shown_s, shown_cs, shown_ink; /* what the clock shows: it is redrawn only when that changes */
} sw;

/* It stops at 99:59.99, as a real one does: the clock shows two digits of minutes (past them it read 00:00 again), and
   left running for weeks the ms would wrap. Nothing else grows: the laps are a ring of SW_SHOWN, counted to 9999. */
#define SW_MAX (100u * 60000u - 10u)
static uint32_t sw_elapsed(void)
{
    if (sw.running && lv_tick_elaps(sw.start) >= SW_MAX - (sw.acc < SW_MAX ? sw.acc : SW_MAX)) sw.acc = SW_MAX, sw.running = false;
    return sw.acc + (sw.running ? lv_tick_elaps(sw.start) : 0);
}

static void sw_lap_text(char *t, size_t n, uint32_t lap)
{
    snprintf(t, n, "%02u:%02u.%02u", (unsigned)(lap / 60000 % 100), (unsigned)(lap / 1000 % 60), (unsigned)(lap / 10 % 100));
}

static void sw_draw_laps(void)
{
    int state = sw.n * 2 + !sw_elapsed(); /* a new lap, or a reset */
    if (state == sw.drawn) return;
    sw.drawn = state;
    char t[24], key[24]; /* "круг" is 8 bytes of UTF-8 */
    if (TH_TERMINAL) {
        for (int k = 0; k < SW_SHOWN; k++) {
            int i = sw.n - k; /* lap number, newest first */
            if (i < 1) { lv_label_set_text(sw.lap_rows[k], ""); continue; }
            snprintf(key, sizeof key, "круг %d", i);
            sw_lap_text(t, sizeof t, sw.ring[(i - 1) % SW_SHOWN]);
            th_leader(sw.lap_rows[k], key, t, 234);
        }
        return;
    }
    char buf[SW_SHOWN * 40] = "";
    size_t len = 0;
    for (int k = 0; k < SW_SHOWN && sw.n - k >= 1; k++) {
        int i = sw.n - k;
        sw_lap_text(t, sizeof t, sw.ring[(i - 1) % SW_SHOWN]);
        len += (size_t)snprintf(buf + len, sizeof buf - len, "%s%s %d   %s", k ? "\n" : "", th->caps ? "КРУГ" : "круг", i, t);
    }
    lv_label_set_text(sw.list, buf); /* five lines are past th_label's 92 bytes: the word comes in the right case instead */
}

static void sw_draw(lv_timer_t *t)
{
    (void)t;
    if (!sw.time) return;
    uint32_t e = sw_elapsed(), secs = e / 1000, cs = e / 10 % 100;
    uint32_t ink = sw.running || !e ? th->text : th->dim; /* paused: the time dims */
    bool fresh = sw.drawn < 0;
    if (fresh || secs != sw.shown_s || ink != sw.shown_ink) {
        sw.shown_s = secs, sw.shown_ink = ink;
        char m[16];
        snprintf(m, sizeof m, "%02u:%02u", (unsigned)(secs / 60 % 100), (unsigned)(secs % 60));
        if (TH_TERMINAL) th_blocks_show(sw.grid, sw.tc, m, ink); /* only the cells that change are redrawn */
        else lv_label_set_text(sw.time, m), lv_obj_set_style_text_color(sw.time, lv_color_hex(ink), 0);
    }
    if (fresh || cs != sw.shown_cs) sw.shown_cs = cs, lv_label_set_text_fmt(sw.cs, ".%02u", (unsigned)cs);
    sw_draw_laps();
}

static bool sw_button(int id)
{
    if (id == PIP_BTN_PWR) { /* start, then a lap mark every press */
        if (!sw.running) sw.start = lv_tick_get(), sw.running = true;
        else if (sw.n < 9999) {
            uint32_t now = sw_elapsed();
            sw.ring[sw.n++ % SW_SHOWN] = now - sw.mark, sw.mark = now;
        }
    } else { /* reset, or leave once there is nothing to reset */
        if (!sw.running && !sw_elapsed()) return false;
        sw.running = false, sw.acc = 0, sw.n = 0, sw.mark = 0;
    }
    sw_draw(NULL);
    return true;
}

static void sw_tap(lv_event_t *e) /* pause and go on */
{
    (void)e;
    if (sw.running) sw.acc = sw_elapsed(), sw.running = false;
    else if (sw.acc) sw.start = lv_tick_get(), sw.running = true;
    sw_draw(NULL);
}

/* where the small .cc goes: right after MM:SS, its baseline on theirs */
static int sw_baseline_y(const lv_font_t *big, int big_y, const lv_font_t *small)
{
    return big_y + (lv_font_get_line_height(big) - big->base_line) - (lv_font_get_line_height(small) - small->base_line);
}

static void sw_open(lv_obj_t *scr)
{
    sw.drawn = -1;
    lv_point_t cw;
    lv_text_get_size(&cw, ".00", th->body, th->caps ? 1 : 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (TH_TERMINAL) {
        const int pitch = 11, w = 24 * pitch + 9, gap = 10, y0 = 140; /* "00:00": 25 columns of cells */
        int x0 = (SCR - (w + gap + cw.x)) / 2;
        th_blocks(sw.tc, x0, y0, "00:00", pitch, 9);
        sw.grid = th_cells(scr, sw.tc, 147);
        sw.time = sw.grid; /* "open" */
        sw.cs = th_text_at(scr, th->body, th->dim, ".00", x0 + w + gap, y0 + 6 * pitch + 9 - lv_font_get_line_height(th->body) + th->body->base_line);
        const uint32_t ink[SW_SHOWN] = {th->text, th->dim, th->dim, th->mute, th->mute};
        for (int k = 0; k < SW_SHOWN; k++) sw.lap_rows[k] = th_text_at(scr, th->tiny, ink[k], "", 116, 252 + k * 26);
    } else {
        lv_point_t tw;
        lv_text_get_size(&tw, "00:00", th->digits, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        const int gap = 6, y = 118;
        int x = (SCR - (tw.x + gap + cw.x)) / 2;
        sw.time = th_text_at(scr, th->digits, th->text, "00:00", x, y);
        sw.cs = th_text_at(scr, th->body, th->dim, ".00", x + tw.x + gap, sw_baseline_y(th->digits, y, th->body));
        sw.list = th_text(scr, th->small, th->text, "");
        lv_obj_set_style_text_line_space(sw.list, 2, 0);
        lv_obj_align(sw.list, LV_ALIGN_TOP_MID, 0, 240);
    }
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, sw_tap, LV_EVENT_SHORT_CLICKED, NULL);
    sw_draw(NULL);
    sw.ui = lv_timer_create(sw_draw, 33, NULL);
}

static void sw_close(void)
{
    lv_timer_delete(sw.ui);
    sw.time = NULL;
}

/* ================= noise: a sound level meter for the studio, the colour says how quiet it is ================= */

#define NZ_LO 20  /* the dial's range, dB */
#define NZ_HI 100
#define NZ_BARS 40 /* the last 20 s, a bar per half second */

static const struct { float below; uint32_t color; const char *word; } nz_bands[] = {
    {30, 0x34C759, "тишина"}, {40, 0x9BE15D, "тихо"}, {55, 0xFFD60A, "разговор"}, {70, 0xFF9F0A, "шумно"}, {999, 0xFF453A, "громко"},
};

static struct {
    lv_obj_t *arc, *num, *unit, *word, *span, *bars;
    th_cell_t cells[NZ_BARS];
    lv_timer_t *timer;
    float slow, peak, lo, hi, hist[NZ_BARS];
    int n, ticks, quiet; /* quiet: ticks with no sound yet (the host opens the mic a moment after the app) */
} nz;

static int nz_band(float db)
{
    int i = 0;
    while (db >= nz_bands[i].below) i++;
    return i;
}

static void nz_reset(void) { nz.lo = 999, nz.hi = 0, nz.n = 0, nz.slow = 0; }

static void nz_update(lv_timer_t *t)
{
    (void)t;
    if (!mic_on()) { /* not yet: the mic comes on within half a second of opening; none after 1.5 s, there is none */
        if (++nz.quiet == 15) lv_label_set_text(nz.word, "нет микрофона");
        return;
    }
    float db = mic_db();
    if (!*lv_label_get_text(nz.unit)) lv_label_set_text(nz.unit, "дБ");
    nz.slow = nz.slow ? nz.slow + (db - nz.slow) * 0.1f : db; /* about a second: the number holds still enough to read */
    nz.peak = fmaxf(nz.peak, db);
    int b = nz_band(nz.slow);
    uint32_t c = nz_bands[b].color;
    lv_arc_set_value(nz.arc, (int32_t)fminf(NZ_HI, fmaxf(NZ_LO, db)));
    lv_obj_set_style_arc_color(nz.arc, lv_color_hex(nz_bands[nz_band(db)].color), LV_PART_INDICATOR);
    lv_label_set_text_fmt(nz.num, "%d", (int)lroundf(nz.slow));
    lv_obj_set_style_text_color(nz.num, lv_color_hex(c), 0);
    lv_label_set_text(nz.word, th_label(nz_bands[b].word));
    lv_obj_set_style_text_color(nz.word, lv_color_hex(c), 0);
    if (++nz.ticks % 5) return;
    /* every half second: a bar of the loudest moment in it, and the session's quietest and loudest */
    nz.lo = fminf(nz.lo, nz.slow), nz.hi = fmaxf(nz.hi, nz.peak);
    memmove(nz.hist, nz.hist + 1, sizeof nz.hist - sizeof nz.hist[0]);
    nz.hist[NZ_BARS - 1] = nz.peak, nz.peak = 0, nz.n++;
    for (int i = 0; i < NZ_BARS; i++) {
        float v = nz.hist[i];
        int h = v > 0 ? 3 + (int)(fminf(1, fmaxf(0, (v - NZ_LO) / (NZ_HI - NZ_LO))) * 44) : 3;
        nz.cells[i].y = (int16_t)(352 - h), nz.cells[i].h = (int16_t)h;
        nz.cells[i].color = v > 0 ? nz_bands[nz_band(v)].color : th_off();
    }
    lv_obj_invalidate(nz.bars);
    char s[48];
    snprintf(s, sizeof s, "мин %d   макс %d", (int)lroundf(nz.lo), (int)lroundf(nz.hi));
    lv_label_set_text(nz.span, th_label(s));
}

static void nz_tap(lv_event_t *e) { (void)e; nz_reset(), memset(nz.hist, 0, sizeof nz.hist), nz.ticks = 4, nz_update(NULL); }

static void nz_close(void)
{
    if (nz.timer) lv_timer_delete(nz.timer), nz.timer = NULL;
}

static void nz_open(lv_obj_t *scr)
{
    nz_reset();
    memset(nz.hist, 0, sizeof nz.hist);
    nz.peak = 0, nz.ticks = 0, nz.quiet = 0;
    /* the dial: 20..100 dB over three quarters of a turn, open at the bottom */
    lv_obj_t *a = nz.arc = lv_arc_create(scr);
    lv_obj_set_size(a, 420, 420);
    lv_obj_center(a);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(a, 135);
    lv_arc_set_bg_angles(a, 0, 270);
    lv_arc_set_range(a, NZ_LO, NZ_HI);
    lv_arc_set_value(a, NZ_LO);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(a, 0, LV_PART_KNOB);
    lv_obj_set_style_arc_width(a, 10, 0);
    lv_obj_set_style_arc_color(a, lv_color_hex(th->faint), 0);
    lv_obj_set_style_arc_width(a, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, th->radius > 0, 0);
    lv_obj_set_style_arc_rounded(a, th->radius > 0, LV_PART_INDICATOR);
    nz.num = th_text(scr, th->digits, th->text, "");
    lv_obj_align(nz.num, LV_ALIGN_CENTER, 0, -50);
    nz.unit = th_text(scr, th->small, th->dim, "");
    lv_label_set_text(nz.unit, mic_on() ? "дБ" : ""); /* the unit keeps its case */
    lv_obj_align(nz.unit, LV_ALIGN_CENTER, 0, 14);
    nz.word = th_text(scr, th->body, th->dim, "");
    lv_obj_align(nz.word, LV_ALIGN_CENTER, 0, 52);
    for (int i = 0; i < NZ_BARS; i++) nz.cells[i] = (th_cell_t){(int16_t)(SCR / 2 - NZ_BARS * 6 / 2 + i * 6), 349, 4, 3, th_off()};
    nz.bars = th_cells(scr, nz.cells, NZ_BARS);
    if (theme_index() == TH_BARBIE) th_cells_round(nz.bars, 2); /* her bars are softly rounded, like the rest of her */
    nz.span = th_text(scr, th->tiny, th_mute(), "");
    lv_obj_align(nz.span, LV_ALIGN_TOP_MID, 0, 364);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE); /* a tap starts the min/max and the bars over */
    lv_obj_add_event_cb(scr, nz_tap, LV_EVENT_SHORT_CLICKED, NULL);
    nz.timer = lv_timer_create(nz_update, 100, NULL);
    nz_update(NULL);
}

/* ================= sounds: how loud his voice is and the interface's, VOICE_STEPS segments a row, set by a tap ================= */
/* A tap on a row sets its level to the segment under the finger (the first one when it is already the level turns it
   off); a taste of the sound plays at the new level. */
#define SND_X 73
#define SND_W 320
#define SND_H 96
#define SND_SEG_W 52
#define SND_SEG_GAP 8
#define SND_SEG_X ((SND_W - VOICE_STEPS * SND_SEG_W - (VOICE_STEPS - 1) * SND_SEG_GAP) / 2)

static struct { lv_obj_t *rows[2], *values[2], *segs[2][VOICE_STEPS]; } snd;

static int snd_level(int i) { return i ? voice_ui_level() : voice_pip_level(); }

static void snd_show(void)
{
    uint32_t on = TH_TERMINAL ? th->text : th->accent;
    for (int i = 0; i < 2; i++) {
        int l = snd_level(i);
        char b[16];
        if (l) snprintf(b, sizeof b, "%d%%", l * 100 / VOICE_STEPS);
        lv_label_set_text(snd.values[i], l ? b : th_label("выкл"));
        lv_obj_set_style_text_color(snd.values[i], lv_color_hex(l ? on : th->dim), 0);
        for (int k = 0; k < VOICE_STEPS; k++) lv_obj_set_style_bg_color(snd.segs[i][k], lv_color_hex(k < l ? on : th->faint), 0);
    }
}

static void snd_tap(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_point_t pt;
    lv_indev_get_point(lv_indev_active(), &pt);
    int x = pt.x - SND_X - SND_SEG_X, seg = x < 0 ? 0 : x / (SND_SEG_W + SND_SEG_GAP);
    seg = seg >= VOICE_STEPS ? VOICE_STEPS - 1 : seg;
    int l = seg == 0 && snd_level(i) == 1 ? 0 : seg + 1; /* the first segment when it is the level: off */
    voice_levels(i ? voice_pip_level() : l, i ? l : voice_ui_level());
    snd_show();
    voice_say(i ? VOICE_TICK : VOICE_HAPPY); /* a taste of it, as loud as it now is (nothing when off) */
}

static void snd_open(lv_obj_t *scr)
{
    static const char *const names[2] = {"Звуки Пипа", "Звуки меню"};
    th_header(scr, shell_title("sound"), 40);
    for (int i = 0; i < 2; i++) {
        lv_obj_t *r = snd.rows[i] = th_box(scr, SND_X, 136 + i * (SND_H + 14), SND_W, SND_H, th->radius ? 16 : 0, th->panel);
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(r, snd_tap, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_align(th_text(r, th->small, th->text, names[i]), LV_ALIGN_TOP_LEFT, SND_SEG_X, 12);
        snd.values[i] = th_text(r, th->small, th->accent, "");
        lv_obj_align(snd.values[i], LV_ALIGN_TOP_RIGHT, -SND_SEG_X, 12);
        for (int k = 0; k < VOICE_STEPS; k++) {
            lv_obj_t *g = snd.segs[i][k] = th_box(r, SND_SEG_X + k * (SND_SEG_W + SND_SEG_GAP), SND_H - 20 - 18, SND_SEG_W, 18,
                                                   th->radius ? 9 : 0, th->faint);
            lv_obj_remove_flag(g, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    snd_show();
}

/* ================= brightness: one row of BR_STEPS segments, 10% each, set by a tap (as the panel's "bright %") ================= */
/* The level lives here (shell_brightness): the host lights the panel by it and keeps it (main.c reads it back each
   second, as the eye colours); 10% is as dim as it goes, so the first segment never turns it off. */
#define BR_STEPS 10
#define BR_SEG_W 24
#define BR_SEG_GAP 6
#define BR_SEG_X ((SND_W - BR_STEPS * BR_SEG_W - (BR_STEPS - 1) * BR_SEG_GAP) / 2)
static int bright_pct = 60; /* as after a clean flash (main.c `bright`, the panel's DEFAULT_CFG) */
void shell_brightness(int pct) { bright_pct = pct < 10 ? 10 : pct > 100 ? 100 : pct; }
int shell_brightness_get(void) { return bright_pct; }

static struct { lv_obj_t *value, *segs[BR_STEPS]; } br;

static void br_show(void)
{
    uint32_t on = TH_TERMINAL ? th->text : th->accent;
    int l = (bright_pct + 5) / 10; /* the panel's slider goes in 5s: 75% lights 8 */
    char b[16];
    snprintf(b, sizeof b, "%d%%", bright_pct);
    lv_label_set_text(br.value, b);
    for (int k = 0; k < BR_STEPS; k++) lv_obj_set_style_bg_color(br.segs[k], lv_color_hex(k < l ? on : th->faint), 0);
}

static void br_tap(lv_event_t *e)
{
    (void)e;
    lv_point_t pt;
    lv_indev_get_point(lv_indev_active(), &pt);
    int x = pt.x - SND_X - BR_SEG_X, seg = x < 0 ? 0 : x / (BR_SEG_W + BR_SEG_GAP);
    seg = seg >= BR_STEPS ? BR_STEPS - 1 : seg;
    shell_brightness((seg + 1) * 10);
    br_show();
    voice_say(VOICE_TICK);
}

static void br_open(lv_obj_t *scr)
{
    th_header(scr, shell_title("bright"), 40);
    lv_obj_t *r = th_box(scr, SND_X, (SCR - SND_H) / 2, SND_W, SND_H, th->radius ? 16 : 0, th->panel);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r, br_tap, LV_EVENT_CLICKED, NULL);
    lv_obj_align(th_text(r, th->small, th->text, "Яркость"), LV_ALIGN_TOP_LEFT, BR_SEG_X, 12);
    br.value = th_text(r, th->small, TH_TERMINAL ? th->text : th->accent, "");
    lv_obj_align(br.value, LV_ALIGN_TOP_RIGHT, -BR_SEG_X, 12);
    for (int k = 0; k < BR_STEPS; k++) {
        lv_obj_t *g = br.segs[k] = th_box(r, BR_SEG_X + k * (BR_SEG_W + BR_SEG_GAP), SND_H - 20 - 18, BR_SEG_W, 18, th->radius ? 9 : 0, th->faint);
        lv_obj_remove_flag(g, LV_OBJ_FLAG_CLICKABLE);
    }
    br_show();
}

/* ================= themes: a row each, a tap on one puts it on (the panel's "theme i" does the same) ================= */

static void style_apply(void *i) /* after the tap's event is over: the restyle closes this screen, then it opens again */
{
    shell_set_theme((int)(intptr_t)i);
    shell_open("style");
}

static void style_tap(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i != theme_index()) lv_async_call(style_apply, (void *)(intptr_t)i);
}


static void style_open(lv_obj_t *scr)
{
    th_header(scr, shell_title("style"), 40);
    for (int i = 0; i < TH_COUNT; i++) {
        bool on = i == theme_index();
        lv_obj_t *r = th_box(scr, 73, 128 + i * 76, 320, 60, th->radius ? 16 : 0, th->panel);
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(r, style_tap, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_align(th_text(r, th->small, th->text, themes[i].name), LV_ALIGN_LEFT_MID, 20, 0);
        lv_obj_t *mark = th_text(r, th->small, on ? (TH_TERMINAL ? th->text : th->accent) : th->dim,
                                 TH_TERMINAL ? (on ? "[X]" : "[ ]") : th_label(on ? "вкл" : ""));
        lv_obj_align(mark, LV_ALIGN_RIGHT_MID, -20, 0);
    }
}

/* ================= eye colours: a scrolling list of names, a tap on one puts it on (as the panel's "eyes i") ================= */

#define EYE_ROWS 4   /* rows seen at once */
#define EYE_PITCH 76 /* a row and the gap under it, as in Темы */
#define EYE_TOP 104  /* the window's top edge */
static struct {
    lv_obj_t *marks[PIP_EYES_COUNT], *rows[PIP_EYES_COUNT], *list;
    /* the wheel, in rows (the top one seen): where it is, dragged from, coasting from and to; its speed when let go */
    float pos, drag_from, anim_from, anim_to, vy;
    lv_point_t press, last;
    uint32_t last_tick;
    int tick_row;
    bool caught; /* the press stopped a coasting wheel: letting go only settles it */
} ey;

static void eyes_show(void)
{
    for (int i = 0; i < PIP_EYES_COUNT; i++) {
        bool on = i == pip_eyes_get();
        lv_label_set_text(ey.marks[i], TH_TERMINAL ? (on ? "[X]" : "[ ]") : th_label(on ? "вкл" : ""));
        lv_obj_set_style_text_color(ey.marks[i], lv_color_hex(on ? (TH_TERMINAL ? th->text : th->accent) : th->dim), 0);
    }
}


#define EYE_LAST ((float)(PIP_EYES_COUNT - EYE_ROWS)) /* the furthest the wheel goes: the last row at the bottom */

static void eyes_layout(void) /* the rows where the wheel puts them; only the window is redrawn */
{
    for (int i = 0; i < PIP_EYES_COUNT; i++) {
        float y = (i - ey.pos) * EYE_PITCH;
        bool out = y < -EYE_PITCH || y > EYE_ROWS * EYE_PITCH;
        lv_obj_set_flag(ey.rows[i], LV_OBJ_FLAG_HIDDEN, out);
        if (!out) lv_obj_set_y(ey.rows[i], (int32_t)lroundf(y));
    }
    int row = (int)lroundf(fmaxf(0, fminf(EYE_LAST, ey.pos)));
    if (row != ey.tick_row) ey.tick_row = row, voice_say(VOICE_TICK); /* a detent, as the menu's wheel */
}

static void eyes_snap_exec(void *var, int32_t v)
{
    (void)var;
    ey.pos = ey.anim_from + (ey.anim_to - ey.anim_from) * v / 1000.0f;
    eyes_layout();
}

static void eyes_snap(float target, uint32_t ms)
{
    ey.anim_from = ey.pos, ey.anim_to = target;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &ey);
    lv_anim_set_exec_cb(&a, eyes_snap_exec);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

/* a picker wheel like the menu's, anywhere on the screen: drag it, flick it and it coasts, slowing down to land on a
   whole row; past the first or the last it pulls back like a rubber band; a tap on a row puts those eyes on */
static void eyes_input(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    int dx = p.x - ey.press.x, dy = p.y - ey.press.y;
    if (code == LV_EVENT_PRESSED) {
        ey.press = p;
        ey.caught = lv_anim_delete(&ey, eyes_snap_exec); /* a touch catches a coasting wheel */
        ey.drag_from = ey.pos, ey.vy = 0;
    } else if (code == LV_EVENT_PRESSING) {
        uint32_t el = lv_tick_elaps(ey.last_tick);
        if (el > 0) ey.vy = 0.6f * ey.vy + 0.4f * (p.y - ey.last.y) / el; /* px per ms */
        if (abs(dy) > abs(dx)) {
            float pos = ey.drag_from - (float)dy / EYE_PITCH;
            if (pos < 0) pos = pos / (1 - pos * 2);
            if (pos > EYE_LAST) pos = EYE_LAST + (pos - EYE_LAST) / (1 + (pos - EYE_LAST) * 2);
            ey.pos = pos;
            eyes_layout();
        }
    } else if (code == LV_EVENT_RELEASED) {
        bool tap = abs(dx) < 12 && abs(dy) < 12;
        if (tap && ey.caught) eyes_snap(fmaxf(0, fminf(EYE_LAST, roundf(ey.pos))), 260); /* caught mid-coast: settle */
        else if (tap) {
            int row = (int)floorf(ey.pos + (float)(p.y - EYE_TOP) / EYE_PITCH);
            bool in = p.x >= 73 && p.x < 73 + 320 && p.y >= EYE_TOP && p.y < EYE_TOP + EYE_ROWS * EYE_PITCH - 16;
            float off = (ey.pos + (float)(p.y - EYE_TOP) / EYE_PITCH) - row; /* not in the gap under a row */
            if (in && row >= 0 && row < PIP_EYES_COUNT && off < 60.0f / EYE_PITCH) {
                pip_eyes(row); /* the face takes them at once; main.c keeps them */
                eyes_show();
                voice_say(VOICE_TICK);
            }
        } else {
            if (lv_tick_elaps(ey.last_tick) > 60) ey.vy = 0; /* held still before letting go: no throw */
            /* where a wheel losing ~0.3% of its speed per ms would stop, rounded to a row */
            float target = fmaxf(0, fminf(EYE_LAST, roundf(ey.pos - ey.vy * 330 / EYE_PITCH)));
            eyes_snap(target, (uint32_t)fminf(1200, 260 + 110 * fabsf(target - ey.pos)));
        }
    }
    ey.last = p, ey.last_tick = lv_tick_get();
}

static void eyes_open(lv_obj_t *scr)
{
    th_header(scr, shell_title("eyes"), 40);
    /* a window of four whole rows, made like Темы' rows, turned like the menu's wheel (eyes_input) */
    lv_obj_t *list = ey.list = lv_obj_create(scr);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 320, EYE_ROWS * EYE_PITCH - 16);
    lv_obj_set_pos(list, 73, EYE_TOP);
    lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, eyes_input, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, eyes_input, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, eyes_input, LV_EVENT_RELEASED, NULL);
    for (int i = 0; i < PIP_EYES_COUNT; i++) {
        lv_obj_t *r = ey.rows[i] = th_box(list, 0, 0, 320, 60, th->radius ? 16 : 0, th->panel);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE); /* the screen takes the touch */
        lv_obj_align(th_text(r, th->small, th->text, pip_eyes_name(i)), LV_ALIGN_LEFT_MID, 20, 0);
        ey.marks[i] = th_text(r, th->small, th->accent, "");
        lv_obj_align(ey.marks[i], LV_ALIGN_RIGHT_MID, -20, 0);
    }
    eyes_show();
    /* opens with the ones he wears in view, a row from the top when it can */
    float top = (float)(pip_eyes_get() - 1);
    ey.pos = fmaxf(0, fminf(EYE_LAST, top)), ey.vy = 0, ey.caught = false;
    ey.tick_row = (int)ey.pos; /* no click for opening */
    eyes_layout();
}

static void eyes_close(void) { lv_anim_delete(&ey, eyes_snap_exec); } /* a coasting wheel stops with the screen */

/* ================= worn out: in place of a game with him until he has rested ================= */

static void tired_open(lv_obj_t *scr)
{
    pip_info_t in;
    pip_get_info(&in);
    /* asleep, worn out, too grubby to feel like it (wash him: on his face tap the bottom key, then hold it), or still
       cross with you (pet_sulking: give him time, and some kindness) */
    const char *title = in.asleep ? "Пип спит" : in.energy < pet_tune.play_tired ? "Пип устал"
                      : in.clean < pet_tune.play_dirty ? "Пип грязный" : pet_sulking(pip_pet()) ? "Пип обиделся" : "Пип устал";
    lv_obj_t *t = th_text(scr, th->body, th->accent, title); /* the same in every theme: no header */
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -22);
    lv_obj_t *l = th_text(scr, th->small, th->dim, "Эта игра недоступна");
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 22);
}

const app_t app_tired = {.id = "tired", .name = "Пип устал", .open = tired_open};

/* a shake for the toys: the jolt over gravity, with a rest so one shake is one throw */
static bool jolt(float ax, float ay, float az, float dt, float *rest)
{
    float j = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1);
    if ((*rest -= dt) > 0 || j < 0.7f) return false;
    *rest = 1.0f;
    return true;
}

/* ================= endless noughts and crosses: three marks each, the fourth takes the oldest away ================= */
/* Against Pip (you are X and start, he is O) or with a friend, passing it over. The mark that goes next is shown faint.
   No draws: someone wins sooner or later. */

typedef struct { uint8_t b[9]; int8_t q[2][3]; uint8_t n[2]; } xo_t;

static struct {
    xo_t st;
    lv_obj_t *x[9][2], *o[9], *status, *chips[2];
    lv_timer_t *think;
    int turn, won, line, mode, wins[2][2]; /* mode 0: with Pip, 1: with a friend; wins per mode */
    bool tired; /* he does not want to play: with a friend only */
} xo;

static const uint8_t xo_lines[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};

static void xo_place(xo_t *s, int p, int c)
{
    if (s->n[p] == 3) { /* the oldest goes */
        s->b[s->q[p][0]] = 0;
        s->q[p][0] = s->q[p][1], s->q[p][1] = s->q[p][2], s->n[p] = 2;
    }
    s->b[c] = (uint8_t)(p + 1), s->q[p][s->n[p]++] = (int8_t)c;
}

static int xo_line(const xo_t *s, int p) /* the winning line, or -1 */
{
    for (int i = 0; i < 8; i++)
        if (s->b[xo_lines[i][0]] == p + 1 && s->b[xo_lines[i][1]] == p + 1 && s->b[xo_lines[i][2]] == p + 1) return i;
    return -1;
}

static int xo_search(const xo_t *s, int p, int depth) /* the best Pip (1) can force, looking depth moves ahead */
{
    int best = p ? -100 : 100;
    for (int c = 0; c < 9; c++) {
        if (s->b[c]) continue;
        xo_t t = *s;
        xo_place(&t, p, c);
        int v = xo_line(&t, p) >= 0 ? (p ? 50 + depth : -50 - depth) : depth ? xo_search(&t, !p, depth - 1) : 0;
        best = p ? (v > best ? v : best) : (v < best ? v : best);
    }
    return best;
}

/* how good a player he is follows his character (pet_level, the panel's "care"): Неприхотливый looks two moves ahead and
   slips a third of the time, Уравновешенный four and a fifth, Требовательный five and hardly ever */
static const struct { int depth, slip; } xo_skill[PET_LEVELS] = {{2, 35}, {4, 20}, {5, 5}};
#define XO_SKILL (&xo_skill[pet_level >= 0 && pet_level < PET_LEVELS ? pet_level : 1])

static int xo_pick(void)
{
    int moves[9], n = 0, best = -1000, pick = -1;
    for (int c = 0; c < 9; c++) if (!xo.st.b[c]) moves[n++] = c;
    for (int i = n - 1; i > 0; i--) { int j = (int)lv_rand(0, (uint32_t)i), t = moves[i]; moves[i] = moves[j], moves[j] = t; }
    for (int i = 0; i < n; i++) {
        xo_t t = xo.st;
        xo_place(&t, 1, moves[i]);
        if (xo_line(&t, 1) >= 0) return moves[i]; /* a win is never missed */
        int v = xo_search(&t, 0, XO_SKILL->depth);
        if (v > best) best = v, pick = moves[i];
    }
    if ((int)lv_rand(0, 99) < XO_SKILL->slip) pick = moves[lv_rand(0, (uint32_t)n - 1)]; /* he is a pet, not a computer: sometimes he slips */
    return pick;
}

static void xo_draw(void)
{
    for (int c = 0; c < 9; c++) {
        int who = xo.st.b[c];
        /* the oldest mark of whoever moves next fades: it is the one that goes */
        bool going = who && xo.won < 0 && xo.st.n[who - 1] == 3 && xo.st.q[who - 1][0] == c && who - 1 == xo.turn;
        bool lit = xo.won >= 0 && xo.line >= 0 && (xo_lines[xo.line][0] == c || xo_lines[xo.line][1] == c || xo_lines[xo.line][2] == c);
        lv_opa_t opa = going ? LV_OPA_30 : xo.won >= 0 && !lit ? LV_OPA_40 : LV_OPA_COVER;
        for (int k = 0; k < 2; k++) lv_obj_set_flag(xo.x[c][k], LV_OBJ_FLAG_HIDDEN, who != 1), lv_obj_set_style_opa(xo.x[c][k], opa, 0);
        lv_obj_set_flag(xo.o[c], LV_OBJ_FLAG_HIDDEN, who != 2), lv_obj_set_style_opa(xo.o[c], opa, 0);
    }
    static const char *const say[2][4] = {{"ты выиграл", "пип выиграл", "твой ход", "пип думает"},
                                          {"крестики выиграли", "нолики выиграли", "ход крестиков", "ход ноликов"}};
    char s[64];
    snprintf(s, sizeof s, "%s   %d:%d", say[xo.mode][xo.won >= 0 ? xo.won : 2 + xo.turn], xo.wins[xo.mode][0], xo.wins[xo.mode][1]);
    lv_label_set_text(xo.status, th_title(s));
    for (int i = 0; i < 2; i++) {
        bool on = i == xo.mode;
        lv_obj_set_style_opa(xo.chips[i], i == 0 && xo.tired ? LV_OPA_40 : LV_OPA_COVER, 0); /* closed: faint */
        lv_obj_set_style_bg_opa(xo.chips[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(xo.chips[i], on ? 0 : 2, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(xo.chips[i], 0), lv_color_hex(on ? th->bg : th->dim), 0);
    }
}

static void xo_end(int p)
{
    xo.won = p, xo.line = xo_line(&xo.st, p), xo.wins[xo.mode][p]++;
    if (xo.mode) { voice_say(VOICE_HAPPY); return; }
    pip_game(p ? PIP_GAME_WON : PIP_GAME_LOST); /* smug for a while, or pouting: either way it was playing with you */
    if (!pip_wants_play()) xo.tired = true; /* that was his last one for now: the next game is with a friend */
}

static void xo_pip(lv_timer_t *t)
{
    (void)t;
    xo.think = NULL;
    xo_place(&xo.st, 1, xo_pick());
    voice_say(VOICE_TICK);
    if (xo_line(&xo.st, 1) >= 0) xo_end(1);
    else xo.turn = 0;
    xo_draw();
}

static void xo_new(void)
{
    if (xo.tired) xo.mode = 1;
    memset(&xo.st, 0, sizeof xo.st);
    xo.turn = 0, xo.won = -1, xo.line = -1;
    xo_draw();
}

static void xo_tap(lv_event_t *e)
{
    (void)e;
    if (xo.won >= 0) { xo_new(); return; }
    if (xo.turn && !xo.mode) return; /* Pip's move */
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    int cx = (p.x - (SCR / 2 - 144)) / 96, cy = (p.y - (SCR / 2 - 144)) / 96;
    if (p.x < SCR / 2 - 144 || p.y < SCR / 2 - 144 || cx > 2 || cy > 2) return;
    int c = cy * 3 + cx;
    if (xo.st.b[c]) return;
    int me = xo.mode ? xo.turn : 0;
    xo_place(&xo.st, me, c);
    voice_say(VOICE_TICK);
    if (!xo.mode) pip_play_move(); /* playing with him: fun for him, and it tires him a little */
    if (xo_line(&xo.st, me) >= 0) xo_end(me);
    else if (xo.mode) xo.turn = !xo.turn; /* hand it over */
    else xo.turn = 1, xo.think = lv_timer_create(xo_pip, 550, NULL), lv_timer_set_repeat_count(xo.think, 1);
    xo_draw();
}

static void xo_close(void)
{
    if (xo.think) lv_timer_delete(xo.think), xo.think = NULL;
}

static void xo_mode(lv_event_t *e)
{
    int mode = (int)(intptr_t)lv_event_get_user_data(e);
    if (mode == 0 && xo.tired) { pip_action(PIP_ACT_PLAY); return; } /* he says no */
    xo_close();
    xo.mode = mode;
    xo_new();
}

static void xo_open(lv_obj_t *scr)
{
    int x0 = SCR / 2 - 144, y0 = SCR / 2 - 144;
    uint32_t o_ink = TH_TERMINAL ? th->dim : th->accent;
    for (int i = 1; i < 3; i++) { /* the grid */
        th_box(scr, x0 + i * 96 - 2, y0 + 10, 4, 288 - 20, 2, th->faint);
        th_box(scr, x0 + 10, y0 + i * 96 - 2, 288 - 20, 4, 2, th->faint);
    }
    static lv_point_precise_t diag[2][2] = {{{0, 0}, {48, 48}}, {{48, 0}, {0, 48}}};
    for (int c = 0; c < 9; c++) {
        int cx = x0 + c % 3 * 96 + 48, cy = y0 + c / 3 * 96 + 48;
        for (int k = 0; k < 2; k++) {
            lv_obj_t *l = xo.x[c][k] = lv_line_create(scr);
            lv_line_set_points(l, diag[k], 2);
            lv_obj_set_style_line_width(l, 8, 0);
            lv_obj_set_style_line_rounded(l, th->radius > 0, 0);
            lv_obj_set_style_line_color(l, lv_color_hex(th->text), 0);
            lv_obj_set_pos(l, cx - 24, cy - 24);
        }
        lv_obj_t *o = xo.o[c] = th_box(scr, cx - 28, cy - 28, 56, 56, !TH_TERMINAL ? LV_RADIUS_CIRCLE : 0, th->bg);
        lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(o, 8, 0);
        lv_obj_set_style_border_color(o, lv_color_hex(o_ink), 0);
    }
    xo.status = th_text_at(scr, th->small, th->dim, "", -1, 44);
    xo.tired = !pip_wants_play();
    if (xo.tired) xo.mode = 1; /* worn out: only a friend to play with */
    /* who you play with: Pip or a friend */
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 392);
    static const char *const names[2] = {"с пипом", "с другом"};
    for (int i = 0; i < 2; i++) {
        lv_obj_t *c = xo.chips[i] = lv_obj_create(row);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_hor(c, 12, 0);
        lv_obj_set_style_pad_ver(c, 6, 0);
        lv_obj_set_style_radius(c, th->radius, 0);
        lv_obj_set_style_bg_color(c, lv_color_hex(TH_TERMINAL ? th->text : th->accent), 0);
        lv_obj_set_style_border_color(c, lv_color_hex(th->faint), 0);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c, xo_mode, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        th_text(c, th->tiny_bold, th->dim, names[i]);
    }
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, xo_tap, LV_EVENT_CLICKED, NULL);
    xo_new();
}

/* ================= the toys: a coin, the 8 ball and a die, one look ================= */
/* Each is a token in the theme's colours: a disc (coin with its inner ring, 8 ball with its dark window) or a rounded
   square (die), with its rim, a soft light from above and a shadow, in the screen's own background. When it plays the
   token itself moves (toy_paint): the coin is tossed up turning over and over (edge-on it is a line) and lands with a
   bounce, the die tumbles forwards and sideways with a hop each turn and a new face each time it is edge-on, the 8
   ball is shaken side to side, the 8 sinks and the answer floats up. Then a word under it. No timer runs while it
   rests. The top button, like the bottom one, goes back. */

static int toy_y = 206; /* the token's centre: the coin and the die over their word, the 8 ball (no word) in the middle */
#define TOY_Y toy_y
#define TOY_R 128   /* the disc's radius */
#define TOY_SQ 112  /* the square's half side */
#define TOY_WIN 84  /* the 8 ball's window, where the 8 and the answer are */
enum { TOY_COIN, TOY_EIGHT, TOY_DIE };

/* The part of the screen a token can move in (up when tossed, the shadow under it): the screen's background holds the
   token at rest, and while it plays this region is put together again each frame from three pictures made once per
   theme and toy - the ground (with the shadow), the token's colours and how much of each pixel the token covers. A
   frame is row copies and a table look-up per pixel: shifted (tossed, shaken) and squashed (turning over, tumbling)
   with no maths per pixel and nothing for LVGL to transform or blend. */
#define TOY_RX0 (SCR / 2 - TOY_R - 44)
#define TOY_RY0 (TOY_Y - TOY_R - 64)
#define TOY_RW (2 * (TOY_R + 44))
#define TOY_RH (TOY_R + 64 + TOY_R + 44)
#define TOY_CX (SCR / 2 - TOY_RX0) /* the token's centre in the region */
#define TOY_CY (TOY_Y - TOY_RY0)

/* ten yes, five maybe, five no; no punctuation at all: a dash alone on a line, or a comma or colon at a line end, pulls the centred line askew */
static const char *const eight_says[] = {
    "Бесспорно", "Предрешено", "Никаких сомнений", "Определённо да", "Можешь быть уверен",
    "Похоже на то", "Вероятнее всего", "Хорошие перспективы", "Звёзды на твоей стороне", "Да",
    "Пока не ясно", "Спроси позже", "Лучше не рассказывать", "Сейчас нельзя сказать", "Сосредоточься и спроси",
    "Даже не думай", "Ответ отрицательный", "Звёзды против", "Перспективы не очень", "Весьма сомнительно",
};
/* pips on the 3x3 spots: bit (row * 3 + col) */
static const uint16_t die_pips[7] = {0, 0x010, 0x101, 0x111, 0x145, 0x155, 0x16D};
static const char *const die_words[7] = {"", "Один", "Два", "Три", "Четыре", "Пять", "Шесть"};

static struct {
    int kind, value, shown, from, turns, seg; /* from, turns: a coin's toss; seg: which tumble a die is in */
    int32_t e8_space;                         /* the 8 ball answer's letter spacing as the theme sets it */
    lv_obj_t *scr, *face, *coin_word, *eight, *says, *pips[9], *word;
    lv_timer_t *timer;
    uint32_t start;
    float rest;
} ty;

static lv_draw_buf_t *toy_bg;                 /* the screen's background: the ground, and the token at rest */
static uint16_t *toy_ground, *toy_tok;        /* the region: ground with shadow; the token's colours */
static uint8_t *toy_cover;                    /* how much of each region pixel the token covers, 0..255 */
static uint32_t toy_key = 0xFFFFFFFF;

static float toy_mix(uint32_t a, uint32_t b, int shift, float t) { return ((a >> shift) & 255) * (1 - t) + ((b >> shift) & 255) * t; }

static uint16_t toy_565(const float *c, int x, int y) /* 5 bits a channel, dithered: dark greys stay grey, light does not band */
{
    static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    uint32_t q[3];
    for (int i = 0; i < 3; i++) q[i] = (uint32_t)fminf(255, fmaxf(0, c[i]) + bayer[y & 3][x & 3] / 2.0f) >> 3;
    return (uint16_t)(q[0] << 11 | (q[1] << 1 | q[1] >> 4) << 5 | q[2]);
}

static void toy_paint(float ox, float oy, float kx, float ky);
void lv_image_cache_drop(const void *src); /* LVGL's (misc/cache/instance/lv_image_cache.h, not in lvgl.h): the edited picture is drawn afresh */

/* the pictures, once per theme and toy: the ground everywhere (the dot grid too), and in the region the shadow, the
   token's face (lighter on top, a 3 px rim; the coin's inner ring, the 8 ball's dark window) and its coverage */
static void toy_build(int kind)
{
    uint32_t key = th->bg ^ th->panel << 1 ^ th->faint << 2 ^ th->grid << 3 ^ (uint32_t)th->radius << 4 ^ (uint32_t)kind << 29;
    if (!toy_bg) {
        toy_bg = lv_draw_buf_create(SCR, SCR, LV_COLOR_FORMAT_RGB565, 0);
        toy_ground = lv_malloc(TOY_RW * TOY_RH * 2), toy_tok = lv_malloc(TOY_RW * TOY_RH * 2), toy_cover = lv_malloc(TOY_RW * TOY_RH);
        LV_ASSERT_MALLOC(toy_bg);
        LV_ASSERT_MALLOC(toy_ground);
        LV_ASSERT_MALLOC(toy_tok);
        LV_ASSERT_MALLOC(toy_cover);
    }
    if (toy_key == key) { toy_paint(0, 0, 1, 1); return; }
    toy_key = key;
    bool square = kind == TOY_DIE;
    uint32_t top = 0, lo = th->panel, rim = th->faint;
    for (int s = 0; s < 24; s += 8) top |= (uint32_t)fminf(255, ((th->panel >> s) & 255) * 1.35f + 16) << s;
    const float ext = square ? TOY_SQ : TOY_R, corner = square ? (th->radius ? 34 : 6) : 0;
    uint16_t *px = (uint16_t *)toy_bg->data;
    uint32_t stride = toy_bg->header.stride / 2;
    for (int y = 0; y < SCR; y++)
        for (int x = 0; x < SCR; x++) {
            bool dot = th->grid && (x % 11 == 7 || x % 11 == 8) && (y % 11 == 7 || y % 11 == 8);
            uint32_t base = dot ? th->grid : th->bg;
            float g[3] = {toy_mix(base, 0, 16, 0), toy_mix(base, 0, 8, 0), toy_mix(base, 0, 0, 0)};
            int rx = x - TOY_RX0, ry = y - TOY_RY0;
            if (rx < 0 || ry < 0 || rx >= TOY_RW || ry >= TOY_RH) { px[y * stride + x] = toy_565(g, x, y); continue; }
            float dx = x + 0.5f - SCR / 2.0f, dy = y + 0.5f - TOY_Y;
            float sx = dx / (ext * 0.95f), sy = (dy - ext - 6) / 22, sh = 1 - (sx * sx + sy * sy); /* the shadow under it */
            if (sh > 0) for (int i = 0; i < 3; i++) g[i] *= 1 - 0.55f * sh * sh;
            float d, rr = sqrtf(dx * dx + dy * dy);
            if (!square) d = rr - ext;
            else {
                float qx = fabsf(dx) - (ext - corner), qy = fabsf(dy) - (ext - corner);
                float ox = fmaxf(qx, 0), oy = fmaxf(qy, 0);
                d = sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - corner;
            }
            float tcol[3], t = fminf(1, fmaxf(0, (dy + ext) / (2 * ext))), r = fminf(1, fmaxf(0, d + 4));
            for (int i = 0; i < 3; i++) {
                int s = 16 - i * 8;
                float face = toy_mix(top, lo, s, t);
                if (kind == TOY_COIN) { /* an inner ring, as a coin has */
                    float ring = fminf(1, fmaxf(0, 1.8f - fabsf(rr - (ext - 26))));
                    face += (((rim >> s) & 255) - face) * ring * 0.9f;
                } else if (kind == TOY_EIGHT && rr < TOY_WIN + 3) { /* the window: dark, with a thin rim */
                    float in = fminf(1, fmaxf(0, TOY_WIN - rr + 0.5f)), wrim = fminf(1, fmaxf(0, 2.5f - fabsf(rr - TOY_WIN)));
                    float dark = toy_mix(th->bg, th->panel, s, 0.35f);
                    face = face + (dark - face) * in;
                    face += (((rim >> s) & 255) - face) * wrim;
                }
                tcol[i] = face + (((rim >> s) & 255) - face) * r;
            }
            int k = ry * TOY_RW + rx;
            toy_ground[k] = toy_565(g, x, y);
            toy_tok[k] = toy_565(tcol, x, y);
            toy_cover[k] = (uint8_t)(255 * fminf(1, fmaxf(0, 0.5f - d)));
        }
    toy_paint(0, 0, 1, 1);
}

static uint16_t mix565(uint16_t a, uint16_t b, uint32_t k) /* a over b, k 0..32 */
{
    uint32_t r = ((a >> 11) * k + (b >> 11) * (32 - k)) >> 5, g = (((a >> 5) & 63) * k + ((b >> 5) & 63) * (32 - k)) >> 5;
    uint32_t bl = ((a & 31) * k + (b & 31) * (32 - k)) >> 5;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

/* the token in the region, moved by (ox, oy) and squashed to (kx, ky) of its size about its centre */
static void toy_paint(float ox, float oy, float kx, float ky)
{
    uint16_t *dst = (uint16_t *)toy_bg->data;
    uint32_t stride = toy_bg->header.stride / 2;
    int32_t ikx = (int32_t)(65536 / fmaxf(kx, 0.03f)), iky = (int32_t)(65536 / fmaxf(ky, 0.03f)), iox = (int32_t)ox, ioy = (int32_t)oy;
    int x_lo = TOY_CX + iox - (int)((TOY_R + 2) * kx), x_hi = TOY_CX + iox + (int)((TOY_R + 2) * kx) + 1; /* where it can be */
    x_lo = x_lo < 0 ? 0 : x_lo, x_hi = x_hi > TOY_RW ? TOY_RW : x_hi;
    for (int y = 0; y < TOY_RH; y++) {
        uint16_t *d = dst + (TOY_RY0 + y) * stride + TOY_RX0;
        const uint16_t *g = toy_ground + y * TOY_RW;
        int32_t sy = TOY_CY + (int32_t)(((int64_t)(y - TOY_CY - ioy) * iky) >> 16);
        if (sy < 0 || sy >= TOY_RH || x_hi <= x_lo) { memcpy(d, g, TOY_RW * 2); continue; }
        const uint16_t *t = toy_tok + sy * TOY_RW;
        const uint8_t *a = toy_cover + sy * TOY_RW;
        memcpy(d, g, (size_t)x_lo * 2);
        for (int x = x_lo; x < x_hi; x++) {
            int32_t sx = TOY_CX + (int32_t)(((int64_t)(x - TOY_CX - iox) * ikx) >> 16);
            uint8_t c = sx >= 0 && sx < TOY_RW ? a[sx] : 0;
            d[x] = c == 255 ? t[sx] : c == 0 ? g[x] : mix565(t[sx], g[x], (c + 4) >> 3);
        }
        memcpy(d + x_hi, g + x_hi, (size_t)(TOY_RW - x_hi) * 2);
    }
    lv_image_cache_drop(toy_bg);
    if (ty.scr) {
        lv_area_t r = {TOY_RX0, TOY_RY0, TOY_RX0 + TOY_RW - 1, TOY_RY0 + TOY_RH - 1};
        lv_obj_invalidate_area(ty.scr, &r);
    }
}

/* an answer in the round window, laid out by hand (LVGL's wrap keeps the breaking space on a line, which pushes a
   centred line off centre, and may break a word): every way of putting the words on lines is tried; a word of one
   or two letters (не, и, по) stays with the word after it unless then nothing fits. A line must fit the circle's
   chord at its glyphs' top and bottom. The biggest font wins (the smallest one may close up its letter spacing for a
   long word), and in it the layout with the most room to spare, a little against more lines (so they come out
   balanced, not one word each). */
#define E8_WORDS 8
static int utf8_len(const char *s) { int n = 0; for (; *s; s++) n += (*s & 0xC0) != 0x80; return n; }

static void eight_fit(const char *text)
{
    const lv_font_t *fonts[3] = {th->body, th->small, th->tiny};
    int nf = 0;
    for (int i = 0; i < 3; i++)
        if (fonts[i] && (nf == 0 || fonts[i] != fonts[nf - 1])) fonts[nf++] = fonts[i];
    if (!nf) return; /* a theme with no text faces: nothing to fit */
    const char *t = th_label(text);
    int32_t gap = lv_obj_get_style_text_line_space(ty.says, 0);
    char words[E8_WORDS][64];
    unsigned glued = 0; /* bit i: word i is a short one that would rather not end a line */
    int nw = 0;
    for (const char *p = t; *p && nw < E8_WORDS;) {
        while (*p == ' ') p++;
        if (!*p) break;
        int n = 0;
        while (p[n] && p[n] != ' ') n++;
        char w[48];
        if (n >= (int)sizeof w) { p += n; continue; }
        memcpy(w, p, (size_t)n), w[n] = 0, p += n;
        if (utf8_len(w) <= 2) glued |= 1u << nw;
        snprintf(words[nw++], sizeof words[0], "%s", w);
    }
    const float R = TOY_WIN - 7; /* the window less a margin */
    const lv_font_t *best_f = fonts[nf - 1];
    int32_t best_sp = 0;
    unsigned best_cut = 0, worst_cut = 0;
    float best = 1e9f, worst = 1e9f; /* worst: the least bad layout in the smallest font, if none fits at all */
    for (int pass = 0; pass < 2 && best > 1e8f; pass++) /* a short word kept with the next, then if nothing fits, not */
    for (int fi = 0; fi < nf && best > 1e8f; fi++) {
        int32_t lh = lv_font_get_line_height(fonts[fi]);
        for (int32_t sp = ty.e8_space; sp >= (fi == nf - 1 ? 0 : ty.e8_space) && best > 1e8f; sp--) {
            for (unsigned cut = 0; cut < 1u << (nw > 1 ? nw - 1 : 0); cut++) { /* bit i: a new line after word i */
                if (pass == 0 && cut & glued) continue;
                int lines = 1 + __builtin_popcount(cut);
                float top = -(lines * lh + (lines - 1) * gap) / 2.0f, k = 0;
                char line[200];
                line[0] = 0;
                for (int i = 0, l = 0; i < nw; i++) {
                    if (line[0]) strcat(line, " ");
                    strcat(line, words[i]);
                    if (i < nw - 1 && !(cut >> i & 1)) continue;
                    float y0 = top + l * (lh + gap) + 2, y1 = y0 + lh - 4; /* the glyphs, less the font's padding */
                    float ye = fmaxf(fabsf(y0), fabsf(y1));
                    float chord = ye < R ? 2 * sqrtf(R * R - ye * ye) : 0;
                    lv_point_t sz;
                    lv_text_get_size(&sz, line, fonts[fi], sp, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
                    k = fmaxf(k, chord > 0 ? sz.x / chord : 1e6f);
                    line[0] = 0, l++;
                }
                float score = k + 0.06f * lines;
                if (k <= 1 && score < best) best = score, best_cut = cut, best_f = fonts[fi], best_sp = sp;
                if (k < worst) worst = k, worst_cut = cut;
            }
        }
    }
    if (best > 1e8f) best_cut = worst_cut; /* nothing fits: the smallest font, letters closed up, the least bad lines */
    /* the chosen layout, lines split by '\n' (the label centres each one exactly) */
    char out[240];
    out[0] = 0;
    for (int i = 0; i < nw; i++) {
        strcat(out, words[i]);
        if (i < nw - 1) strcat(out, best_cut >> i & 1 ? "\n" : " ");
    }
    lv_obj_set_style_text_font(ty.says, best_f, 0); /* the label as wide as its widest line, centred in the window */
    lv_obj_set_style_text_letter_space(ty.says, best_sp, 0);
    lv_label_set_text(ty.says, out);
}

static void toy_show(int v) /* what is on the token, at rest */
{
    ty.shown = v;
    if (ty.kind == TOY_COIN) {
        lv_label_set_text(ty.coin_word, th_label(v ? "Решка" : "Орёл"));
        lv_obj_remove_flag(ty.coin_word, LV_OBJ_FLAG_HIDDEN);
    } else if (ty.kind == TOY_DIE) {
        for (int i = 0; i < 9; i++) lv_obj_set_flag(ty.pips[i], LV_OBJ_FLAG_HIDDEN, !(die_pips[v] >> i & 1));
    } else {
        lv_obj_set_flag(ty.eight, LV_OBJ_FLAG_HIDDEN, v >= 0);
        lv_obj_set_flag(ty.says, LV_OBJ_FLAG_HIDDEN, v < 0);
        if (v >= 0) eight_fit(eight_says[v]);
    }
}

static int toy_pick(void)
{
    return ty.kind == TOY_COIN ? (int)lv_rand(0, 1) : ty.kind == TOY_DIE ? (int)lv_rand(1, 6)
                                                                         : (int)lv_rand(0, sizeof eight_says / sizeof eight_says[0] - 1);
}

static uint32_t toy_ms(void) { return ty.kind == TOY_COIN ? 1500 : ty.kind == TOY_DIE ? 1400 : 1800; }
static float ease_out(float t) { return 1 - (1 - t) * (1 - t) * (1 - t); }

static void die_spread(float kx, float ky) /* the pips on their spots, the face squashed to (kx, ky) as it tumbles */
{
    const int side = TOY_SQ * 2;
    bool edge = kx < 0.6f || ky < 0.6f; /* turned well away: no pips to see */
    for (int i = 0; i < 9; i++) {
        lv_obj_set_pos(ty.pips[i], side / 2 + (int)((i % 3 - 1) * 62 * kx) - 18, side / 2 + (int)((i / 3 - 1) * 62 * ky) - 18);
        lv_obj_set_style_opa(ty.pips[i], edge ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    }
}

static void toy_move(float ox, float oy, float kx, float ky) /* the token and what is on it, together */
{
    toy_paint(ox, oy, kx, ky);
    lv_obj_set_style_translate_x(ty.face, (int32_t)ox, 0);
    lv_obj_set_style_translate_y(ty.face, (int32_t)oy, 0);
}

static void toy_land(void)
{
    lv_timer_delete(ty.timer), ty.timer = NULL;
    toy_move(0, 0, 1, 1);
    if (ty.kind == TOY_DIE) die_spread(1, 1);
    if (ty.kind == TOY_EIGHT) {
        lv_obj_set_style_translate_y(ty.says, 0, 0), lv_obj_set_style_text_opa(ty.says, LV_OPA_COVER, 0);
        lv_obj_set_style_translate_y(ty.eight, 0, 0), lv_obj_set_style_text_opa(ty.eight, LV_OPA_COVER, 0);
    }
    toy_show(ty.value);
    if (ty.kind == TOY_EIGHT) return; /* its answer is in the window (it clicked as it came up), no word under it */
    lv_label_set_text(ty.word, th_label(ty.kind == TOY_COIN ? (ty.value ? "Решка" : "Орёл") : die_words[ty.value]));
    voice_say(VOICE_TICK);
}

/* Every 20 ms while it plays, and only then. */
static void toy_step(lv_timer_t *t)
{
    (void)t;
    uint32_t el = lv_tick_elaps(ty.start), total = toy_ms();
    if (el >= total) { toy_land(); return; }
    float k = el / (float)total;
    if (ty.kind == TOY_COIN) {
        /* tossed up, turning over and over (seen edge-on it is a line), down again with a little bounce */
        float a = ty.turns * 3.14159f * ease_out(k), c = fabsf(cosf(a));
        int face = ty.from ^ ((int)((a + 1.5708f) / 3.14159f) & 1);
        if (face != ty.shown) toy_show(face);
        lv_obj_set_flag(ty.coin_word, LV_OBJ_FLAG_HIDDEN, c < 0.6f); /* words cannot flatten: gone near the edge */
        float up = k < 0.82f ? -44 * sinf(3.14159f * k / 0.82f) : -9 * sinf(3.14159f * (k - 0.82f) / 0.18f);
        toy_move(0, up, 1, fmaxf(0.04f, c));
    } else if (ty.kind == TOY_DIE) {
        /* it tumbles: over forwards, then sideways, and so on, slower each time, a hop with every turn; a new face
           comes up each time it is edge-on, the last one the face it was dealt */
        const int flips = 5;
        float ph = flips * 3.14159f * ease_out(k);
        int seg = (int)((ph + 1.5708f) / 3.14159f);
        if (seg != ty.seg) {
            ty.seg = seg;
            int v;
            do v = toy_pick(); while (v == ty.shown);
            toy_show(seg >= flips ? ty.value : v);
        }
        float c = fmaxf(0.04f, fabsf(cosf(ph))), hop = -fabsf(sinf(ph)) * 44 * (1 - k);
        float kx = seg % 2 ? c : 1, ky = seg % 2 ? 1 : c;
        toy_move(0, hop, kx, ky);
        die_spread(kx, ky);
    } else {
        /* shaken: it rocks side to side and the 8 sinks into the dark, then the answer floats up into the window */
        const float sink = 0.55f;
        if (k < sink) {
            float q = k / sink;
            toy_move(sinf(el * 0.045f) * 26 * (1 - q), fabsf(cosf(el * 0.045f)) * -8 * (1 - q), 1, 1);
            lv_obj_set_style_translate_y(ty.eight, (int32_t)(30 * q * q), 0);
            lv_obj_set_style_text_opa(ty.eight, (lv_opa_t)(255 * (1 - q * q)), 0);
        } else {
            if (ty.shown < 0) toy_show(ty.value), toy_move(0, 0, 1, 1), voice_say(VOICE_TICK); /* the answer comes up: a click */
            float q = ease_out((k - sink) / (1 - sink));
            lv_obj_set_style_translate_y(ty.says, (int32_t)(28 * (1 - q)), 0);
            lv_obj_set_style_text_opa(ty.says, (lv_opa_t)(255 * q), 0);
        }
    }
}

static void toy_roll(void)
{
    if (ty.timer) return;
    ty.value = toy_pick(), ty.start = lv_tick_get(), ty.seg = 0;
    lv_label_set_text(ty.word, "");
    if (ty.kind == TOY_COIN) ty.from = ty.shown, ty.turns = 6 + (ty.from != ty.value); /* an odd count turns it over */
    if (ty.kind == TOY_EIGHT) {
        toy_show(-1); /* the 8 while it thinks: silent (a toy, not Pip talking; its answer clicks) */
        lv_obj_set_style_text_opa(ty.says, LV_OPA_TRANSP, 0);
    }
    ty.timer = lv_timer_create(toy_step, 20, NULL);
}

static void toy_tap(lv_event_t *e) { (void)e; toy_roll(); }
static void toy_imu(float ax, float ay, float az, float dt) { if (jolt(ax, ay, az, dt, &ty.rest)) toy_roll(); }
static void toy_close(void)
{
    if (ty.timer) lv_timer_delete(ty.timer), ty.timer = NULL;
    ty.scr = NULL;
}

static void toy_open(lv_obj_t *scr, int kind, const char *head)
{
    ty.kind = kind, ty.rest = 0.5f, ty.timer = NULL, ty.scr = NULL;
    toy_y = kind == TOY_EIGHT ? SCR / 2 : 206; /* fixed per kind, so toy_build's cache (keyed by kind) holds */
    toy_build(kind); /* the token at rest in the background */
    lv_obj_set_style_bg_image_src(scr, toy_bg, 0);
    ty.scr = scr;
    th_header(scr, head, 40);
    const int side = kind == TOY_DIE ? TOY_SQ * 2 : TOY_R * 2;
    lv_obj_t *f = ty.face = lv_obj_create(scr); /* all that is on the token, so it moves as one */
    lv_obj_remove_style_all(f);
    lv_obj_set_size(f, side, side);
    lv_obj_set_pos(f, SCR / 2 - side / 2, TOY_Y - side / 2);
    lv_obj_remove_flag(f, LV_OBJ_FLAG_CLICKABLE);
    if (kind == TOY_COIN) { /* the side's name inside the coin's ring */
        const lv_font_t *big = th->title && lv_font_get_line_height(th->title) <= 48 ? th->title : th->body;
        ty.coin_word = th_text(f, big, th->text, "");
        lv_obj_set_style_text_letter_space(ty.coin_word, th->caps ? 2 : 0, 0);
        lv_obj_center(ty.coin_word);
        ty.value = (int)lv_rand(0, 1);
    } else if (kind == TOY_DIE) {
        for (int i = 0; i < 9; i++)
            ty.pips[i] = th_box(f, side / 2 + (i % 3 - 1) * 62 - 18, side / 2 + (i / 3 - 1) * 62 - 18, 36, 36,
                                th->radius ? LV_RADIUS_CIRCLE : 0, th->text);
        ty.value = (int)lv_rand(1, 6);
    } else {
        ty.eight = th_text(f, th->digits, th->text, "8");
        lv_obj_center(ty.eight);
        ty.says = th_text(f, th->body, th->text, "");
        lv_label_set_long_mode(ty.says, LV_LABEL_LONG_CLIP); /* never wrapped by LVGL: eight_fit breaks the lines */
        ty.e8_space = lv_obj_get_style_text_letter_space(ty.says, 0);
        lv_obj_set_style_text_align(ty.says, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(ty.says, 2, 0);
        lv_obj_center(ty.says);
        ty.value = -1;
    }
    toy_show(ty.value);
    ty.word = th_text(scr, th->body, th->accent == th->bg ? th->text : th->accent, "");
    lv_obj_align(ty.word, LV_ALIGN_TOP_MID, 0, 352);
    lv_obj_align(th_text(scr, TH_TERMINAL ? th->tiny : th->small, th->dim, "встряхни или коснись"), LV_ALIGN_TOP_MID, 0, 398);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, toy_tap, LV_EVENT_CLICKED, NULL);
}

static void e8_open(lv_obj_t *scr) { toy_open(scr, TOY_EIGHT, "шар предсказаний"); }
static void cn_open(lv_obj_t *scr) { toy_open(scr, TOY_COIN, "пип-01 // монетка"); }
static void dc_open(lv_obj_t *scr) { toy_open(scr, TOY_DIE, "пип-01 // кубик"); }

/* ================= ping-pong with Pip: yours at the bottom (drag anywhere, it follows sideways), his at the top ================= */
/* First to 5. He tracks the ball well but aims with an error that grows when he is ahead and shrinks when he is behind,
   and his paddle has a top speed: a fast ball beats him. Every point is a little fun for him (like a move at noughts
   and crosses), the end of a game makes him smug or sulky. Small objects moving: cheap on the board. */

#define PG_L 86          /* the side walls */
#define PG_R (SCR - 86)
#define PG_TOP 104       /* where the ball meets his paddle */
#define PG_BOT 352       /* and yours */
#define PG_PW 112        /* paddle width: about a fingertip and a half on the board */
#define PG_PH 16
#define PG_BALL 24
#define PG_WIN 5
/* his skill by his character (pet_level): how far off he aims as a rule (px, before the lead changes it) and his paddle's
   top speed (px/s) - the more care he needs, the harder he is to beat */
static const struct { float spread, speed; } pg_skill[PET_LEVELS] = {{130, 250}, {100, 300}, {70, 360}};
#define PG_SKILL (&pg_skill[pet_level >= 0 && pet_level < PET_LEVELS ? pet_level : 1])
enum { PG_READY, PG_PLAY, PG_POINT, PG_OVER };

static struct {
    lv_obj_t *me, *him, *eyes[2], *ball, *score[2], *msg;
    th_cell_t net[16];
    lv_timer_t *timer;
    float bx, by, vx, vy, mx, hx, err, wait;
    int pts[2], state, serve_to; /* pts[0] you, pts[1] Pip; serve_to: 0 toward you, 1 toward him */
    int32_t grab;
    uint32_t last;
} pg;

static float pg_clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void pg_place(void)
{
    lv_obj_set_pos(pg.me, (int32_t)(pg.mx - PG_PW / 2), PG_BOT);
    lv_obj_set_pos(pg.him, (int32_t)(pg.hx - PG_PW / 2), PG_TOP - PG_PH);
    for (int i = 0; i < 2; i++) { /* his eyes over his paddle, looking where the ball is */
        float look = pg_clamp((pg.bx - pg.hx) / 40, -3, 3);
        lv_obj_set_pos(pg.eyes[i], (int32_t)(pg.hx - 20 + i * 28 + look), PG_TOP - PG_PH - 30);
    }
    lv_obj_set_pos(pg.ball, (int32_t)(pg.bx - PG_BALL / 2), (int32_t)(pg.by - PG_BALL / 2));
}

static void pg_score(void)
{
    for (int i = 0; i < 2; i++) lv_label_set_text_fmt(pg.score[i], "%d", pg.pts[i]);
}

static void pg_new_error(void) /* how far off he will aim at the next ball: sloppier when ahead, sharper when behind */
{
    float base = PG_SKILL->spread, lead = pg_clamp((float)(pg.pts[1] - pg.pts[0]), -3, 3);
    float spread = pg_clamp(base + 16 * lead, base * 0.56f, base * 1.5f); /* he reaches 68 px either side */
    pg.err = (lv_rand(0, 2000) / 1000.0f - 1) * spread;
}

static void pg_serve(void)
{
    pg.bx = SCR / 2, pg.by = SCR / 2, pg.state = PG_PLAY;
    float sp = 230;
    pg.vx = (lv_rand(0, 2000) / 1000.0f - 1) * 120, pg.vy = pg.serve_to ? -sp : sp;
    pg_new_error();
    lv_obj_add_flag(pg.msg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(pg.ball, LV_OBJ_FLAG_HIDDEN);
}

static void pg_point(int who) /* who: 0 you scored, 1 Pip did */
{
    pg.pts[who]++;
    pg_score();
    pip_play_move(); /* a little fun for him, a little energy spent */
    voice_say(VOICE_TICK);
    lv_obj_add_flag(pg.ball, LV_OBJ_FLAG_HIDDEN);
    if (pg.pts[who] >= PG_WIN) {
        pg.state = PG_OVER;
        lv_label_set_text(pg.msg, th_label(who ? "Пип выиграл" : "Ты выиграл"));
        lv_obj_remove_flag(pg.msg, LV_OBJ_FLAG_HIDDEN);
        pip_game(who ? PIP_GAME_WON : PIP_GAME_LOST);
        return;
    }
    pg.state = PG_POINT, pg.wait = 0.9f, pg.serve_to = who ? 0 : 1; /* from the middle, toward whoever lost the point */
}

static void pg_tick(lv_timer_t *t)
{
    (void)t;
    float dt = fminf(lv_tick_elaps(pg.last) / 1000.0f, 0.04f);
    pg.last = lv_tick_get();
    /* his paddle: after the ball when it comes his way, back to the middle when it goes away */
    float target = SCR / 2;
    if (pg.state == PG_PLAY && pg.vy < 0) {
        float x = pg.bx + pg.vx * (pg.by - PG_TOP) / -pg.vy, w = PG_R - PG_L - PG_BALL; /* where it will arrive, walls folded */
        float u = fmodf(x - PG_L - PG_BALL / 2, 2 * w);
        if (u < 0) u += 2 * w;
        target = PG_L + PG_BALL / 2 + (u > w ? 2 * w - u : u) + pg.err;
    }
    float step = PG_SKILL->speed * dt;
    pg.hx += pg_clamp(target - pg.hx, -step, step);
    pg.hx = pg_clamp(pg.hx, PG_L + PG_PW / 2, PG_R - PG_PW / 2);
    if (pg.state == PG_POINT && (pg.wait -= dt) <= 0) pg_serve();
    if (pg.state == PG_PLAY) {
        pg.bx += pg.vx * dt, pg.by += pg.vy * dt;
        const float r = PG_BALL / 2;
        if (pg.bx < PG_L + r) pg.bx = PG_L + r, pg.vx = fabsf(pg.vx);
        if (pg.bx > PG_R - r) pg.bx = PG_R - r, pg.vx = -fabsf(pg.vx);
        if (pg.vy > 0 && pg.by + r >= PG_BOT && pg.by + r < PG_BOT + PG_PH + 6 && fabsf(pg.bx - pg.mx) <= PG_PW / 2 + r) {
            /* yours: where it hits sends it sideways, and it gets a little quicker */
            float sp = fminf(540, fabsf(pg.vy) * 1.06f);
            pg.vy = -sp, pg.vx = pg_clamp(pg.vx * 0.5f + (pg.bx - pg.mx) / (PG_PW / 2) * 220, -0.9f * sp, 0.9f * sp);
            pg.by = PG_BOT - r;
            pg_new_error();
            voice_say(VOICE_TICK);
        } else if (pg.vy < 0 && pg.by - r <= PG_TOP && pg.by - r > PG_TOP - PG_PH - 6 && fabsf(pg.bx - pg.hx) <= PG_PW / 2 + r) {
            float sp = fminf(540, fabsf(pg.vy) * 1.04f);
            pg.vy = sp, pg.vx = pg_clamp(pg.vx * 0.6f + (pg.bx - pg.hx) / (PG_PW / 2) * 160 + (lv_rand(0, 200) - 100.0f), -0.9f * sp, 0.9f * sp);
            pg.by = PG_TOP + r;
            voice_say(VOICE_TICK);
        } else if (pg.by > PG_BOT + 50) pg_point(1);
        else if (pg.by < PG_TOP - 50) pg_point(0);
    }
    pg_place();
}

static void pg_restart(void)
{
    pg.pts[0] = pg.pts[1] = 0, pg.serve_to = 1, pg.state = PG_READY;
    pg.bx = pg.by = SCR / 2, pg.mx = pg.hx = SCR / 2;
    pg_score();
    lv_label_set_text(pg.msg, th_label("Коснись — подача"));
    lv_obj_remove_flag(pg.msg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(pg.ball, LV_OBJ_FLAG_HIDDEN);
}

static void pg_input(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (code == LV_EVENT_PRESSED) pg.grab = (int32_t)pg.mx - p.x; /* the paddle keeps its distance from the finger */
    else if (code == LV_EVENT_PRESSING) pg.mx = pg_clamp((float)(p.x + pg.grab), PG_L + PG_PW / 2, PG_R - PG_PW / 2);
    else if (code == LV_EVENT_SHORT_CLICKED) {
        if (pg.state == PG_READY) pg_serve();
        else if (pg.state == PG_OVER) {
            if (!pip_wants_play()) shell_open("pong"); /* worn out now: the "Пип устал" card */
            else pg_restart();
        }
    }
}

static void pg_close(void) { if (pg.timer) lv_timer_delete(pg.timer), pg.timer = NULL; }

static void pg_open(lv_obj_t *scr)
{
    memset(&pg, 0, sizeof pg);
    int round = th->radius ? LV_RADIUS_CIRCLE : 0;
    for (int i = 0; i < 2; i++) th_box(scr, i ? PG_R : PG_L - 2, PG_TOP - 24, 2, PG_BOT - PG_TOP + 48 + PG_PH, 0, th->faint); /* the walls */
    for (int i = 0; i < 16; i++) pg.net[i] = (th_cell_t){(int16_t)(PG_L + 6 + i * 18), SCR / 2 - 1, 10, 3, th->faint};
    th_cells(scr, pg.net, 16);
    for (int i = 0; i < 2; i++) { /* his score above the net, yours under it */
        pg.score[i] = th_text(scr, th->body, th->dim, "0");
        lv_obj_align(pg.score[i], LV_ALIGN_TOP_LEFT, PG_R + 14, i ? SCR / 2 - 40 : SCR / 2 + 8); /* outside the wall */
    }
    pg.him = th_box(scr, 0, 0, PG_PW, PG_PH, round, th->accent);
    for (int i = 0; i < 2; i++) pg.eyes[i] = th_box(scr, 0, 0, 14, 20, round, th->text);
    pg.me = th_box(scr, 0, 0, PG_PW, PG_PH, round, th->text);
    pg.ball = th_box(scr, 0, 0, PG_BALL, PG_BALL, round, th->text);
    pg.msg = th_text(scr, th->small, th->text, "");
    lv_obj_set_style_bg_color(pg.msg, lv_color_hex(th->panel), 0);
    lv_obj_set_style_bg_opa(pg.msg, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(pg.msg, 16, 0), lv_obj_set_style_pad_ver(pg.msg, 8, 0);
    lv_obj_set_style_radius(pg.msg, th->radius ? 12 : 0, 0);
    lv_obj_align(pg.msg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, pg_input, LV_EVENT_ALL, NULL);
    pg_restart();
    pg_place();
    pg.last = lv_tick_get();
    pg.timer = lv_timer_create(pg_tick, 16, NULL);
}

/* ================= snake: steer it to the food by a swipe, or a tap on the side it should go to ================= */
/* A 15x15 grid in the square inside the round screen, drawn into one canvas (RGB565 in PSRAM). A step repaints only the
   cells that changed - the new head, the old head, the tail let go, the food - and invalidates just those, so a step
   costs the same few hundred pixels at any length (no object per segment). Turns are queued (two), so quick ones are
   not lost; a turn straight back is ignored. A little faster with every bite. The top key pauses and goes on, and
   after a crash (or a full board) a tap or the top key starts again; the bottom key, as everywhere, goes back. */
#define SN_N 15
#define SN_C 22                    /* px a cell */
#define SN_W (SN_N * SN_C)         /* 330: the square inside the circle */
#define SN_X ((SCR - SN_W) / 2)
#define SN_CELLS (SN_N * SN_N)
#define SN_START_MS 170            /* a step at first... */
#define SN_FASTER_MS 4             /* ...this much quicker each bite... */
#define SN_MIN_MS 85               /* ...down to this */
#define SN_SWIPE 26                /* px of a finger's travel that make a turn (it turns again for every SN_SWIPE more) */
enum { SN_READY, SN_PLAY, SN_PAUSE, SN_OVER };
enum { SN_EMPTY, SN_BODY, SN_HEAD, SN_FOOD };

static int sn_best; /* this power-on's best score */
static struct {
    lv_obj_t *cv, *score, *msg;
    lv_draw_buf_t *buf;
    lv_timer_t *timer;
    uint8_t *ring;                 /* the body's cells, a ring: ring[head] is the head, the len before it the rest */
    bool *occ;                     /* both in one block made at open: 2 KB, so PSRAM on the board, not internal RAM */
    int head, len, dir, q[2], qn, food, state, ms;
    lv_point_t from;               /* where the finger's last turn was counted from */
    bool swiped;
    uint16_t ink[4];               /* RGB565: the ground, the dots, the snake, the food */
} sn;

static uint16_t sn_565(uint32_t c) { return (uint16_t)((c >> 8 & 0xF800) | (c >> 5 & 0x07E0) | (c >> 3 & 0x001F)); }

static void sn_paint(int cell, int what)
{
    if (!sn.buf) return;
    int x0 = cell % SN_N * SN_C, y0 = cell / SN_N * SN_C, stride = sn.buf->header.stride / 2;
    uint16_t *px = (uint16_t *)sn.buf->data;
    bool round = th->radius;
    for (int y = 0; y < SN_C; y++) {
        uint16_t *row = px + (y0 + y) * stride + x0;
        for (int x = 0; x < SN_C; x++) {
            int cx = 2 * x - (SN_C - 1), cy = 2 * y - (SN_C - 1); /* twice the offset from the cell's middle */
            uint16_t c = sn.ink[0];
            if (what == SN_EMPTY) {
                if (abs(cx) <= 1 && abs(cy) <= 1) c = sn.ink[1]; /* a faint dot in each empty cell */
            } else if (what == SN_FOOD) {
                int m = abs(cx) > abs(cy) ? abs(cx) : abs(cy);
                if (round ? cx * cx + cy * cy <= 15 * 15 : th->accent == th->text ? m <= 15 && m >= 10 : m <= 13) c = sn.ink[3];
                /* (one ink only, Ретро: a hollow square, so it is not taken for a piece of the snake) */
            } else {
                int ax = abs(cx), ay = abs(cy); /* a square 2 px in from the cell's edges, its corners rounded off */
                bool in = ax <= 17 && ay <= 17 && (!round || (ax - 9) * (ax - 9) * (ax > 9) + (ay - 9) * (ay - 9) * (ay > 9) <= 8 * 8);
                if (in) c = sn.ink[2];
                if (in && what == SN_HEAD) { /* two eyes, looking the way it goes */
                    int fx = (sn.dir == 1) - (sn.dir == 3), fy = (sn.dir == 2) - (sn.dir == 0);
                    for (int s = -1; s <= 1; s += 2) { /* a pair across the way it goes, a little ahead */
                        int ex = fx * 4 + (fx == 0) * s * 6, ey = fy * 4 + (fy == 0) * s * 6;
                        if (abs(cx - 2 * ex) <= 4 && abs(cy - 2 * ey) <= 4) c = sn.ink[0];
                    }
                }
            }
            row[x] = c;
        }
    }
    lv_area_t a;
    lv_obj_get_coords(sn.cv, &a);
    a.x1 += x0, a.y1 += y0, a.x2 = a.x1 + SN_C - 1, a.y2 = a.y1 + SN_C - 1;
    lv_obj_invalidate_area(sn.cv, &a);
}

static void sn_msg(const char *text)
{
    if (!text) { lv_obj_add_flag(sn.msg, LV_OBJ_FLAG_HIDDEN); return; }
    lv_label_set_text(sn.msg, text);
    lv_obj_remove_flag(sn.msg, LV_OBJ_FLAG_HIDDEN);
}

static void sn_food(void)
{
    int free_n = SN_CELLS - sn.len;
    if (free_n <= 0) { sn.food = -1; return; }
    int k = (int)lv_rand(0, (uint32_t)free_n - 1); /* the k-th free cell: every free one as likely */
    for (int i = 0; i < SN_CELLS; i++)
        if (!sn.occ[i] && k-- == 0) { sn.food = i; break; }
    sn_paint(sn.food, SN_FOOD);
}

static void sn_score(void) { lv_label_set_text_fmt(sn.score, "%d", sn.len - 3); }

static void sn_new(void)
{
    memset(sn.occ, 0, SN_CELLS * sizeof *sn.occ);
    sn.len = 3, sn.head = 2, sn.dir = 1, sn.qn = 0, sn.ms = SN_START_MS;
    for (int i = 0; i < 3; i++) sn.ring[i] = (uint8_t)(SN_N / 2 * SN_N + 3 + i), sn.occ[sn.ring[i]] = true;
    for (int i = 0; i < SN_CELLS; i++) sn_paint(i, sn.occ[i] ? (i == sn.ring[sn.head] ? SN_HEAD : SN_BODY) : SN_EMPTY);
    sn_food();
    sn_score();
    if (sn.timer) lv_timer_set_period(sn.timer, (uint32_t)sn.ms);
    sn.state = SN_READY;
    sn_msg(th_label("Свайп или касание"));
}

static void sn_over(bool won)
{
    sn.state = SN_OVER;
    int score = sn.len - 3;
    if (score > sn_best) sn_best = score;
    char t[96];
    snprintf(t, sizeof t, "%s\nРекорд %d", won ? "Победа" : "Игра окончена", sn_best);
    sn_msg(th_label(t));
}

static void sn_step(lv_timer_t *t)
{
    (void)t;
    if (sn.state != SN_PLAY) return;
    if (sn.qn) sn.dir = sn.q[0], sn.q[0] = sn.q[1], sn.qn--;
    int h = sn.ring[sn.head], x = h % SN_N + (sn.dir == 1) - (sn.dir == 3), y = h / SN_N + (sn.dir == 2) - (sn.dir == 0);
    if (x < 0 || x >= SN_N || y < 0 || y >= SN_N) { sn_over(false); return; }
    int nh = y * SN_N + x, tail = sn.ring[(sn.head - sn.len + 1 + SN_CELLS) % SN_CELLS];
    bool eat = nh == sn.food;
    if (!eat) sn.occ[tail] = false; /* the tail moves on first: following it into its cell is fine */
    if (sn.occ[nh]) { sn.occ[tail] = true; sn_over(false); return; }
    if (!eat) sn_paint(tail, SN_EMPTY);
    else sn.len++;
    sn_paint(h, SN_BODY);
    sn.head = (sn.head + 1) % SN_CELLS, sn.ring[sn.head] = (uint8_t)nh, sn.occ[nh] = true;
    sn_paint(nh, SN_HEAD);
    if (!eat) return;
    voice_say(VOICE_TICK);
    sn_score();
    if (sn.ms > SN_MIN_MS) sn.ms -= SN_FASTER_MS, lv_timer_set_period(sn.timer, (uint32_t)sn.ms);
    if (sn.len >= SN_CELLS) { sn_over(true); return; }
    sn_food();
}

static void sn_turn(int d) /* 0 up, 1 right, 2 down, 3 left */
{
    int last = sn.qn ? sn.q[sn.qn - 1] : sn.dir;
    if (d == last || d == (last + 2) % 4 || sn.qn >= 2) return;
    sn.q[sn.qn++] = d;
}

static int sn_way(int dx, int dy) { return abs(dx) > abs(dy) ? (dx > 0 ? 1 : 3) : (dy > 0 ? 2 : 0); }

static void sn_go(void) /* from ready, paused or over */
{
    if (sn.state == SN_OVER) sn_new();
    sn.state = SN_PLAY;
    sn_msg(NULL);
}

static void sn_input(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (!sn.cv) return; /* closed, the screen sliding away */
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (code == LV_EVENT_PRESSED) sn.from = p, sn.swiped = false;
    else if (code == LV_EVENT_PRESSING) { /* a swipe turns as soon as it is one, and again as the finger goes on */
        int dx = p.x - sn.from.x, dy = p.y - sn.from.y;
        if (abs(dx) < SN_SWIPE && abs(dy) < SN_SWIPE) return;
        if (sn.state == SN_OVER) return; /* the board stays to be looked at until a tap */
        if (sn.state != SN_PLAY) sn_go();
        sn_turn(sn_way(dx, dy));
        sn.from = p, sn.swiped = true;
    } else if (code == LV_EVENT_RELEASED && !sn.swiped) {
        if (sn.state != SN_PLAY) { sn_go(); return; }
        int dx = p.x - SCR / 2, dy = p.y - SCR / 2; /* a tap: toward that side of the screen */
        if (abs(dx) < 30 && abs(dy) < 30) return;
        sn_turn(sn_way(dx, dy));
    }
}

static bool sn_button(int id)
{
    if (id != PIP_BTN_PWR) return false;
    if (sn.state == SN_PLAY) sn.state = SN_PAUSE, sn_msg(th_label("Пауза"));
    else sn_go();
    return true;
}

static void sn_close(void) /* the buffer goes with the canvas (sn_gone): the screen still slides away after this */
{
    if (sn.timer) lv_timer_delete(sn.timer), sn.timer = NULL;
    free(sn.ring);
    sn.ring = NULL, sn.occ = NULL, sn.buf = NULL, sn.cv = NULL;
}

static void sn_gone(lv_event_t *e) { lv_draw_buf_destroy(lv_event_get_user_data(e)); }

static void sn_open(lv_obj_t *scr)
{
    memset(&sn, 0, sizeof sn);
    sn.ink[0] = sn_565(th->bg), sn.ink[1] = sn_565(th->faint), sn.ink[2] = sn_565(th->text), sn.ink[3] = sn_565(th->accent);
    if (!(sn.ring = calloc(1, 2048))) return; /* 225 + 225 bytes, asked as 2 KB: PSRAM on the board */
    sn.occ = (bool *)(sn.ring + SN_CELLS);
    sn.buf = lv_draw_buf_create(SN_W, SN_W, LV_COLOR_FORMAT_RGB565, 0);
    if (!sn.buf) { free(sn.ring), sn.ring = NULL, sn.occ = NULL; return; }
    sn.cv = lv_canvas_create(scr);
    lv_canvas_set_draw_buf(sn.cv, sn.buf);
    lv_obj_add_event_cb(sn.cv, sn_gone, LV_EVENT_DELETE, sn.buf);
    lv_obj_set_pos(sn.cv, SN_X, SN_X);
    for (int i = 0; i < 4; i++) /* the walls, just outside the field */
        th_box(scr, i == 1 ? SN_X + SN_W : SN_X - 2, i == 2 ? SN_X + SN_W : SN_X - 2, i & 1 ? 2 : SN_W + 4, i & 1 ? SN_W + 4 : 2, 0, th->dim);
    sn.score = th_text(scr, th->body, th->dim, "0");
    lv_obj_align(sn.score, LV_ALIGN_TOP_MID, 0, SN_X - 44);
    sn.msg = th_text(scr, th->small, th->text, "");
    lv_obj_set_style_text_align(sn.msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(sn.msg, lv_color_hex(th->panel), 0);
    lv_obj_set_style_bg_opa(sn.msg, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(sn.msg, 16, 0), lv_obj_set_style_pad_ver(sn.msg, 8, 0);
    lv_obj_set_style_radius(sn.msg, th->radius ? 12 : 0, 0);
    lv_obj_align(sn.msg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, sn_input, LV_EVENT_ALL, NULL);
    sn.timer = lv_timer_create(sn_step, SN_START_MS, NULL);
    sn_new();
}

/* the sim's status: the snake's length and state, or "-" */
const char *apps_snake(void)
{
    static char b[16];
    if (!sn.cv) return "-";
    snprintf(b, sizeof b, "%d,%d", sn.len, sn.state);
    return b;
}

/* ================= "Повтори за мной": Pip shows a run of directions, you repeat it ================= */
/* His two eyes in the middle look each way in turn, the pad on that side lights and he sings its note (C E G C); then
   you repeat it by a swipe or a tap on that side. Right: he is glad (a squint) and adds one more. Wrong, or nothing for
   RP_WAIT s: over, he is smug. Getting past his goal (rp_goal by his character) is beating him (pip_game LOST, once a
   game), failing before it his win; every round passed is a little fun (pip_play_move). He shows it quicker as it
   grows, and quicker still the more care he needs. A game with him: worn out, "Пип устал" opens instead (.with_pip).
   Ten small objects, moved or recoloured a few times a second: nothing per frame. */
#define RP_MAX 64
#define RP_WAIT 6000 /* ms with no answer: over */
enum { RP_READY, RP_SHOW, RP_INPUT, RP_GOOD, RP_OVER };
static const int rp_goal[PET_LEVELS] = {6, 8, 10};
static const float rp_pace[PET_LEVELS] = {1.15f, 1, 0.85f};
static int rp_best;
static struct {
    lv_obj_t *eye[2], *pupil[2], *pad[4], *msg, *top;
    lv_timer_t *timer;
    uint8_t seq[RP_MAX];
    int len, at, state, lit; /* at: the step shown or expected next; lit: the pad lit now (-1 none) */
    uint32_t t0, lit_until;
    bool beaten, swiped, lit_on;
    lv_point_t from;
} rp;

static int rp_level(void) { return pet_level >= 0 && pet_level < PET_LEVELS ? pet_level : 1; }
static uint32_t rp_on_ms(void) { int ms = 560 - 22 * rp.len; return (uint32_t)((ms < 260 ? 260 : ms) * rp_pace[rp_level()]); }

static void rp_eyes(int dir, int h) /* look a way (-1 ahead), the eyes this tall (84 open, less: a squint or a smug lid) */
{
    int dx = dir == 1 ? 14 : dir == 3 ? -14 : 0, dy = dir == 2 ? 14 : dir == 0 ? -14 : 0;
    for (int i = 0; i < 2; i++) {
        int x = SCR / 2 + (i ? 46 : -46);
        lv_obj_set_size(rp.eye[i], 64, h);
        lv_obj_set_pos(rp.eye[i], x - 32, SCR / 2 - h / 2);
        lv_obj_set_pos(rp.pupil[i], x - 15 + dx, SCR / 2 - 15 + dy * h / 84);
        if (h < 40) lv_obj_add_flag(rp.pupil[i], LV_OBJ_FLAG_HIDDEN); /* a squint: no pupils */
        else lv_obj_remove_flag(rp.pupil[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void rp_light(int dir, uint32_t ms) /* a pad lit, its note, his eyes that way */
{
    if (rp.lit >= 0) lv_obj_set_style_bg_color(rp.pad[rp.lit], lv_color_hex(th->faint), 0);
    rp.lit = dir;
    if (dir < 0) { rp_eyes(-1, 84); return; }
    lv_obj_set_style_bg_color(rp.pad[dir], lv_color_hex(th->accent), 0);
    rp.lit_until = lv_tick_get() + ms;
    voice_say(VOICE_NOTE0 + dir);
    rp_eyes(dir, 84);
}

static void rp_text(const char *top, const char *msg)
{
    if (top) lv_label_set_text(rp.top, th_label(top));
    if (!msg) lv_obj_add_flag(rp.msg, LV_OBJ_FLAG_HIDDEN);
    else lv_label_set_text(rp.msg, th_label(msg)), lv_obj_remove_flag(rp.msg, LV_OBJ_FLAG_HIDDEN);
}

static void rp_round(void) /* one more step, then he shows the lot */
{
    if (rp.len < RP_MAX) rp.seq[rp.len++] = (uint8_t)lv_rand(0, 3);
    char t[32];
    snprintf(t, sizeof t, "Раунд %d", rp.len);
    rp_text(t, "Смотри");
    rp.state = RP_SHOW, rp.at = 0, rp.t0 = lv_tick_get() + 500, rp.lit_on = false;
}

static void rp_new(void)
{
    rp.len = 0, rp.beaten = false;
    rp_light(-1, 0);
    rp_round();
}

static void rp_over(const char *why)
{
    rp.state = RP_OVER;
    rp_light(-1, 0);
    rp_eyes(1, 46); /* smug: lids half down, looking away */
    int score = rp.len - 1;
    if (score > rp_best) rp_best = score;
    if (!rp.beaten) pip_game(PIP_GAME_WON); /* stopped short of his goal: his win */
    char t[64];
    snprintf(t, sizeof t, "%s\nРекорд %d", why, rp_best);
    rp_text(NULL, t);
}

static void rp_answer(int dir)
{
    if (rp.state != RP_INPUT) return;
    rp_light(dir, 260);
    if (dir != rp.seq[rp.at]) { rp_over("Ошибка"); return; }
    rp.t0 = lv_tick_get();
    if (++rp.at < rp.len) return;
    rp.state = RP_GOOD, rp.t0 = lv_tick_get() + 300; /* the whole run: he is glad, then one more */
    pip_play_move();
    if (rp.len >= rp_goal[rp_level()] && !rp.beaten) rp.beaten = true, pip_game(PIP_GAME_LOST);
}

static void rp_tick(lv_timer_t *t)
{
    (void)t;
    uint32_t now = lv_tick_get();
    if (rp.lit >= 0 && (int32_t)(now - rp.lit_until) >= 0) { /* a pad goes out */
        lv_obj_set_style_bg_color(rp.pad[rp.lit], lv_color_hex(th->faint), 0);
        rp.lit = -1;
        if (rp.state != RP_GOOD && rp.state != RP_OVER) rp_eyes(-1, 84);
    }
    if ((int32_t)(now - rp.t0) < 0) return;
    if (rp.state == RP_SHOW) {
        uint32_t on = rp_on_ms();
        if (rp.at >= rp.len) { rp.state = RP_INPUT, rp.at = 0, rp.t0 = now; rp_text(NULL, "Повтори"); return; }
        rp_light(rp.seq[rp.at++], on);
        rp.t0 = now + on + on * 45 / 100; /* lit, then a gap */
    } else if (rp.state == RP_INPUT && now - rp.t0 > RP_WAIT) rp_over("Не успел");
    else if (rp.state == RP_GOOD) {
        if (rp.lit_on) { rp.lit_on = false; rp_eyes(-1, 84); rp_round(); return; }
        rp.lit_on = true, rp.t0 = now + 650;
        rp_eyes(-1, 30); /* a glad squint */
        rp_text(NULL, "Верно");
    }
}

static void rp_go(void) /* from ready or over */
{
    if (rp.state == RP_OVER && !pip_wants_play()) { shell_open("repeat"); return; } /* worn out now: "Пип устал" */
    rp_new();
}

static void rp_input(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (!rp.timer) return; /* closed, the screen sliding away */
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (code == LV_EVENT_PRESSED) rp.from = p, rp.swiped = false;
    else if (code == LV_EVENT_PRESSING && !rp.swiped) { /* one answer a touch: the swipe's way, as soon as it is one */
        int dx = p.x - rp.from.x, dy = p.y - rp.from.y;
        if (abs(dx) < SN_SWIPE && abs(dy) < SN_SWIPE) return;
        rp.swiped = true;
        rp_answer(sn_way(dx, dy));
    } else if (code == LV_EVENT_RELEASED && !rp.swiped) {
        if (rp.state == RP_READY || rp.state == RP_OVER) { rp_go(); return; }
        int dx = p.x - SCR / 2, dy = p.y - SCR / 2; /* a tap: that side's pad */
        if (abs(dx) < 40 && abs(dy) < 40) return;
        rp_answer(sn_way(dx, dy));
    }
}

static bool rp_button(int id) /* top: a new game once it is over */
{
    if (id != PIP_BTN_PWR || (rp.state != RP_OVER && rp.state != RP_READY)) return false;
    rp_go();
    return true;
}

static void rp_close(void)
{
    if (rp.timer) lv_timer_delete(rp.timer), rp.timer = NULL;
}

static void rp_open(lv_obj_t *scr)
{
    memset(&rp, 0, sizeof rp);
    rp.lit = -1;
    int round = TH_TERMINAL ? 0 : LV_RADIUS_CIRCLE; /* his eyes are round but in Ретро (its cells) */
    static const int16_t pads[4][4] = {{SCR / 2 - 45, 58, 90, 10}, {SCR - 68, SCR / 2 - 45, 10, 90},
                                       {SCR / 2 - 45, SCR - 68, 90, 10}, {58, SCR / 2 - 45, 10, 90}};
    for (int i = 0; i < 4; i++) rp.pad[i] = th_box(scr, pads[i][0], pads[i][1], pads[i][2], pads[i][3], round, th->faint);
    for (int i = 0; i < 2; i++) rp.eye[i] = th_box(scr, 0, 0, 64, 84, round ? 32 : 0, th->text);
    for (int i = 0; i < 2; i++) rp.pupil[i] = th_box(scr, 0, 0, 30, 30, round, th->bg);
    rp.top = th_text(scr, th->small, th->dim, "");
    lv_obj_align(rp.top, LV_ALIGN_TOP_MID, 0, 100);
    rp.msg = th_text(scr, th->small, th->text, "");
    lv_obj_set_style_text_align(rp.msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(rp.msg, LV_ALIGN_TOP_MID, 0, SCR / 2 + 64);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, rp_input, LV_EVENT_ALL, NULL);
    rp_eyes(-1, 84);
    rp.state = RP_READY;
    rp_text("Повтори за мной", "Коснись — начать");
    rp.timer = lv_timer_create(rp_tick, 20, NULL);
}

const char *apps_repeat(void) /* the sim's status: "len,state" while it is open, else "-" */
{
    static char b[16];
    if (!rp.timer) return "-";
    snprintf(b, sizeof b, "%d,%d", rp.len, rp.state);
    return b;
}

/* ================= Pip's flight: a tap lifts his eyes, gravity pulls them down; through the gaps in the columns =====
   An original take on the tap-to-fly genre, drawn in the theme's own inks: what flies is Pip's pair of eyes - looking
   ahead, up as they climb and down as they fall, screwed up for a moment at each flap, squeezed shut on a crash. Cheap
   to draw: plain rectangles (three columns, each a top and a bottom part with a ledge at the gap and a lit strip; two eye
   whites and two pupils; three trail dots), only moved each frame - nothing recomputed per pixel, no transforms, no
   opacity layers; LVGL repaints the strips they leave and enter. The physics runs
   on the real time between frames (clamped), so the game plays the same at any frame rate. A tap (on its press, for
   no lag) or the top key lifts it with a "fwip" (VOICE_FLAP); a column passed is a point and an achievement's
   "ding-ding!" (VOICE_POINT); a crash stops it, and after a moment a tap or
   the top key starts again. The best of this power-on is kept. Not a game with Pip, but time given to him all the same
   (the games group: shell.c counts it as attention). */
#define FL_N 3                   /* columns, recycled from the left edge to the right */
#define FL_W 46                  /* a column's width */
#define FL_CAP 8                 /* the ledge at the gap: this much wider each side, this tall */
#define FL_GAP0 150              /* the gap at first... */
#define FL_GAP_MIN 112           /* ...narrowing by 2 a point to this */
#define FL_EVERY 190             /* px between columns */
#define FL_SPEED0 135.0f         /* px/s at first... */
#define FL_SPEED_MAX 190.0f      /* ...3 faster a point up to this */
#define FL_X 150                 /* the eyes' middle across the screen */
#define FL_EW 18                 /* an eye: this wide... */
#define FL_EH 28                 /* ...this tall (open) */
#define FL_EGAP 9                /* between the two */
#define FL_HALF_W ((2 * FL_EW + FL_EGAP) / 2) /* half the pair's width: 22 */
#define FL_R 12                  /* what of the pair can touch a column: a box this much each way of the middle up and down
                                    (the eyes' 14 less a little grace), FL_HALF_W - 2 across */
#define FL_SQUINT_S 0.12f        /* a flap screws them up this long */
#define FL_G 1150.0f             /* gravity, px/s^2 */
#define FL_LIFT (-345.0f)        /* a tap's upward speed, px/s */
#define FL_FALL_MAX 540.0f
#define FL_TOP 16                /* above this or below FL_BOTTOM it is out of the sky (the round screen's rim) */
#define FL_BOTTOM (SCR - 16)
#define FL_FRAME_MS 15
#define FL_AGAIN_MS 650          /* after a crash, taps are ignored this long (no restart by the tap that crashed) */
#define FL_TRAIL 3               /* dots fading behind it: solid inks mixed toward the ground (no opacity: that costs) */
#define FL_SHINE 7               /* a lighter strip down each column's left side */
#define FL_POINT_MS 210          /* a point's "ding-ding" lasts this long: no flap sound over it */
enum { FL_READY, FL_PLAY, FL_OVER };

static int fl_best; /* this power-on's best */
static struct {
    /* the columns as objects of their own: tried as one object drawing them all and redrawing only the bands their edges
       swept (fewer pixels), it was slower on the board - every refreshed area drew every rectangle, and thin full-height
       bands cost nearly what wide ones do (Барби's render 19 -> 30 ms) */
    lv_obj_t *top[FL_N], *bot[FL_N], *cap_t[FL_N], *cap_b[FL_N], *lit_t[FL_N], *lit_b[FL_N], *eye[2], *pupil[2], *score, *msg;
    int eye_h;                     /* the eyes' height now (open, screwed up, shut): resized only when it changes */
    float squint;                  /* s left of a flap's squint */
    lv_obj_t *trail[FL_TRAIL];
    float trail_y[FL_TRAIL];
    lv_timer_t *timer;
    float x[FL_N], gap_y[FL_N], gap[FL_N], y, vy, speed, phase;
    bool counted[FL_N];
    int state, points;
    uint32_t last, over_at, point_at;
} fl;

static float fl_rand(float a, float b) { return a + (b - a) * (float)lv_rand(0, 10000) / 10000.0f; }
static void fl_msg(const char *text)
{
    if (!text) { lv_obj_add_flag(fl.msg, LV_OBJ_FLAG_HIDDEN); return; }
    lv_label_set_text(fl.msg, text);
    lv_obj_remove_flag(fl.msg, LV_OBJ_FLAG_HIDDEN);
}
static void fl_place(int i) /* a column where its numbers say */
{
    int x = (int)lroundf(fl.x[i]), g = (int)lroundf(fl.gap[i]), cy = (int)lroundf(fl.gap_y[i]);
    int top_h = cy - g / 2, bot_y = cy + g / 2;
    lv_obj_set_pos(fl.top[i], x, top_h - 400), lv_obj_set_pos(fl.bot[i], x, bot_y);
    lv_obj_set_pos(fl.cap_t[i], x - FL_CAP / 2, top_h - FL_CAP), lv_obj_set_pos(fl.cap_b[i], x - FL_CAP / 2, bot_y);
    if (fl.lit_t[i]) lv_obj_set_pos(fl.lit_t[i], x + 5, top_h - FL_CAP - 400), lv_obj_set_pos(fl.lit_b[i], x + 5, bot_y + FL_CAP);
}
static void fl_new_column(int i, float x)
{
    fl.x[i] = x, fl.counted[i] = false;
    fl.gap[i] = fmaxf(FL_GAP_MIN, FL_GAP0 - 2.0f * fl.points);
    fl.gap_y[i] = fl_rand(118 + fl.gap[i] / 2 - 40, SCR - 118 - fl.gap[i] / 2 + 40); /* inside the round screen's sky */
    fl_place(i);
}
static void fl_orb(void)
{
    /* the trail: where they were a few frames back, each dot further behind and smaller */
    for (int k = FL_TRAIL - 1; k > 0; k--) fl.trail_y[k] = fl.trail_y[k] + (fl.trail_y[k - 1] - fl.trail_y[k]) * 0.5f;
    fl.trail_y[0] = fl.trail_y[0] + (fl.y - fl.trail_y[0]) * 0.5f;
    for (int k = 0; k < FL_TRAIL; k++) {
        int r = 6 - 2 * k;
        lv_obj_set_pos(fl.trail[k], FL_X - FL_HALF_W - 8 - 10 * k - r, (int)lroundf(fl.trail_y[k]) - r);
    }
    /* open, screwed up a moment after a flap, or shut (a line) once crashed */
    int h = fl.state == FL_OVER ? 4 : fl.squint > 0 ? FL_EH * 2 / 3 : FL_EH;
    if (h != fl.eye_h) {
        fl.eye_h = h;
        for (int e = 0; e < 2; e++) lv_obj_set_height(fl.eye[e], h);
        for (int e = 0; e < 2; e++) lv_obj_set_flag(fl.pupil[e], LV_OBJ_FLAG_HIDDEN, fl.state == FL_OVER);
    }
    /* looking ahead, and up or down with the flight */
    int y = (int)lroundf(fl.y), look = fl.state == FL_PLAY ? (int)fmaxf(-7, fminf(7, fl.vy / 60)) : 0;
    for (int e = 0; e < 2; e++) {
        int ex = FL_X - FL_HALF_W + e * (FL_EW + FL_EGAP);
        lv_obj_set_pos(fl.eye[e], ex, y - h / 2);
        lv_obj_set_pos(fl.pupil[e], ex + FL_EW / 2 - 5 + 2, y - 5 + (h < FL_EH ? look / 2 : look)); /* a little ahead */
    }
}
static void fl_score(void) { lv_label_set_text_fmt(fl.score, "%d", fl.points); }
static void fl_new(void) /* ready: the light hovers, the columns wait off to the right */
{
    fl.state = FL_READY, fl.points = 0, fl.y = SCR / 2, fl.vy = 0, fl.speed = FL_SPEED0, fl.phase = 0;
    for (int k = 0; k < FL_TRAIL; k++) fl.trail_y[k] = fl.y;
    for (int i = 0; i < FL_N; i++) fl_new_column(i, SCR - 20 + i * FL_EVERY);
    fl_orb(), fl_score();
    fl_msg(th_label("Коснись, чтобы взлететь"));
}
static bool fl_touch(float x0, float y0, float x1, float y1) /* the pair of eyes (a box, a little grace) against a rectangle */
{
    return x1 > FL_X - (FL_HALF_W - 2) && x0 < FL_X + (FL_HALF_W - 2) && y1 > fl.y - FL_R && y0 < fl.y + FL_R;
}
static bool fl_hits(int i) /* a column's four parts: the body and the ledge above the gap, the ledge and the body below */
{
    float x = fl.x[i], top = fl.gap_y[i] - fl.gap[i] / 2, bot = fl.gap_y[i] + fl.gap[i] / 2;
    return fl_touch(x, -1000, x + FL_W, top - FL_CAP) || fl_touch(x - FL_CAP / 2, top - FL_CAP, x + FL_W + FL_CAP / 2, top) ||
           fl_touch(x - FL_CAP / 2, bot, x + FL_W + FL_CAP / 2, bot + FL_CAP) || fl_touch(x, bot + FL_CAP, x + FL_W, 2000);
}
static void fl_crash(void)
{
    fl.state = FL_OVER, fl.over_at = lv_tick_get();
    if (fl.points > fl_best) fl_best = fl.points;
    voice_say(VOICE_TOCK);
    char t[64];
    snprintf(t, sizeof t, "Счёт %d\nРекорд %d", fl.points, fl_best);
    fl_msg(th_label(t));
}
static void fl_lift(void)
{
    if (fl.state == FL_OVER) {
        if (lv_tick_elaps(fl.over_at) < FL_AGAIN_MS) return;
        fl_new();
        return;
    }
    if (fl.state == FL_READY) fl.state = FL_PLAY, fl_msg(NULL), fl.last = lv_tick_get();
    fl.vy = FL_LIFT, fl.squint = FL_SQUINT_S;
    /* "fwip" - but not over a point's "ding-ding" still ringing (one sound at a time: the newer would cut it off) */
    if (lv_tick_elaps(fl.point_at) >= FL_POINT_MS) voice_say(VOICE_FLAP);
}
/* the debug link's autopilot (`flauto 1`, for measuring the frame rate the same way in every theme): it flaps whenever it
   sinks under the middle of the next gap, and starts again after a crash */
static bool fl_auto;
void apps_flight_auto(bool on) { fl_auto = on; }
static void fl_pilot(void)
{
    if (fl.state != FL_PLAY) {
        if (fl.state == FL_READY || lv_tick_elaps(fl.over_at) >= FL_AGAIN_MS) fl_lift();
        return;
    }
    int next = -1;
    for (int i = 0; i < FL_N; i++)
        if (fl.x[i] + FL_W + FL_CAP >= FL_X - FL_HALF_W && (next < 0 || fl.x[i] < fl.x[next])) next = i;
    if (next >= 0 && fl.y > fl.gap_y[next] + 12 && fl.vy > 0) fl_lift();
}
static void fl_step(lv_timer_t *t)
{
    (void)t;
    if (!fl.eye[0]) return;
    if (fl_auto) fl_pilot();
    float dt = fminf(0.05f, lv_tick_elaps(fl.last) / 1000.0f);
    fl.last = lv_tick_get();
    if (fl.state == FL_READY) { /* hovering, waiting for the first tap */
        fl.phase += dt * 3;
        fl.y = SCR / 2 + sinf(fl.phase) * 8;
        fl_orb();
        return;
    }
    if (fl.state != FL_PLAY) return;
    fl.vy = fminf(FL_FALL_MAX, fl.vy + FL_G * dt);
    fl.y += fl.vy * dt;
    float far = 0;
    for (int i = 0; i < FL_N; i++) far = fmaxf(far, fl.x[i]);
    for (int i = 0; i < FL_N; i++) {
        fl.x[i] -= fl.speed * dt;
        if (fl.x[i] < -FL_W - FL_CAP) { fl_new_column(i, far + FL_EVERY); far = fl.x[i]; continue; }
        if (!fl.counted[i] && fl.x[i] + FL_W < FL_X - FL_HALF_W) { /* passed: a point, a little faster and narrower from now */
            fl.counted[i] = true, fl.points++;
            fl.speed = fminf(FL_SPEED_MAX, FL_SPEED0 + 3.0f * fl.points);
            fl_score();
            voice_say(VOICE_POINT), fl.point_at = lv_tick_get(); /* an achievement's "ding-ding!" */
        }
        fl_place(i);
    }
    bool hit = fl.y < FL_TOP || fl.y > FL_BOTTOM;
    if (fl.squint > 0) fl.squint -= dt;
    for (int i = 0; i < FL_N && !hit; i++) hit = fl_hits(i);
    if (hit) fl_crash();
    fl_orb();
}
static void fl_input(lv_event_t *e)
{
    if (!fl.eye[0]) return; /* closed, the screen sliding away */
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) fl_lift(); /* on the press: no waiting for the finger to lift */
}
static bool fl_button(int id)
{
    if (id != PIP_BTN_PWR) return false;
    fl_lift();
    return true;
}
static void fl_close(void)
{
    if (fl.timer) lv_timer_delete(fl.timer), fl.timer = NULL;
    fl.eye[0] = NULL; /* the objects go with the screen */
}
static uint32_t fl_mix(uint32_t a, uint32_t b, int k) /* a toward b, k/256 of the way */
{
    uint32_t c = 0;
    for (int s = 0; s < 24; s += 8) c |= (uint32_t)((int)(a >> s & 0xFF) + ((int)(b >> s & 0xFF) - (int)(a >> s & 0xFF)) * k / 256) << s;
    return c;
}
static void fl_open(lv_obj_t *scr)
{
    memset(&fl, 0, sizeof fl);
    uint32_t col = th->faint, edge = th->dim;
    bool mono = th->accent == th->text; /* Ретро: one ink - the columns outlined, not filled */
    for (int i = 0; i < FL_N; i++) {
        lv_obj_t **parts[4] = {&fl.top[i], &fl.bot[i], &fl.cap_t[i], &fl.cap_b[i]};
        for (int k = 0; k < 4; k++) {
            bool cap = k >= 2;
            lv_obj_t *o = *parts[k] = th_box(scr, -200, 0, cap ? FL_W + FL_CAP : FL_W, cap ? FL_CAP : 400, cap && th->radius ? 3 : 0, mono ? th->bg : col);
            lv_obj_set_style_border_width(o, mono || cap ? 2 : 0, 0);
            lv_obj_set_style_border_color(o, lv_color_hex(mono ? th->text : edge), 0);
        }
    }
    if (!mono) /* the columns' lit side */
        for (int i = 0; i < FL_N; i++) {
            uint32_t lit = fl_mix(col, th->text, 40);
            fl.lit_t[i] = th_box(scr, -200, 0, FL_SHINE, 400, 0, lit), fl.lit_b[i] = th_box(scr, -200, 0, FL_SHINE, 400, 0, lit);
        }
    for (int k = 0; k < FL_TRAIL; k++) {
        int r = 6 - 2 * k;
        fl.trail[k] = th_box(scr, -100, 0, 2 * r, 2 * r, mono ? 0 : LV_RADIUS_CIRCLE, fl_mix(th->accent, th->bg, 110 + 50 * k));
    }
    /* Pip's eyes: white (the terminal's one ink there), dark pupils, rounded but in Ретро */
    for (int e = 0; e < 2; e++) {
        fl.eye[e] = th_box(scr, 0, 0, FL_EW, FL_EH, mono ? 0 : 8, mono ? th->text : 0xFFFFFF);
        fl.pupil[e] = th_box(scr, 0, 0, 10, 10, mono ? 0 : LV_RADIUS_CIRCLE, mono ? th->bg : 0x101010);
    }
    fl.eye_h = FL_EH;
    fl.score = th_text(scr, th->body, th->dim, "0");
    lv_obj_align(fl.score, LV_ALIGN_TOP_MID, 0, 34);
    fl.msg = th_text(scr, th->small, th->text, "");
    lv_obj_set_style_text_align(fl.msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(fl.msg, lv_color_hex(th->panel), 0);
    lv_obj_set_style_bg_opa(fl.msg, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(fl.msg, 16, 0), lv_obj_set_style_pad_ver(fl.msg, 8, 0);
    lv_obj_set_style_radius(fl.msg, th->radius ? 12 : 0, 0);
    lv_obj_align(fl.msg, LV_ALIGN_CENTER, 0, 70);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, fl_input, LV_EVENT_PRESSED, NULL);
    fl.last = lv_tick_get();
    fl.timer = lv_timer_create(fl_step, FL_FRAME_MS, NULL);
    fl_new();
}
/* the sim's status: the points and the state (0 ready 1 flying 2 over), or "-" */
const char *apps_flight(void)
{
    static char b[16];
    if (!fl.eye[0]) return "-";
    snprintf(b, sizeof b, "%d,%d", fl.points, fl.state);
    return b;
}

/* ================= the menu, in order; then the groups ================= */

const app_t apps[] = {
    {.id = "care", .name = "Пип", .open = care_open, .close = care_close},
    {.id = "games", .name = "Игры"}, /* a group's owner: opening it turns the list into the group (shell.c) */
    {.id = "tools", .name = "Утилиты"},
    {.id = "phone", .name = "Телефон"},
    {.id = "settings", .name = "Настройки"},
    /* the groups: not in the menu, listed by their owner row above (each group in one run, in this order) */
    {.id = "clock", .name = "Часы", .open = clock_open, .close = clock_close, .group = "tools"},
    {.id = "timer", .name = "Таймер", .open = timer_open, .close = timer_close, .group = "tools"},
    {.id = "stopwatch", .name = "Секундомер", .open = sw_open, .close = sw_close, .button = sw_button, .group = "tools"},
    {.id = "noise", .name = "Шумомер", .open = nz_open, .close = nz_close, .group = "tools"},
    {.id = "torch", .name = "Фонарик", .open = torch_open, .group = "tools"},
    {.id = "xo", .name = "Крестики-нолики", .open = xo_open, .close = xo_close, .group = "games"},
    {.id = "pong", .name = "Пинг-понг", .open = pg_open, .close = pg_close, .group = "games", .with_pip = true},
    {.id = "snake", .name = "Змейка", .open = sn_open, .close = sn_close, .button = sn_button, .group = "games"},
    {.id = "flight", .name = "Flappy Pip", .open = fl_open, .close = fl_close, .button = fl_button, .group = "games"},
    {.id = "repeat", .name = "Повтори за мной", .open = rp_open, .close = rp_close, .button = rp_button, .group = "games", .with_pip = true},
    {.id = "ball", .name = "Шарик", .open = game_open, .close = game_close, .imu = game_imu, .button = game_button, .group = "games"},
    {.id = "eight", .name = "Шар предсказаний", .open = e8_open, .close = toy_close, .imu = toy_imu, .group = "games"},
    {.id = "coin", .name = "Монетка", .open = cn_open, .close = toy_close, .imu = toy_imu, .group = "games"},
    {.id = "dice", .name = "Кубик", .open = dc_open, .close = toy_close, .imu = toy_imu, .group = "games"},
    {.id = "maze", .name = "Лабиринт", .open = mz_open, .close = mz_close, .imu = mz_imu, .button = mz_button, .group = "games"},
    {.id = "notify", .name = "Уведомления", .open = phone_notify_open, .close = phone_notify_close,
     .button = phone_notify_button, .group = "phone"},
    {.id = "music", .name = "Музыка", .open = phone_music_open, .close = phone_music_close,
     .button = phone_music_button, .group = "phone"},
    {.id = "camera", .name = "Камера", .open = phone_camera_open, .close = phone_camera_close,
     .button = phone_camera_button, .group = "phone"},
    {.id = "locate", .name = "Локатор", .open = phone_locate_open, .close = phone_locate_close, .group = "phone"},
    {.id = "sound", .name = "Звуки", .open = snd_open, .group = "settings"},
    {.id = "style", .name = "Темы", .open = style_open, .group = "settings"},
    {.id = "bright", .name = "Яркость", .open = br_open, .group = "settings"},
    {.id = "eyes", .name = "Глаза", .open = eyes_open, .close = eyes_close, .group = "settings"},
    {.id = "privacy", .name = "Приватность", .open = phone_privacy_open, .group = "settings"},
    {.id = "bt", .name = "Bluetooth", .open = phone_bt_open, .close = phone_bt_close, .group = "settings"},
};
const int app_total = sizeof apps / sizeof apps[0];

void apps_init(void) { lv_timer_create(timer_watch, 250, NULL); }
