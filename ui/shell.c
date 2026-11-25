#include "shell.h"
#include "phone.h"
#include "pip.h"
#include "theme.h"
#include "voice.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SCR 466
#define ROW 76.0f          /* list: px between rows: a fingertip each */
#define LENS_X 80          /* list: the focus plate, rows read through it */
#define LENS_W 320
#define LENS_H 56

#define MAX_APPS 32 /* rows the list can hold: every app, grouped or not (apps.c checks it) */
static struct {
    lv_obj_t *pip_scr, *menu_scr, *app_scr;
    lv_obj_t *rows[MAX_APPS], *marker;
    bool updating;            /* shell_updating(): the screen stays lit until the reset */
    lv_obj_t *setup;          /* shell_setup(): "РЕЖИМ НАСТРОЙКИ" on top while the panel is linked, NULL otherwise */
    bool gated;               /* the open game is one with him and he is worn out: app_tired is on screen */
    lv_obj_t *lens, *lens_rows[MAX_APPS], *lens_cursor, *chrome, *head, *scroll; /* the list's plate; the terminal's header and scroll cells */
    th_cell_t scroll_cells[MAX_APPS];
    int tick_row; /* tick_row: the wheel clicks each time a new row reaches the middle */
    int open; /* app index, -1 = none */
    /* the wheel: where it is (in rows), dragged from, snapping from and to; its speed when let go */
    float pos, drag_from, anim_from, anim_to, vy;
    /* input */
    lv_point_t press, last;
    uint32_t last_tick;
    int64_t now;
    int tz;
    /* the list shows apps lo..lo+n-1: the menu, or a group (the games) in place of it */
    int lo, n;
    const char *group;
    float saved_pos; /* where the menu was before the group */
} sh = {.open = -1};
#define CUR (sh.gated ? &app_tired : &apps[sh.open]) /* what is on screen: the app, or the tired note in its place */
#define PLAY_TILT 0.06f        /* a game: gravity moved this much (g) since it last counted is play (shell_imu) */
#define PLAY_STILL_MS 120000   /* lying still and untouched this long, a game lets the sleep timer run */
#define ATTN_IDLE_MS 20000     /* off his face, used within this long: attention to him (pet_attention) */
static struct { float g[3]; uint32_t at; } play;

/* ---------- the menu's layout, set on the panel (shell_menu_set) ---------- */
#define TOP 0xFF /* mn.par: on the top list */
static struct {
    uint8_t par[MAX_APPS];      /* TOP, or the index of the block it is in */
    uint8_t seq[MAX_APPS];      /* every app, in the layout's order */
    const char *name[MAX_APPS]; /* NULL: apps[i].name */
    char *text, *names;         /* the layout as set and the names' storage, MENU_TEXT_MAX each (PSRAM on the board) */
    void (*cb)(const char *text);
    /* the list's rows in order: the top list's (a block only with something in it), then each block's apps */
    uint8_t slot[MAX_APPS];
    int16_t blk_lo[MAX_APPS], blk_n[MAX_APPS];
    int slots, top_n;
} mn;

static bool is_block(int i) { return !apps[i].open; } /* the group owners: "Игры", "Утилиты"… */
static int app_find(const char *id, int len)
{
    for (int i = 0; i < app_total; i++)
        if ((int)strlen(apps[i].id) == len && !strncmp(apps[i].id, id, len)) return i;
    return -1;
}
static int slot_of(int i)
{
    for (int s = 0; s < mn.slots; s++)
        if (mn.slot[s] == i) return s;
    return -1;
}
const char *shell_app_name(int i) { return i < 0 || i >= app_total ? "" : mn.name[i] ? mn.name[i] : apps[i].name; }
const char *shell_title(const char *id) { return shell_app_name(app_find(id, (int)strlen(id))); }

static bool name_ok(const char *s, int len) /* fits a row, and the JSON it goes back in needs no escapes */
{
    if (len <= 0 || len > MENU_NAME_MAX) return false;
    for (int k = 0; k < len; k++)
        if ((unsigned char)s[k] < 0x20 || s[k] == '"' || s[k] == '\\') return false;
    int chars = 0;
    for (int k = 0; k < len; k++) chars += ((unsigned char)s[k] & 0xC0) != 0x80; /* a UTF-8 letter's first byte */
    return chars <= MENU_NAME_CHARS; /* as the panel's NAME_MAX: what fits a row in every theme */
}

static void menu_build(const char *text)
{
    bool placed[MAX_APPS] = {0};
    int n = 0, used = 0;
    for (int i = 0; i < app_total; i++) mn.name[i] = NULL;
    for (const char *p = text ? text : ""; *p;) {
        const char *e = strchr(p, '\n');
        if (!e) e = p + strlen(p);
        const char *w = p;
        const char *we = w;
        while (we < e && *we != ' ') we++;
        if (we - w == 3 && !strncmp(w, "put", 3) && we < e) { /* put <top|block> ids… */
            const char *t = we + 1, *te = t;
            while (te < e && *te != ' ') te++;
            int to = te - t == 3 && !strncmp(t, "top", 3) ? TOP : app_find(t, (int)(te - t));
            if (to >= 0 && (to == TOP || is_block(to)))
                for (const char *a = te; a < e;) {
                    while (a < e && *a == ' ') a++;
                    const char *ae = a;
                    while (ae < e && *ae != ' ') ae++;
                    int k = ae > a ? app_find(a, (int)(ae - a)) : -1;
                    if (k >= 0 && !placed[k] && (to == TOP || !is_block(k))) placed[k] = true, mn.par[k] = (uint8_t)to, mn.seq[n++] = (uint8_t)k;
                    a = ae;
                }
        } else if (we - w == 4 && !strncmp(w, "name", 4) && we < e) { /* name <id> <text> */
            const char *t = we + 1, *te = t;
            while (te < e && *te != ' ') te++;
            int k = app_find(t, (int)(te - t));
            const char *s = te < e ? te + 1 : e;
            int len = (int)(e - s);
            while (len > 0 && s[len - 1] == ' ') len--; /* trailing spaces (and a \r) are not part of it */
            while (len > 0 && s[len - 1] == '\r') len--;
            if (k >= 0 && name_ok(s, len) && mn.names && used + len + 1 <= MENU_TEXT_MAX &&
                !((int)strlen(apps[k].name) == len && !strncmp(apps[k].name, s, len))) {
                memcpy(mn.names + used, s, len), mn.names[used + len] = 0;
                mn.name[k] = mn.names + used, used += len + 1;
            }
        }
        p = *e ? e + 1 : e;
    }
    for (int i = 0; i < app_total; i++) /* the rest where they are by default */
        if (!placed[i]) {
            int g = apps[i].group ? app_find(apps[i].group, (int)strlen(apps[i].group)) : -1;
            mn.par[i] = g >= 0 ? (uint8_t)g : TOP, mn.seq[n++] = (uint8_t)i;
        }
    for (int i = 0; i < app_total; i++) mn.blk_n[i] = 0, mn.blk_lo[i] = -1;
    for (int k = 0; k < n; k++)
        if (mn.par[mn.seq[k]] != TOP) mn.blk_n[mn.par[mn.seq[k]]]++;
    int s = 0;
    for (int k = 0; k < n; k++) {
        int i = mn.seq[k];
        if (mn.par[i] == TOP && (!is_block(i) || mn.blk_n[i])) mn.slot[s++] = (uint8_t)i;
    }
    mn.top_n = s;
    for (int k = 0; k < n; k++) {
        int b = mn.seq[k];
        if (mn.par[b] != TOP || !is_block(b) || !mn.blk_n[b]) continue;
        mn.blk_lo[b] = (int16_t)s;
        for (int j = 0; j < n; j++)
            if (mn.par[mn.seq[j]] == b) mn.slot[s++] = mn.seq[j];
    }
    mn.slots = s;
}

static void menu_relayout(void);
bool shell_menu_set(const char *text)
{
    if (!text) text = "";
    size_t len = strlen(text);
    if (len >= MENU_TEXT_MAX) return false;
    if (!mn.text) { /* once, in one piece: big enough to land in PSRAM on the board, internal RAM stays as it was */
        mn.text = malloc(2 * MENU_TEXT_MAX);
        if (!mn.text) return false;
        mn.names = mn.text + MENU_TEXT_MAX;
    }
    memmove(mn.text, text, len + 1);
    menu_build(mn.text);
    if (sh.menu_scr) menu_relayout();
    if (mn.cb) mn.cb(mn.text);
    return true;
}
const char *shell_menu_text(void) { return mn.text ? mn.text : ""; }
void shell_menu_on_change(void (*cb)(const char *text)) { mn.cb = cb; }

static int json_app(char *o, int cap, int n, int i)
{
    n += lv_snprintf(o + n, n < cap ? cap - n : 0, "[\"%s\",\"%s\",\"%s\"", apps[i].id, apps[i].name, mn.name[i] ? mn.name[i] : "");
    if (is_block(i)) {
        n += lv_snprintf(o + n, n < cap ? cap - n : 0, ",[");
        bool first = true;
        for (int k = 0; k < app_total; k++)
            if (mn.par[mn.seq[k]] == i) {
                if (!first) n += lv_snprintf(o + n, n < cap ? cap - n : 0, ",");
                n = json_app(o, cap, n, mn.seq[k]), first = false;
            }
        n += lv_snprintf(o + n, n < cap ? cap - n : 0, "]");
    }
    return n + lv_snprintf(o + n, n < cap ? cap - n : 0, "]");
}
int shell_menu_json(char *out, int cap)
{
    if (!mn.slots) menu_build(shell_menu_text());
    int n = lv_snprintf(out, cap, "[");
    bool first = true;
    for (int k = 0; k < app_total; k++)
        if (mn.par[mn.seq[k]] == TOP) {
            if (!first) n += lv_snprintf(out + n, n < cap ? cap - n : 0, ",");
            n = json_app(out, cap, n, mn.seq[k]), first = false;
        }
    n += lv_snprintf(out + n, n < cap ? cap - n : 0, "]");
    return n < cap ? n : -1; /* -1: did not fit */
}

int64_t shell_local_time(void) { return sh.now + (int64_t)sh.tz * 60; }

static struct { int pct; bool charging; } bat = {-1, false};
static void head_show(void);
void shell_battery(int percent, bool charging)
{
    bool changed = percent != bat.pct || charging != bat.charging;
    bat.pct = percent, bat.charging = charging, pip_battery(percent, charging);
    if (changed) head_show(); /* the menu's battery, drawn in the theme's own inks */
}
int shell_battery_level(bool *charging)
{
    if (charging) *charging = bat.charging;
    return bat.pct;
}
const char *shell_battery_text(void)
{
    static char t[8];
    if (bat.pct < 0) return "--";
    lv_snprintf(t, sizeof t, "%d%%%s", bat.pct, bat.charging ? "+" : "");
    return t;
}

int shell_mic(void) { return pip_mic_wanted() ? 2 : !strcmp(shell_where(), "noise") ? 1 : 0; }

bool shell_in_setup(void) { return sh.setup != NULL; }

const char *shell_where(void)
{
    if (sh.setup) return "setup"; /* the panel is linked: "РЕЖИМ НАСТРОЙКИ" over all of it */
    const char *over = phone_where(); /* a call, the pairing or a banner on top */
    if (over) return over;
    if (apps_alert_on()) return "timer_done";
    lv_obj_t *a = lv_screen_active();
    return a == sh.pip_scr ? "pip" : a == sh.menu_scr ? "menu" : sh.open >= 0 ? CUR->id : "?";
}

static void open_app(int i);
static void group_leave(void);
static const char *group_title(const char *id);

static void list_layout(void);

/* the wheel coasts to a row */
static void snap_exec(void *var, int32_t v)
{
    (void)var;
    sh.pos = sh.anim_from + (sh.anim_to - sh.anim_from) * v / 1000.0f;
    list_layout();
}

static void snap_ms(float target, uint32_t ms)
{
    sh.anim_from = sh.pos;
    sh.anim_to = target;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &sh);
    lv_anim_set_exec_cb(&a, snap_exec);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void snap_to(float target) { snap_ms(target, 260); }

/* ---------- the menu: a list wheel in every theme (Ретро draws it with its cells and cursor) ---------- */

static int list_middle(void) { return (int)fmaxf(0, fminf(sh.n - 1, roundf(sh.pos))); }

/* size and ink by distance from the middle, continuous: 36 px in the plate, 26 one row away, 20 two away */
static float row_scale(float ad) { return (ad < 1 ? 36 - ad * 10 : ad < 2 ? 26 - (ad - 1) * 6 : 20) / 36.0f; }

/* Area-averaging downscale of an alpha mask by f (0.5..1): separable, each output pixel covers at most three source
   pixels a side, weights in 1/256. Fast enough for the ESP32 (it runs when the theme changes). */
#define SHRINK_MAX 512
static void shrink_weights(int n_out, float f, int n_in, int16_t idx[][3], uint8_t wt[][3])
{
    for (int o = 0; o < n_out; o++) {
        float a = o / f, b = (o + 1) / f;
        int t = 0;
        for (int s = (int)a; s < (int)ceilf(b) && s < n_in && t < 3; s++, t++) {
            float cover = fminf(b, s + 1) - fmaxf(a, s);
            idx[o][t] = (int16_t)s, wt[o][t] = (uint8_t)lroundf(cover * f * 255);
        }
        for (; t < 3; t++) idx[o][t] = 0, wt[o][t] = 0;
    }
}

static void shrink(const uint8_t *src, int sstride, int w, int h, uint8_t *dst, int dstride, int mw, int mh, float f)
{
    static int16_t xi[SHRINK_MAX][3], yi[SHRINK_MAX][3];
    static uint8_t xw[SHRINK_MAX][3], yw[SHRINK_MAX][3];
    if (mw > SHRINK_MAX || mh > SHRINK_MAX) return;
    shrink_weights(mw, f, w, xi, xw);
    shrink_weights(mh, f, h, yi, yw);
    for (int y = 0; y < mh; y++)
        for (int x = 0; x < mw; x++) {
            uint32_t v = 0;
            for (int j = 0; j < 3; j++) {
                const uint8_t *row = src + yi[y][j] * sstride;
                v += yw[y][j] * (uint32_t)(row[xi[x][0]] * xw[x][0] + row[xi[x][1]] * xw[x][1] + row[xi[x][2]] * xw[x][2]);
            }
            v = (v + 32512) / 65025; /* both weights were x 255 */
            dst[y * dstride + x] = (uint8_t)(v > 255 ? 255 : v);
        }
}

/* Each row is drawn once, as an alpha mask in the big font, and shrunk ahead of time to every size from 20 to 36 px:
   scrolling then only picks a mask and tints it. (Scaling text live made LVGL render every row into a layer and
   transform it each frame: ~45 ms a menu frame on the board.) */
#define ROW_PX_MIN 20
#define ROW_PX_MAX 36
#define ROW_SIZES (ROW_PX_MAX - ROW_PX_MIN + 1)
#define ROW_W_MAX 640 /* px in the big font: a 16-letter name drawn on one line, never wrapped */
static lv_draw_buf_t *row_mask[MAX_APPS][ROW_SIZES];
/* every size of a row in one block (the headers, then the pixels): a few hundred small masks one by one went to the
   board's scarce internal RAM (allocations under 2 KB stay there) and it ran out before the speaker started */
static void *row_block[MAX_APPS];

static void rows_build(void)
{
    /* an off-screen canvas and one scratch mask, wide enough for any row, to draw the text with */
    static lv_obj_t *pen;
    static lv_draw_buf_t *base;
    const lv_font_t *font = th->title ? th->title : th->body; /* 40 px capitals, shrunk; never scaled up */
    int h = lv_font_get_line_height(font), big = th->title ? 40 : 28; /* the font's px size */
    if (base && base->header.h != h) lv_draw_buf_destroy(base), base = NULL;
    if (!base) base = lv_draw_buf_create(ROW_W_MAX, h, LV_COLOR_FORMAT_A8, 0);
    LV_ASSERT_MALLOC(base);
    if (!pen) pen = lv_canvas_create(sh.menu_scr), lv_obj_add_flag(pen, LV_OBJ_FLAG_HIDDEN);
    lv_canvas_set_draw_buf(pen, base);
    for (int i = 0; i < app_total; i++) { /* i: the row (mn.slot), not the app */
        if (i >= mn.slots) { /* no row this far (an empty block is not listed) */
            lv_free(row_block[i]), row_block[i] = NULL;
            for (int k = 0; k < ROW_SIZES; k++) row_mask[i][k] = NULL;
            continue;
        }
        char text[MENU_NAME_MAX + 8];
        const char *name = shell_app_name(mn.slot[i]);
        lv_snprintf(text, sizeof text, "%s", name); /* the name alone: no "01" before the top rows any more */
        const char *t = th_label(text);
        lv_point_t sz;
        lv_text_get_size(&sz, t, font, th->caps, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        int ix = 0;
        int w = sz.x + 2 < ROW_W_MAX ? sz.x + 2 : ROW_W_MAX;
        lv_draw_buf_clear(base, NULL);
        lv_layer_t layer;
        lv_canvas_init_layer(pen, &layer);
        lv_draw_label_dsc_t dsc;
        lv_draw_label_dsc_init(&dsc);
        dsc.font = font, dsc.text = t, dsc.letter_space = th->caps, dsc.color = lv_color_white();
        lv_area_t a = {ix, 0, w - 1, h - 1};
        lv_draw_label(&layer, &dsc, &a);
        lv_canvas_finish_layer(pen, &layer);
        int mws[ROW_SIZES], mhs[ROW_SIZES];
        size_t at = (ROW_SIZES * sizeof(lv_draw_buf_t) + 15) & ~(size_t)15, size = at;
        for (int k = 0; k < ROW_SIZES; k++) {
            float f = (float)(ROW_PX_MIN + k) / big;
            int mw = (int)lroundf(w * f), mh = (int)lroundf(h * f);
            mws[k] = mw < 1 ? 1 : mw > SHRINK_MAX ? SHRINK_MAX : mw, mhs[k] = mh < 1 ? 1 : mh; /* wider than the glass: cut, unseen */
            size += (lv_draw_buf_width_to_stride(mws[k], LV_COLOR_FORMAT_A8) * mhs[k] + 15) & ~(size_t)15;
        }
        lv_free(row_block[i]);
        uint8_t *blk = row_block[i] = lv_malloc(size);
        LV_ASSERT_MALLOC(blk);
        for (int k = 0; k < ROW_SIZES; k++) { /* area-averaged down to each size */
            float f = (float)(ROW_PX_MIN + k) / big;
            uint32_t stride = lv_draw_buf_width_to_stride(mws[k], LV_COLOR_FORMAT_A8), bytes = stride * mhs[k];
            lv_draw_buf_t *m = row_mask[i][k] = (lv_draw_buf_t *)blk + k;
            lv_draw_buf_init(m, mws[k], mhs[k], LV_COLOR_FORMAT_A8, stride, blk + at, bytes);
            at += (bytes + 15) & ~(size_t)15;
            shrink(base->data, base->header.stride, w, h, m->data, m->header.stride, mws[k], mhs[k], f);
        }
    }
}

static lv_draw_buf_t *row_at(int i, float ad)
{
    int k = (int)lroundf(row_scale(ad) * ROW_PX_MAX) - ROW_PX_MIN;
    k = k < 0 ? 0 : k >= ROW_SIZES ? ROW_SIZES - 1 : k;
    /* the middle row is drawn inside the focus plate (see-through on most themes) and clipped by it: fit it there,
       with room for the terminal's cursor */
    int room = LENS_W - 16 - 8 - (TH_TERMINAL ? 6 + 12 : 0);
    while (k > 0 && (int)row_mask[i][k]->header.w > room) k--;
    return row_mask[i][k];
}

static void list_layout(void)
{
    bool terminal = TH_TERMINAL;
    float settle = fabsf(sh.pos - roundf(sh.pos));
    for (int i = 0; i < app_total; i++) {
        float d = i - sh.lo - sh.pos, ad = fabsf(d);
        lv_obj_t *r = sh.rows[i], *l = sh.lens_rows[i];
        if (i < sh.lo || i >= sh.lo + sh.n) { lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN), lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN); continue; }
        bool far = ad > (TH_TERMINAL ? 1.9f : 2.3f); /* past these it is off the dial (or faded out): not drawn at all */
        lv_obj_set_flag(r, LV_OBJ_FLAG_HIDDEN, far);
        lv_obj_set_flag(l, LV_OBJ_FLAG_HIDDEN, ad > 1.2f);
        if (far) continue;
        /* the row's pre-shrunk mask for this distance: it grows and shrinks a pixel at a time */
        lv_draw_buf_t *m = row_at(i, ad);
        if (lv_image_get_src(r) != m) lv_image_set_src(r, m), lv_image_set_src(l, m);
        lv_color_t ink = lv_color_mix(lv_color_hex(terminal ? th->mute : th->faint), lv_color_hex(th->dim), (uint8_t)(255 * fminf(fmaxf(ad - 1, 0), 1)));
        lv_obj_set_style_image_recolor(r, ink, 0);
        /* the plate is see-through there: hand the row over to it; the terminal fades rows out before its key strip */
        float fade = terminal ? fminf(1, fmaxf(0, (1.9f - ad) * 2)) : fminf(1, ad);
        lv_obj_set_style_opa(r, (lv_opa_t)(255 * fade), 0);
        lv_obj_align(r, LV_ALIGN_LEFT_MID, LENS_X + 16, (int32_t)roundf(d * ROW));
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 16, (int32_t)roundf(d * ROW));
    }
    int mid = list_middle();
    if (mid != sh.tick_row) { /* a detent */
        sh.tick_row = mid;
        if (lv_screen_active() == sh.menu_scr) voice_say(VOICE_TICK);
    }
    if (terminal) {
        lv_obj_set_flag(sh.lens_cursor, LV_OBJ_FLAG_HIDDEN, settle > 0.02f);
        lv_obj_update_layout(sh.lens_rows[sh.lo + mid]);
        lv_obj_align_to(sh.lens_cursor, sh.lens_rows[sh.lo + mid], LV_ALIGN_OUT_RIGHT_MID, 6, 0);
        for (int i = 0; i < sh.n; i++) th_cell_set(sh.scroll, &sh.scroll_cells[i], i == mid ? th->text : th->off);
    } else lv_obj_add_flag(sh.lens_cursor, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_flag(sh.marker, LV_OBJ_FLAG_HIDDEN, terminal);
    head_show();
}

/* over the list, alike in every theme: a battery with the charge written on it on the menu, the group's name inside one */
static struct { lv_obj_t *fill, *text, *bolt; int pct; bool ch; } hb = {.pct = -2};

/* on the charger: the fill turns a green of the theme's own darkness (the text stays readable on it) and a small bolt,
   one polyline, stands before the figure, which moves over a little; the battery keeps its size */
static uint32_t bat_green(void) { return TH_TERMINAL ? 0x486830 : th == &themes[TH_BARBIE] ? 0x186048 : 0x206838; }
static const lv_point_precise_t bolt_pts[] = {{6, 0}, {1, 8}, {6, 8}, {2, 15}};
#define BOLT_W 8

static void bat_make(lv_obj_t *parent)
{
    const int w = 54, h = 22, pad = 2;
    int r = th->radius ? 6 : 0;
    uint32_t ink = TH_TERMINAL ? th->text : th->dim;
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, w + 4, h);
    lv_obj_add_flag(box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 2, 30);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *body = th_box(box, 0, 0, w, h, r, th->bg);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 2, 0);
    lv_obj_set_style_border_color(body, lv_color_hex(ink), 0);
    th_box(box, w + 1, h / 2 - 4, 3, 8, r ? 1 : 0, ink); /* the nub */
    hb.fill = th_box(box, 2 + pad, 2 + pad, 1, h - 4 - 2 * pad, r ? r - 3 : 0, th->faint);
    hb.text = th_text(box, th->mini ? th->mini : th->tiny_bold ? th->tiny_bold : th->small, th->text, "");
    lv_obj_align(hb.text, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_width(hb.text, w);
    lv_obj_set_style_text_align(hb.text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(hb.text, 0, 0);
    hb.bolt = lv_line_create(box);
    lv_line_set_points(hb.bolt, bolt_pts, 4);
    lv_obj_set_style_line_width(hb.bolt, 2, 0);
    lv_obj_set_style_line_rounded(hb.bolt, true, 0);
    lv_obj_set_style_line_color(hb.bolt, lv_color_hex(th->text), 0);
    lv_obj_set_pos(hb.bolt, 6, (h - 16) / 2);
    lv_obj_add_flag(hb.bolt, LV_OBJ_FLAG_HIDDEN);
    hb.pct = -2;
}

static void head_show(void)
{
    if (sh.head) {
        if (sh.group) {
            const char *u = th_label(group_title(sh.group));
            if (strcmp(u, lv_label_get_text(sh.head))) lv_label_set_text(sh.head, u);
        }
        return;
    }
    bool ch = bat.charging && bat.pct >= 0;
    if (!hb.text || (hb.pct == bat.pct && hb.ch == ch)) return;
    hb.pct = bat.pct, hb.ch = ch;
    const int inner = 54 - 4 - 4;
    lv_obj_set_width(hb.fill, bat.pct < 0 ? 0 : 1 + (inner - 1) * bat.pct / 100);
    lv_obj_set_style_bg_color(hb.fill, lv_color_hex(ch ? bat_green() : th->faint), 0);
    lv_obj_set_flag(hb.bolt, LV_OBJ_FLAG_HIDDEN, !ch);
    lv_obj_align(hb.text, LV_ALIGN_LEFT_MID, ch ? BOLT_W / 2 + 1 : 0, 0); /* the figure makes room for the bolt */
    if (bat.pct < 0) lv_label_set_text(hb.text, "--");
    else lv_label_set_text_fmt(hb.text, "%d%%", bat.pct);
}

/* a picker wheel: drag it, flick it and it coasts, slowing down to land exactly on a row */
static void list_input(lv_event_code_t code, lv_point_t p)
{
    int dx = p.x - sh.press.x, dy = p.y - sh.press.y;
    if (code == LV_EVENT_PRESSED) {
        lv_anim_delete(&sh, snap_exec); /* a touch catches a coasting wheel */
        sh.drag_from = sh.pos;
        sh.vy = 0;
    } else if (code == LV_EVENT_PRESSING) {
        uint32_t el = lv_tick_elaps(sh.last_tick);
        if (el > 0) sh.vy = 0.6f * sh.vy + 0.4f * (p.y - sh.last.y) / el; /* px per ms */
        if (abs(dy) > abs(dx)) {
            float pos = sh.drag_from - dy / ROW, lo = 0, hi = sh.n - 1;
            /* past either end it pulls back like a rubber band */
            if (pos < lo) pos = lo - (lo - pos) / (1 + (lo - pos) * 2);
            if (pos > hi) pos = hi + (pos - hi) / (1 + (pos - hi) * 2);
            sh.pos = pos;
            list_layout();
        }
    } else if (code == LV_EVENT_RELEASED) {
        if (dx > 90 && abs(dy) < dx) { /* swipe right: out of a group, or back to Pip */
            if (sh.group) group_leave();
            else snap_to((float)list_middle()), shell_home();
            return;
        }
        if (abs(dx) < 12 && abs(dy) < 12) {                                                  /* tap a row */
            int row = (int)roundf(sh.pos + (p.y - SCR / 2) / ROW);
            if (row == list_middle() && fabsf(sh.pos - row) < 0.1f) voice_say(VOICE_TICK), open_app(mn.slot[sh.lo + row]); /* the click of it */
            else if (row >= 0 && row < sh.n) snap_to((float)row);
            return;
        }
        if (lv_tick_elaps(sh.last_tick) > 60) sh.vy = 0; /* held still before letting go: no throw */
        /* where a wheel losing ~0.3% of its speed per ms would stop, rounded to a row */
        float target = roundf(sh.pos - sh.vy * 330 / ROW);
        target = fmaxf(0, fminf(sh.n - 1, target));
        float rows = fabsf(target - sh.pos);
        snap_ms(target, (uint32_t)fminf(1200, 260 + 110 * rows));
    }
}

/* ---------- the menu screen ---------- */

static void menu_input(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (code == LV_EVENT_PRESSED) sh.press = p;
    list_input(code, p);
    sh.last = p, sh.last_tick = lv_tick_get();
}

static void chrome_build(void) /* the terminal's header and scroll cells, for what the list shows */
{
    lv_obj_clean(sh.chrome);
    sh.head = NULL, hb.text = hb.fill = NULL;
    if (sh.group) {
        sh.head = th_header(sh.chrome, "", 40);
        lv_obj_set_style_text_color(sh.head, lv_color_hex(TH_TERMINAL ? th->text : th->dim), 0);
    } else bat_make(sh.chrome);
    head_show();
    if (!TH_TERMINAL) return;
    int top = SCR / 2 - (sh.n * 11 - 2) / 2;
    /* the scroll cells on the left, before the focus plate, where the other themes have their pointer */
    for (int i = 0; i < sh.n; i++) sh.scroll_cells[i] = (th_cell_t){LENS_X - 22, (int16_t)(top + i * 11), 9, 9, th->off};
    sh.scroll = th_cells(sh.chrome, sh.scroll_cells, sh.n);
}

/* a group (block b: the games) takes the list's place; pos: the row to land on */
static void group_enter(int b, int pos)
{
    if (!sh.group) sh.saved_pos = sh.pos;
    sh.group = apps[b].id, sh.lo = mn.blk_lo[b], sh.n = mn.blk_n[b];
    sh.pos = (float)pos, sh.tick_row = pos;
    chrome_build();
    list_layout();
}

static void group_leave(void)
{
    sh.group = NULL, sh.lo = 0, sh.n = mn.top_n;
    sh.pos = fminf(sh.saved_pos, (float)(sh.n - 1)), sh.tick_row = (int)sh.pos;
    chrome_build();
    list_layout();
}

static const char *group_title(const char *id) /* the owner row's name: "Игры", "Утилиты", "Настройки" */
{
    return shell_app_name(app_find(id, (int)strlen(id)));
}

/* repaint the menu for the current theme: background, fonts, the list rows */
static void restyle_menu(void)
{
    th_screen(sh.menu_scr);
    for (int i = 0; i < app_total; i++) {
        lv_obj_set_style_image_recolor(sh.lens_rows[i], lv_color_hex(TH_TERMINAL ? th->bg : th->text), 0);
        lv_image_set_src(sh.rows[i], NULL), lv_image_set_src(sh.lens_rows[i], NULL); /* the masks are about to change */
    }
    rows_build();
    lv_obj_set_style_bg_opa(sh.lens, TH_TERMINAL ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(sh.lens, lv_color_hex(th->text), 0);
    lv_obj_set_style_bg_color(sh.lens_cursor, lv_color_hex(th->bg), 0);
    chrome_build();
    lv_obj_set_style_bg_color(sh.marker, lv_color_hex(th->accent), 0);
    /* the pointer: a dot on rounded themes, a thin upright bar on square ones */
    bool round = th->radius != 0;
    lv_obj_set_size(sh.marker, round ? 14 : 4, round ? 14 : 30);
    lv_obj_set_style_radius(sh.marker, round ? LV_RADIUS_CIRCLE : 0, 0);
    lv_obj_align(sh.marker, LV_ALIGN_LEFT_MID, round ? 62 : 66, 0);
    sh.vy = 0;
    sh.pos = (float)list_middle(), sh.tick_row = list_middle(); /* no click for setting it */
    list_layout();
}

/* a new layout from the panel: back on the top list, every row drawn again (as for a theme) */
static void menu_relayout(void)
{
    sh.group = NULL, sh.lo = 0, sh.n = mn.top_n;
    sh.saved_pos = 0;
    restyle_menu();
}

/* load a screen: the terminal scans it in top-down, the other themes slide or fade */
/* A thin, faint bar at the right edge says the back gesture is there (edge_input): one small object on the top layer,
   never moved or animated, shown off his face only, in the theme's ink at a third */
static lv_obj_t *edge_hint;
static void edge_hint_show(bool on)
{
    if (!edge_hint) return;
    if (!on) { lv_obj_add_flag(edge_hint, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_set_style_bg_color(edge_hint, lv_color_hex(th->text), 0); /* the theme may have changed */
    lv_obj_set_style_radius(edge_hint, th->radius ? LV_RADIUS_CIRCLE : 0, 0);
    lv_obj_remove_flag(edge_hint, LV_OBJ_FLAG_HIDDEN);
}

static void go(lv_obj_t *scr, lv_screen_load_anim_t anim, uint32_t ms, bool delete_old)
{
    pip_away(scr != sh.pip_scr); /* off his face he is switched off: the menu and the apps alone answer */
    edge_hint_show(scr != sh.pip_scr);
    if (!TH_TERMINAL) { lv_screen_load_anim(scr, anim, ms, 0, delete_old); return; }
    lv_obj_t *old = lv_screen_active();
    th_wipe(scr, lv_color_to_u32(lv_obj_get_style_bg_color(scr, 0)) & 0xFFFFFF);
    if (delete_old && old != scr) lv_obj_delete_async(old);
}

static void show_menu(lv_screen_load_anim_t anim)
{
    list_layout();
    go(sh.menu_scr, anim, 250, false);
}

/* ---------- apps ---------- */

static void back_to_menu(void);

static void close_app(void)
{
    if (sh.open < 0) return;
    if (CUR->close) CUR->close();
    sh.open = -1, sh.gated = false;
}

static void open_app(int i)
{
    if (is_block(i)) { /* the list turns into the group */
        if (!mn.blk_n[i]) return; /* nothing in it: not even listed */
        bool app = sh.open >= 0; /* opened from an app (shell_open): that one is closed and its screen goes */
        close_app();
        group_enter(i, 0);
        if (lv_screen_active() != sh.menu_scr) list_layout(), go(sh.menu_scr, LV_SCREEN_LOAD_ANIM_FADE_IN, 250, app);
        return;
    }
    lv_obj_t *old = sh.open >= 0 ? sh.app_scr : NULL; /* jumping from one app straight into another */
    close_app();
    sh.open = i;
    play.at = lv_tick_get(); /* a game just opened counts as played */
    sh.gated = apps[i].with_pip && !pip_wants_play(); /* worn out: no games with him */
    sh.app_scr = lv_obj_create(NULL);
    th_screen(sh.app_scr);
    lv_obj_remove_flag(sh.app_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(sh.app_scr, th->body, 0);
    lv_obj_set_style_text_color(sh.app_scr, lv_color_hex(th->text), 0);
    CUR->open(sh.app_scr);
    /* the way back: the bottom button, or the back gesture from the right edge (edge_input) */
    if (TH_TERMINAL) {
        go(sh.app_scr, LV_SCREEN_LOAD_ANIM_NONE, 0, false);
        if (old) lv_obj_delete_async(old);
        return;
    }
    pip_away(true);
    edge_hint_show(true);
    lv_screen_load_anim(sh.app_scr, LV_SCREEN_LOAD_ANIM_FADE_IN, 200, 0, old != NULL); /* the replaced app goes after */
}

static void group_return(int i) /* the list shows app i's group, on app i */
{
    int b = mn.par[i];
    group_enter(b, slot_of(i) - mn.blk_lo[b]);
}

static void back_to_menu(void)
{
    if (sh.open < 0) return;
    int i = sh.open;
    bool grouped = mn.par[i] != TOP;
    close_app();
    if (grouped) group_return(i); /* or to its group in the list, on this one */
    list_layout();
    go(sh.menu_scr, LV_SCREEN_LOAD_ANIM_MOVE_BOTTOM, 250, true); /* the app's screen goes when the slide is done */
}

/* ---------- navigation ---------- */

bool shell_open(const char *id)
{
    if (pip_dead()) return false; /* nothing opens any more */
    for (int i = 0; i < app_total; i++)
        if (!strcmp(apps[i].id, id)) {
            if (is_block(i) && !mn.blk_n[i]) return false; /* an empty block: nothing would open */
            if (mn.par[i] == TOP && slot_of(i) >= 0) {
                if (sh.group) group_leave();
                sh.pos = (float)slot_of(i);
            } /* a grouped one leaves the menu where it was */
            list_layout();
            open_app(i);
            return true;
        }
    return false;
}

void shell_set_theme(int theme)
{
    theme_select(theme);
    lv_obj_t *app = sh.open >= 0 ? sh.app_scr : NULL;
    int was = sh.open;
    close_app();
    restyle_menu();
    if (was >= 0 && mn.par[was] != TOP) group_return(was), list_layout(); /* back on that app's row in its group */
    if (app) { /* set from the panel with an app open: land in the restyled menu at once, then drop the app's screen.
                  (A fade plus a delayed delete raced: restyling blocks for a moment, both timers came due together and
                  the screen was freed while the fade still used it - LVGL then spun on a dead object.) */
        pip_away(true), lv_screen_load(sh.menu_scr);
        lv_obj_delete_async(app);
    }
}

/* The panel is about to write new firmware: say so on top of everything. The chip then sits in the flasher and draws
   nothing, but the panel keeps its last frame, so this stays on the glass until the new firmware boots. */
void shell_updating(void)
{
    if (sh.updating) return;
    shell_setup(false); /* the flash's word takes over */
    pip_covered(true);
    sh.updating = true;
    pip_wake();
    lv_obj_t *o = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, SCR, SCR);
    th_screen(o);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE); /* swallows touches */
    th_text_at(o, th->title ? th->title : th->body, th->text, th_label("Обновление"), -1, 176);
    th_text_at(o, th->small, th->accent, th_label("Не отключай кабель"), -1, 252);
}

/* The panel is linked (main.c: its "panel" heartbeat, or its Bluetooth link): say so on top of everything and take no
   input - touches land on the overlay, keys and motion are dropped (shell_button, shell_imu) - until it lets go. */
static lv_timer_t *boot_hold, *wash_hold;
static bool boot_tapped, boot_washing;
void shell_setup(bool on)
{
    if (on == (sh.setup != NULL) || (on && sh.updating)) return;
    pip_covered(on); /* nothing of him shows under it: no frames drawn for nothing */
    if (!on) { lv_obj_delete(sh.setup), sh.setup = NULL; pip_seen(); return; }
    if (wash_hold) lv_timer_delete(wash_hold), wash_hold = NULL; /* a key half-pressed: forgotten */
    if (boot_hold) lv_timer_delete(boot_hold), boot_hold = NULL;
    if (boot_washing) boot_washing = false;
    boot_tapped = false;
    pip_wash(false);
    pip_wake();
    lv_obj_t *o = sh.setup = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, SCR, SCR);
    th_screen(o);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE); /* swallows touches */
    lv_obj_t *l = th_text_at(o, th->title ? th->title : th->body, th->text, "РЕЖИМ\nНАСТРОЙКИ", -1, 170);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    th_text_at(o, th->small, th->accent, th_label("Подключён к панели"), -1, 320);
}

void shell_home(void)
{
    if (lv_screen_active() == sh.pip_scr) return;
    if (sh.group) group_leave(); /* the menu comes back as the menu */
    bool app = sh.open >= 0;
    close_app();
    go(sh.pip_scr, LV_SCREEN_LOAD_ANIM_MOVE_BOTTOM, 250, app); /* an app screen is deleted after */
}

/* The bottom key on Pip's screen: let go quickly = a tap (meet, or the next of a series), hold it = feed him (it
   fires while still held). A tap and then, at once, press and hold = wash him: the sponge scrubs until let go.
   (Not on the top key: held for seconds that one is the power chip's own power off.) */
#define FEED_HOLD_MS 600
#define WASH_AFTER_MS 450 /* the second press must come this soon after the tap's release */
#define WASH_HOLD_MS 300  /* and be held this long */
static uint32_t boot_tap_tick;
static void boot_held(lv_timer_t *t)
{
    (void)t;
    boot_hold = NULL; /* a one-shot timer deletes itself */
    if (lv_screen_active() == sh.pip_scr) pip_feed();
}
static void wash_held(lv_timer_t *t)
{
    (void)t;
    wash_hold = NULL;
    if (lv_screen_active() == sh.pip_scr) pip_wash(true), boot_washing = true;
}

/* the bottom key stepping back (app -> list, group -> menu): pressed again this soon, it goes all the way to Pip */
#define BACK_TWICE_MS 450
static uint32_t back_tick;
static bool back_armed;

/* Top (PWR): Pip -> menu -> the app in the middle; apps may use it. Bottom (BOOT) on Pip: a tap is "meet", a hold
   feeds him, a tap then a hold washes him; back to Pip from the menu, back to the menu from an app unless the app
   uses it; twice quickly: to Pip. */
void shell_button(int id)
{
    if (sh.setup || sh.updating) return; /* the panel is at work: the keys do nothing */
    if (pip_dead()) { pip_seen(); return; } /* he is gone: a button only lights the screen that says so */
    if (pip_unmet()) { pip_hatch(); return; } /* the very first press after a clean flash wakes him: no menu yet */
    bool dark = !pip_display_on();
    pip_seen();
    play.at = lv_tick_get(); /* a key in a game is play */
    /* the press that wakes the screen does nothing else - but a ringing call or timer takes it whatever (answer or
       decline, hush the alarm) */
    if (dark && !apps_alert_on() && !(phone_where() && !strcmp(phone_where(), "call"))) return;
    if (phone_button(id)) return; /* a call, the pairing or a banner answers first */
    if (apps_alert_on()) { apps_alert_close(); return; } /* "time's up" on top: any button puts it away */
    lv_obj_t *now = lv_screen_active();
    bool twice = id == PIP_BTN_BOOT && back_armed && lv_tick_elaps(back_tick) < BACK_TWICE_MS;
    back_armed = false;
    if (now == sh.pip_scr) {
        if (id == PIP_BTN_PWR) show_menu(LV_SCREEN_LOAD_ANIM_FADE_IN);
        else if (boot_tapped && lv_tick_elaps(boot_tap_tick) < WASH_AFTER_MS) { /* right after a tap: a wash, if held */
            boot_tapped = false;
            if (!wash_hold) wash_hold = lv_timer_create(wash_held, WASH_HOLD_MS, NULL), lv_timer_set_repeat_count(wash_hold, 1);
        } else if (!boot_hold) { /* decided on release (meet) or after FEED_HOLD_MS (feed) */
            boot_tapped = false;
            boot_hold = lv_timer_create(boot_held, FEED_HOLD_MS, NULL);
            lv_timer_set_repeat_count(boot_hold, 1);
        }
    } else if (now == sh.menu_scr) {
        if (id == PIP_BTN_PWR) voice_say(VOICE_TICK), open_app(mn.slot[sh.lo + list_middle()]);
        else if (twice) shell_home();                                  /* the second of two quick presses: home */
        else if (sh.group) group_leave(), back_armed = true, back_tick = lv_tick_get(); /* out of the games first */
        else shell_home();
    } else if (sh.open >= 0) {
        if (twice) { shell_home(); return; }
        /* a bottom press the app takes for itself (a notification's text back to the list, the stopwatch's reset) is a
           step back too: the next one this soon goes home (a banner tapped opens straight into the text) */
        if (!(CUR->button && CUR->button(id))) back_to_menu();
        back_armed = id == PIP_BTN_BOOT, back_tick = lv_tick_get();
    }
}

/* The back gesture: a finger put down at the very right edge and drawn left steps back, as the bottom key does (app ->
   list, group -> menu, menu -> Pip; a step a swipe), from the menu and every app; on his face it does nothing.
   Watched on the touch input itself (PRESSED and RELEASED reach it before any widget), so no app has to know; the
   release that made it is kept from the widget under the finger (no tap, no click). Not a step back: the stopwatch's
   reset (its bottom key), so there it just leaves; and nothing while a call, a pairing code or a banner is up. */
#define EDGE_W 36    /* px from the right edge where it starts */
#define EDGE_DX 70   /* px drawn left, more across than up or down */
static struct { lv_point_t at; bool on, swallow; } edge;

static void edge_back(void)
{
    if (sh.setup || sh.updating || pip_dead() || pip_unmet() || phone_where()) return;
    if (apps_alert_on()) { apps_alert_close(); return; }
    lv_obj_t *now = lv_screen_active();
    if (now == sh.pip_scr) return;
    if (now != sh.menu_scr && sh.open >= 0 && !strcmp(CUR->id, "stopwatch")) { /* bottom would reset it: just leave */
        pip_seen();
        back_to_menu();
        back_armed = true, back_tick = lv_tick_get();
        return;
    }
    shell_button(PIP_BTN_BOOT);
}

static void edge_input(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *in = lv_indev_active();
    if (!in) return;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    if (code == LV_EVENT_PRESSED) edge.at = p, edge.on = p.x >= SCR - EDGE_W, edge.swallow = false;
    else if (code == LV_EVENT_RELEASED && edge.on) {
        int dx = p.x - edge.at.x, dy = p.y - edge.at.y;
        edge.on = false;
        if (-dx >= EDGE_DX && abs(dy) < -dx) edge.swallow = true, lv_indev_stop_processing(in), edge_back();
    } else if ((code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_CLICKED) && edge.swallow) lv_indev_stop_processing(in);
}

static bool clock_known = true; /* the board says otherwise until a phone or the panel sets the clock */
void shell_clock_known(bool known) { clock_known = known, pip_clock_known(known); }
bool shell_clock_is_known(void) { return clock_known; }

void shell_time(int64_t unix_s, int tz_min)
{
    sh.now = unix_s;
    sh.tz = tz_min;
    bool was = pip_dead();
    pip_time(unix_s, tz_min);
    if (pip_dead() && !was) { /* he just died: whatever was open goes, his screen says it */
        apps_alert_close();
        if (lv_screen_active() != sh.pip_scr) shell_home();
    }
}

void shell_button_up(int id)
{
    if (id != PIP_BTN_BOOT || pip_dead() || sh.setup) return;
    bool tap = false;
    if (wash_hold) lv_timer_delete(wash_hold), wash_hold = NULL, tap = true; /* let go before the sponge came: just another tap */
    else if (boot_washing) boot_washing = false, pip_wash(false);            /* the bath is over */
    else if (boot_hold) lv_timer_delete(boot_hold), boot_hold = NULL, tap = true; /* let go before it fed him: a tap */
    if (tap && lv_screen_active() == sh.pip_scr) {
        pip_tap(); /* a hello, or the next of a series (pip.c) */
        boot_tapped = true, boot_tap_tick = lv_tick_get();
    }
}

/* A game played without touching holds the screen lit (the ones with .imu: the ball and the maze are tilted, the toys
   shaken); every other app and game sleeps on the set timer, touches keep it lit as anywhere. Tilted (gravity moved by more than PLAY_TILT g since it last counted), touched or a key
   pressed is play; left lying still and untouched for PLAY_STILL_MS the sleep timer runs as ever (a game left on the
   table does not drain the battery). */
static bool in_game(void) { return sh.open >= 0 && apps[sh.open].group && !strcmp(apps[sh.open].group, "games"); }
static bool playing(float ax, float ay, float az)
{
    if (!in_game()) return false;
    float dx = ax - play.g[0], dy = ay - play.g[1], dz = az - play.g[2];
    if (dx * dx + dy * dy + dz * dz > PLAY_TILT * PLAY_TILT) play.g[0] = ax, play.g[1] = ay, play.g[2] = az, play.at = lv_tick_get();
    lv_display_t *d = lv_display_get_default();
    return lv_tick_elaps(play.at) < PLAY_STILL_MS || (d && lv_display_get_inactive_time(d) < PLAY_STILL_MS);
}

void shell_imu(float ax, float ay, float az, float dt)
{
    bool game = playing(ax, ay, az), awake = game && CUR->imu;
    pip_hold_awake(awake || sh.updating || sh.setup);
    /* attention (pet_attention): you busy with the keychain off his face - a game being played, or the menu or an app
       touched or a key pressed within ATTN_IDLE_MS (an app left lying open counts for nothing) */
    int attn = 0;
    if (lv_screen_active() != sh.pip_scr && pip_display_on() && !sh.setup && !sh.updating) {
        lv_display_t *d = lv_display_get_default();
        bool busy = (d && lv_display_get_inactive_time(d) < ATTN_IDLE_MS) || lv_tick_elaps(play.at) < ATTN_IDLE_MS;
        attn = !busy ? 0 : !in_game() || sh.gated ? 1 : CUR->with_pip ? 3 : 2; /* play.at: a tilt or a key in a game too */
    }
    pip_attention(attn, dt);
    if (sh.setup) { /* nothing he feels while the panel is at work: no shake, no tilt, no fall; kept on top of any card */
        if (lv_obj_get_index(sh.setup) != (int32_t)lv_obj_get_child_count(lv_layer_top()) - 1) lv_obj_move_foreground(sh.setup);
        return;
    }
    if (!pip_display_on() && lv_screen_active() != sh.pip_scr && !awake && !phone_where()) shell_home(); /* asleep: back to his face */
    pip_shake_ok(sh.open < 0 || !CUR->imu); /* shaking a die is not shaking him */
    pip_imu(ax, ay, az, dt);
    if (sh.open >= 0 && CUR->imu) CUR->imu(ax, ay, az, dt);
}

void shell_create(void)
{
    LV_ASSERT_MSG(app_total <= MAX_APPS, "more apps than the list has rows: raise MAX_APPS");
    sh.pip_scr = lv_screen_active();
    pip_create(sh.pip_scr);
    for (lv_indev_t *in = lv_indev_get_next(NULL); in; in = lv_indev_get_next(in)) /* the back gesture, on every touch */
        if (lv_indev_get_type(in) == LV_INDEV_TYPE_POINTER) lv_indev_add_event_cb(in, edge_input, LV_EVENT_ALL, NULL);
    edge_hint = lv_obj_create(lv_layer_top()); /* its sign: 4 x 44 px, 8 px in from the edge, a third opaque */
    lv_obj_remove_style_all(edge_hint);
    lv_obj_set_size(edge_hint, 4, 44);
    lv_obj_set_pos(edge_hint, SCR - 12, SCR / 2 - 22);
    lv_obj_set_style_bg_opa(edge_hint, LV_OPA_30, 0);
    lv_obj_remove_flag(edge_hint, LV_OBJ_FLAG_CLICKABLE);
    edge_hint_show(false);

    sh.menu_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(sh.menu_scr, LV_OBJ_FLAG_SCROLLABLE);
    if (!mn.slots) menu_build(shell_menu_text()); /* the default one, unless main.c set the saved one already */
    sh.lo = 0, sh.n = mn.top_n;
    for (int i = 0; i < app_total; i++) {
        sh.rows[i] = lv_image_create(sh.menu_scr); /* a tinted alpha mask, see rows_build */
        lv_obj_set_style_image_recolor_opa(sh.rows[i], LV_OPA_COVER, 0);
    }
    sh.lens = lv_obj_create(sh.menu_scr); /* clips its own copy of the rows: whatever passes through reads big */
    lv_obj_remove_style_all(sh.lens);
    lv_obj_set_size(sh.lens, LENS_W, LENS_H);
    lv_obj_set_pos(sh.lens, LENS_X, (SCR - LENS_H) / 2);
    lv_obj_remove_flag(sh.lens, LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < app_total; i++) {
        sh.lens_rows[i] = lv_image_create(sh.lens);
        lv_obj_set_style_image_recolor_opa(sh.lens_rows[i], LV_OPA_COVER, 0);
    }
    sh.lens_cursor = th_cursor(sh.lens, 12, 28, 0);
    sh.chrome = lv_obj_create(sh.menu_scr);
    lv_obj_remove_style_all(sh.chrome);
    lv_obj_set_size(sh.chrome, SCR, SCR);
    lv_obj_remove_flag(sh.chrome, LV_OBJ_FLAG_CLICKABLE);
    sh.marker = lv_obj_create(sh.menu_scr); /* the dial's pointer on the left */
    lv_obj_remove_style_all(sh.marker);
    lv_obj_set_size(sh.marker, 14, 14);
    lv_obj_set_style_radius(sh.marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(sh.marker, LV_OPA_COVER, 0);
    lv_obj_align(sh.marker, LV_ALIGN_LEFT_MID, 62, 0);
    lv_obj_add_flag(sh.menu_scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(sh.menu_scr, menu_input, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(sh.menu_scr, menu_input, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(sh.menu_scr, menu_input, LV_EVENT_RELEASED, NULL);
    restyle_menu();
    apps_init();
}
