/* The iPhone apps and overlays, laid out after the "Ретро" canvas boards (iPhone row).
   The model is filled by the host through phone.h; commands go back through phone_on_command(). */
#include "phone.h"
#include "pip.h"
#include "shell.h"
#include "theme.h"
#include "voice.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#define SCR 466
#define MAX_NOTES 10 /* the newest ten: older ones fall off the end */
#define ROW_H 87 /* notification list: px between rows */

typedef struct {
    uint32_t uid;
    char app[24], title[48], text[480]; /* what ANCS sends at most */
    uint8_t hour, min;
    bool unread, secret; /* secret: a code, a password or a bank: only the app is kept */
} note_t;

static struct {
    int link;
    uint32_t code;
    note_t *notes; /* newest first; on the heap: long texts, and the board puts big blocks in PSRAM */
    int n;
    struct { bool on; uint32_t uid; char name[48], kind[24]; } call;
    struct { bool has, playing; char title[64], artist[48]; int dur; float pos; uint32_t at; } tr;
    float vol, vol_phone; /* vol: shown; vol_phone: what the phone last said */
    void (*cb)(int cmd, uint32_t uid);
    /* overlays on the top layer */
    lv_obj_t *banner, *call_o, *pair_o, *call_grid, *call_arc;
    lv_timer_t *banner_t, *call_t;
    th_cell_t call_cells[60];
    uint32_t banner_uid, open_uid;
    int call_phase;
    const char *app; /* the phone app on screen, NULL if none */
    int kind;        /* PHONE_IOS / PHONE_ANDROID: the one linked last */
} ph = {.vol = 0.5f};

#define ANDROID (ph.link == PHONE_ON && ph.kind == PHONE_ANDROID) /* linked, and nothing but the shutter works */
void phone_kind(int kind) { ph.kind = kind == PHONE_ANDROID ? PHONE_ANDROID : PHONE_IOS; }

static void send(int cmd, uint32_t uid)
{
    if (ph.cb) ph.cb(cmd, uid);
}

void phone_on_command(void (*cb)(int, uint32_t)) { ph.cb = cb; }

const char *phone_command_name(int cmd)
{
    static const char *const names[PHONE_CMD_COUNT] = {"note_dismiss", "call_answer", "call_decline", "play_pause", "next",
                                                       "prev", "vol_up", "vol_down", "shutter", "pair_ok", "pair_cancel"};
    return cmd >= 0 && cmd < PHONE_CMD_COUNT ? names[cmd] : "";
}

/* drop what the theme's fonts cannot draw (emoji, their joiners and variation selectors, rare symbols):
   a missing glyph would show as a gap or a box */
static void drawable_only(char *s)
{
    const lv_font_t *font = th->small;
    char *out = s;
    for (const unsigned char *p = (const unsigned char *)s; *p;) {
        uint32_t cp = *p;
        int n = cp < 0x80 ? 1 : cp >= 0xF0 ? 4 : cp >= 0xE0 ? 3 : cp >= 0xC0 ? 2 : 1;
        if (n > 1) {
            cp &= 0xFF >> (n + 1);
            int k = 1;
            for (; k < n && p[k]; k++) cp = cp << 6 | (p[k] & 0x3F);
            if (k < n) break; /* cut short by the end of the string: drop it, never step past the terminator */
        }
        lv_font_glyph_dsc_t g;
        bool keep = cp == '\n' || (cp != 0x200D && !(cp >= 0xFE00 && cp <= 0xFE0F) && lv_font_get_glyph_dsc(font, &g, cp, 0));
        if (cp == ' ' && (out == s || out[-1] == ' ')) keep = false; /* the gap a dropped emoji leaves */
        if (keep) memmove(out, p, (size_t)n), out += n;
        p += n;
    }
    while (out > s && out[-1] == ' ') out--;
    *out = 0;
}

/* copy UTF-8 without cutting a character in half */
static void copy(char *d, size_t n, const char *s)
{
    size_t k = 0;
    while (s && s[k] && k + 1 < n) k++;
    while (k > 0 && s[k] && ((unsigned char)s[k] & 0xC0) == 0x80) k--; /* stopped inside a character: drop it */
    memcpy(d, s ? s : "", k);
    d[k] = 0;
}


static lv_obj_t *layer(void) /* a full-screen overlay that swallows touches */
{
    lv_obj_t *o = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, SCR, SCR);
    th_screen(o);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *ring_arc(lv_obj_t *parent, int d, int w) /* the cell rings' counterpart: a faint track, an accent run */
{
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_set_size(a, d, d);
    lv_obj_align(a, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_bg_angles(a, 0, 360);
    lv_arc_set_rotation(a, 270);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(a, 0, LV_PART_KNOB);
    lv_obj_set_style_arc_width(a, w, 0);
    lv_obj_set_style_arc_color(a, lv_color_hex(th->faint), 0);
    lv_obj_set_style_arc_width(a, w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, lv_color_hex(th->accent), LV_PART_INDICATOR);
    return a;
}

static bool relinking; /* the open phone app is being built again because the link came or went (the locator: lost) */
static void reopen_app(void) /* the link came or went: build the open phone app again */
{
    if (ph.app) relinking = true, shell_open(ph.app), relinking = false;
}

/* ================= pairing ================= */

static void pair_hide(void)
{
    if (ph.pair_o) lv_obj_delete(ph.pair_o), ph.pair_o = NULL;
}

static void pair_answer(bool ok)
{
    pair_hide();
    send(ok ? PHONE_PAIR_OK : PHONE_PAIR_CANCEL, 0);
    if (ok) pip_phone(PIP_PAIRED); /* a nod */
}

static void pair_ok_cb(lv_event_t *e) { (void)e; pair_answer(true); }

static void pair_show(void)
{
    static const char *const rune[] = {"0001000", "0001100", "1001010", "0101001", "0011010", "0001100",
                                       "0011010", "0101001", "1001010", "0001100", "0001000"};
    static th_cell_t cells[40];
    pair_hide();
    lv_obj_t *o = ph.pair_o = layer();
    th_header(o, "пип-01 // связь", 40);
    if (TH_TERMINAL) th_cells(o, cells, th_pattern(cells, rune, 11, 209, 86, 7, 6, th->text));
    else { /* the Bluetooth rune as one line */
        static const lv_point_precise_t pts[] = {{0, 18}, {34, 48}, {17, 64}, {17, 0}, {34, 16}, {0, 46}};
        lv_obj_t *ln = lv_line_create(o);
        lv_line_set_points(ln, pts, 6);
        lv_obj_set_style_line_width(ln, 4, 0);
        lv_obj_set_style_line_color(ln, lv_color_hex(th->text), 0);
        lv_obj_set_style_line_rounded(ln, true, 0);
        lv_obj_set_pos(ln, SCR / 2 - 17, 92);
    }
    th_text_at(o, th->tiny, th->dim, "сопряжение с телефоном", -1, 177);
    char code[16];
    snprintf(code, sizeof code, "%03u %03u", (unsigned)(ph.code / 1000 % 1000), (unsigned)(ph.code % 1000));
    th_plate(o, 118, 204, 230, 52, th->body, code, false, NULL);
    th_text_at(o, th->tiny, th_mute(), "сверьте код на телефоне", -1, 273);
    th_button_at(o, 128, 306, 210, "подтвердить", true, pair_ok_cb);
}

void phone_link(int state, uint32_t code)
{
    int was = ph.link;
    ph.link = state, ph.code = code;
    if (state == PHONE_PAIRING) pair_show();
    else pair_hide();
    if (state != PHONE_ON) { /* whatever the phone told us is stale now */
        ph.n = 0, ph.tr.has = false;
        if (ph.call.on) phone_call(0, NULL, NULL);
    }
    if (was != state) reopen_app();
}

/* ================= the banner over whatever is on screen ================= */

static lv_obj_t *banner_leaving; /* the last one, sliding up and away */

static void banner_hide(void) /* at once: put away, or replaced by a newer one */
{
    if (ph.banner_t) lv_timer_delete(ph.banner_t), ph.banner_t = NULL;
    if (banner_leaving) lv_obj_delete(banner_leaving), banner_leaving = NULL;
    if (ph.banner) lv_obj_delete(ph.banner), ph.banner = NULL, pip_glance(0, 0, 0);
}

static void banner_open(void)
{
    ph.open_uid = ph.banner_uid;
    banner_hide();
    shell_open("notify");
}

static void banner_tap(lv_event_t *e) { (void)e; banner_open(); }
static void y_exec(void *o, int32_t v) { lv_obj_set_y(o, v); }
static void banner_gone(lv_anim_t *a) { if (a->var == banner_leaving) banner_leaving = NULL; lv_obj_delete(a->var); }

/* its time is up: it slides up off the screen (it is no longer "the banner": the buttons go back to the screen) */
static void banner_timeout(lv_timer_t *t)
{
    (void)t;
    ph.banner_t = NULL;
    if (!ph.banner) return;
    if (banner_leaving) lv_obj_delete(banner_leaving);
    lv_obj_t *b = banner_leaving = ph.banner;
    ph.banner = NULL, pip_glance(0, 0, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, b);
    lv_anim_set_exec_cb(&a, y_exec);
    lv_anim_set_values(&a, lv_obj_get_y(b), -lv_obj_get_height(b) - 4);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, banner_gone);
    lv_anim_start(&a);
}

/* ---------- privacy ---------- */
static int privacy = PHONE_PRIV_HIDE;
static void (*privacy_cb)(int);
void phone_on_privacy(void (*cb)(int)) { privacy_cb = cb; }
/* calls switched off (Приватность, the panel's "calls 0"): the phone still rings, the keychain never shows it */
static bool calls_on = true;
static void (*calls_cb)(bool);
void phone_on_calls(void (*cb)(bool)) { calls_cb = cb; }
static void call_hide(void);
void phone_set_calls(bool on)
{
    calls_on = on;
    if (!on && ph.call_o) voice_hush(), call_hide(); /* one ringing now goes too */
}
bool phone_calls(void) { return calls_on; }
int phone_privacy(void) { return privacy; }

static void lower(char *d, size_t n, const char *s) /* UTF-8 lower case for Latin and Russian, to search in */
{
    size_t k = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p && k + 2 < n;) {
        if (*p >= 'A' && *p <= 'Z') d[k++] = (char)(*p++ + 32);
        else if (p[0] == 0xD0 && p[1] >= 0x90 && p[1] <= 0x9F) d[k++] = (char)0xD0, d[k++] = (char)(p[1] + 0x20), p += 2;
        else if (p[0] == 0xD0 && p[1] >= 0xA0 && p[1] <= 0xAF) d[k++] = (char)0xD1, d[k++] = (char)(p[1] - 0x20), p += 2;
        else if (p[0] == 0xD0 && p[1] == 0x81) d[k++] = (char)0xD1, d[k++] = (char)0x91, p += 2;
        else d[k++] = (char)*p++;
    }
    d[k] = 0;
}

static bool letter(char c) { unsigned char u = (unsigned char)c; return isalnum(u) || u >= 0x80; } /* UTF-8 bytes: letters */
/* w at the start of a word (stems: "парол" finds "пароль"), short Latin ones a whole word (pin, not shipping); a sign
   such as the tenge's finds itself anywhere */
static bool word_in(const char *low, const char *w)
{
    size_t n = strlen(w);
    bool sign = (unsigned char)w[0] >= 0xE0, whole = n <= 5 && (unsigned char)w[0] < 0x80;
    for (const char *p = strstr(low, w); p; p = strstr(p + 1, w))
        if ((sign || p == low || !letter(p[-1])) && (!whole || !letter(p[n]))) return true;
    return false;
}

static bool sensitive(const char *app, const char *title, const char *text)
{
    static const char *const words[] = {
        "код", "code", "парол", "password", "passcode", "pin", "otp", "2fa", "одноразов", "подтвержд", "verif",
        "құпиясөз", "банк", "bank", "kaspi", "каспи", "halyk", "халык", "jusan", "freedom", "forte", "форте", "bcc",
        "центркредит", "bereke", "береке", "eurasian", "евразийск", "home credit", "хоум кредит", "egov", "revolut",
        "paypal", "wise", "списан", "зачислен", "перевод", "баланс", "покупка", "оплата", "₸", "тенге", "kzt",
    };
    char all[600], low[600];
    snprintf(all, sizeof all, "%s\n%s\n%s", app, title, text);
    lower(low, sizeof low, all);
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++)
        if (word_in(low, words[i])) return true;
    return false;
}

/* the sender and the line under it, as a glance may show them */
static const char *glance_title(const note_t *n) { return privacy == PHONE_PRIV_OPEN && !n->secret ? n->title : "новое уведомление"; }
static const char *glance_text(const note_t *n) { return privacy == PHONE_PRIV_OPEN && !n->secret ? n->text : ""; }

#define BANNER_W 290
#define BANNER_Y 60 /* low enough that the round glass leaves its corners alone */
#define BANNER_PAD 10
#define BANNER_MS 2000 /* how long it stays before it slides away */

static lv_obj_t *banner_line(lv_obj_t *b, const lv_font_t *f, uint32_t color, const char *text, int x, int y, int w)
{
    lv_obj_t *l = th_text_at(b, f, color, text, x, y);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); /* one line each, cut with "...", never wrapped over the next */
    lv_obj_set_size(l, w, lv_font_get_line_height(f));
    return l;
}

/* A card at the top: the app and "now", the sender, the first line when privacy lets it show. Square in every theme
   but Барби; exactly as tall as what it holds. It slides down, stays BANNER_MS and slides back up; a newer one
   replaces it at once (no two on screen). */
static void banner_show(const note_t *n)
{
    banner_hide();
    ph.banner_uid = n->uid;
    const lv_font_t *f_app = th->tiny_bold ? th->tiny_bold : th->small, *f_title = th->button ? th->button : th->small;
    const lv_font_t *f_text = th->tiny ? th->tiny : th->small;
    const char *text = glance_text(n);
    int inner = BANNER_W - 2 * BANNER_PAD, h_app = lv_font_get_line_height(f_app), h_title = lv_font_get_line_height(f_title);
    int h = BANNER_PAD + h_app + 2 + h_title + (*text ? 2 + lv_font_get_line_height(f_text) : 0) + BANNER_PAD;
    bool round = theme_index() == TH_BARBIE;
    lv_obj_t *b = ph.banner = th_box(lv_layer_top(), (SCR - BANNER_W) / 2, -h, BANNER_W, h, round ? 16 : 0, TH_TERMINAL ? th->bg : th->panel);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(TH_TERMINAL ? th->text : th->faint), 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, banner_tap, LV_EVENT_CLICKED, NULL);
    th_box(b, BANNER_PAD - 2, BANNER_PAD + h_app / 2 - 4, 8, 8, round ? LV_RADIUS_CIRCLE : 0, TH_TERMINAL ? th->text : th->accent); /* unread */
    char t[64];
    snprintf(t, sizeof t, "%s // сейчас", n->app);
    int y = BANNER_PAD - 2; /* the border takes 2 px */
    banner_line(b, f_app, th->dim, t, BANNER_PAD + 12, y, inner - 12);
    y += h_app + 2;
    banner_line(b, f_title, th->text, glance_title(n), BANNER_PAD - 2, y, inner);
    if (*text) banner_line(b, f_text, th->dim, text, BANNER_PAD - 2, y + h_title + 2, inner);
    lv_anim_t a; /* slides down from above the screen */
    lv_anim_init(&a);
    lv_anim_set_var(&a, b);
    lv_anim_set_exec_cb(&a, y_exec);
    lv_anim_set_values(&a, -h, BANNER_Y);
    lv_anim_set_duration(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
    ph.banner_t = lv_timer_create(banner_timeout, BANNER_MS, NULL);
    lv_timer_set_repeat_count(ph.banner_t, 1);
    pip_glance(SCR / 2, 330, BANNER_MS / 1000.0f + 0.3f); /* Pip lowers his eyes and makes room */
    if (!pip_sleeping_face_down() && !shell_in_setup()) voice_say(VOICE_HM);
}

/* ================= incoming call ================= */

static void call_hide(void)
{
    if (ph.call_t) lv_timer_delete(ph.call_t), ph.call_t = NULL;
    if (ph.call_o) lv_obj_delete(ph.call_o), ph.call_o = NULL;
}

static void call_answer(bool yes) /* the top button answers, the bottom one declines; the ringing stops at once */
{
    voice_hush();
    ph.call.on = false; /* first: the phone ends the call while we tell it, and that is no missed call */
    send(yes ? PHONE_CALL_ANSWER : PHONE_CALL_DECLINE, ph.call.uid);
    call_hide();
    pip_phone(yes ? PIP_CALL_ANSWERED : PIP_CALL_DECLINED); /* he nods it through, or shakes his head */
}

static void call_yes(lv_event_t *e) { (void)e; call_answer(true); }
static void call_no(lv_event_t *e) { (void)e; call_answer(false); }

static void call_spin(lv_timer_t *t) /* a light runs round the ring while it rings */
{
    (void)t;
    ph.call_phase++;
    if (ph.call_phase % 65 == 0 && !pip_sleeping_face_down() && !shell_in_setup()) voice_say(VOICE_RING), pip_wake(); /* the ringtone again, about every 2.6 s; lit while it rings */
    if (ph.call_arc) { lv_arc_set_angles(ph.call_arc, ph.call_phase * 6 % 360, ph.call_phase * 6 % 360 + 48); return; }
    for (int i = 0; i < 60; i++) {
        int k = ((i - ph.call_phase) % 4 + 4) % 4;
        th_cell_set(ph.call_grid, &ph.call_cells[i], k == 0 ? th->text : k == 1 ? th->dim : th_off());
    }
}

static void call_show(void)
{
    pip_wake();
    call_hide();
    banner_hide();
    lv_obj_t *o = ph.call_o = layer();
    ph.call_arc = NULL;
    if (TH_TERMINAL) th_ring(ph.call_cells, 60, 210, 8), ph.call_grid = th_cells(o, ph.call_cells, 60);
    else lv_obj_set_y(ph.call_arc = ring_arc(o, 432, 6), 17);
    th_header(o, "входящий вызов", 92);
    th_text_at(o, th->body, th->text, ph.call.name, -1, 160);
    lv_obj_set_style_text_letter_space(th_text_at(o, th->tiny, th->dim, ph.call.kind, -1, 203), 2, 0);
    th_button_at(o, 153, 252, 160, "ответить", true, call_yes);
    th_button_at(o, 153, 306, 160, "отклонить", false, call_no);
    ph.call_t = lv_timer_create(call_spin, 40, NULL);
    ph.call_phase = 0;
    if (!pip_sleeping_face_down() && !shell_in_setup()) voice_say(VOICE_RING); /* the setup screen or asleep face down: it rings on the phone, not on him */
}

void phone_call(uint32_t uid, const char *name, const char *kind)
{
    bool rang = ph.call.on;
    ph.call.on = name != NULL && !pip_dead() && calls_on; /* a call not shown here is not missed here either */
    if (!name) {
        if (ph.call_o) voice_hush(); /* it stopped ringing: so does the ringtone */
        call_hide();
        if (rang && ph.link == PHONE_ON) pip_phone(PIP_CALL_MISSED); /* it stopped ringing before anyone answered here */
        return;
    }
    if (pip_dead() || !calls_on) return; /* dead, or calls switched off: it rings on the phone only */
    ph.call.uid = uid;
    copy(ph.call.name, sizeof ph.call.name, name);
    drawable_only(ph.call.name);
    copy(ph.call.kind, sizeof ph.call.kind, kind ? kind : "");
    call_show();
}

/* ================= the side buttons and where we are ================= */

bool phone_button(int id)
{
    bool top = id == PIP_BTN_PWR;
    if (ph.pair_o) pair_answer(top);
    else if (ph.call_o) call_answer(top);
    else if (ph.banner) top ? banner_open() : banner_hide();
    else return false;
    return true;
}

const char *phone_where(void) { return ph.pair_o ? "pair" : ph.call_o ? "call" : ph.banner ? "banner" : NULL; }

/* ================= no phone yet: what the apps show ================= */

static void offline(lv_obj_t *scr, const char *name) /* the same words in every theme, no pictures */
{
    th_header(scr, name, 40);
    bool pairing = ph.link == PHONE_PAIRING;
    th_text_at(scr, th->body, th->text, pairing ? "Сверьте код" : "Нет связи", -1, 176);
    lv_obj_t *l = th_text_at(scr, th->small, th->dim, pairing ? "на телефоне и подтвердите\nверхней кнопкой" : "Нужно Bluetooth подключение", -1, 226);
    lv_obj_set_style_text_line_space(l, 6, 0);
}

/* a word in the middle and a line under it: "пусто", "выключены", "не поддерживается" */
static void app_says(lv_obj_t *scr, const char *word, const char *why)
{
    th_text_at(scr, th->body, th->dim, word, -1, 196);
    lv_obj_set_style_text_letter_space(th_text_at(scr, th->tiny, th_mute(), why, -1, 240), 2, 0);
}
#define NOT_ON_ANDROID(scr) app_says(scr, "не поддерживается", "на android")

/* ================= notifications: a wheel of two-line rows, then the whole text ================= */

static struct {
    lv_obj_t *scr, *rows[MAX_NOTES], *grid;
    th_cell_t cells[12];
    float pos, drag_from, vy;
    int focus, tick;
    bool detail;
    uint32_t detail_uid;
    lv_point_t press, last;
    uint32_t last_tick;
} nt;

static int note_find(uint32_t uid)
{
    for (int i = 0; i < ph.n; i++)
        if (ph.notes[i].uid == uid) return i;
    return -1;
}

static void nt_layout(void)
{
    int mid = (int)fmaxf(0, fminf(ph.n - 1, roundf(nt.pos)));
    if (mid != nt.tick) { nt.tick = mid; voice_say(VOICE_TICK); }
    nt.focus = mid;
    for (int i = 0; i < ph.n; i++) {
        float d = i - nt.pos;
        lv_obj_set_flag(nt.rows[i], LV_OBJ_FLAG_HIDDEN, fabsf(d) > 1.6f);
        lv_obj_set_y(nt.rows[i], (int32_t)lroundf(SCR / 2 - 32 + d * ROW_H));
        /* the row in the middle wears the plate */
        bool f = i == mid && fabsf(d) < 0.5f;
        lv_obj_t *plate = lv_obj_get_child(nt.rows[i], 0), *top = lv_obj_get_child(nt.rows[i], 2), *bottom = lv_obj_get_child(nt.rows[i], 3);
        lv_obj_set_style_bg_opa(plate, f ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        if (TH_TERMINAL) lv_obj_set_style_bg_color(lv_obj_get_child(nt.rows[i], 1), lv_color_hex(f ? th->bg : th->text), 0); /* on the ink plate */
        lv_obj_set_style_text_color(top, lv_color_hex(f ? (TH_TERMINAL ? th->bg : th->dim) : th_mute()), 0);
        lv_obj_set_style_text_color(bottom, lv_color_hex(f ? (TH_TERMINAL ? th->bg : th->text) : th->dim), 0);
        lv_obj_set_style_text_font(top, f ? th->tiny_bold : th->tiny, 0);
        lv_obj_set_style_text_font(bottom, f && th->button ? th->button : th->small, 0);
    }
    for (int i = 0; i < 12 && i < ph.n && nt.grid; i++) th_cell_set(nt.grid, &nt.cells[i], i == mid ? th->text : th_off());
}

static void nt_exec(void *v, int32_t x) { (void)v; nt.pos = x / 1000.0f; nt_layout(); }

static void nt_snap(float target, uint32_t ms)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &nt);
    lv_anim_set_exec_cb(&a, nt_exec);
    lv_anim_set_values(&a, (int32_t)(nt.pos * 1000), (int32_t)(target * 1000));
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void nt_build(void);

static void nt_read(lv_event_t *e) /* "прочитано": gone here and on the phone */
{
    (void)e;
    int i = note_find(nt.detail_uid);
    send(PHONE_NOTE_DISMISS, nt.detail_uid);
    if (i >= 0) phone_note_gone(nt.detail_uid); /* rebuilds the list */
    else nt.detail = false, nt_build();
}

/* ================= Настройки > Приватность: a row each, as in Темы; a tap on one puts it on ================= */
static struct { lv_obj_t *marks[PHONE_PRIV_COUNT], *about, *calls; } pv;

#define PV_ROW_Y 104  /* the three rows of what a notification shows */
#define PV_ROW_H 54
#define PV_PITCH 62
#define PV_CALLS_Y 326 /* the calls row, under the line about the chosen one */
static void pv_show(void)
{
    static const char *const about[PHONE_PRIV_COUNT] = {"видно, от кого и что пишут", "видно только, что пришло", "уведомления не приходят"};
    for (int i = 0; i < PHONE_PRIV_COUNT; i++) {
        bool on = i == privacy;
        lv_label_set_text(pv.marks[i], TH_TERMINAL ? (on ? "[X]" : "[ ]") : th_label(on ? "вкл" : ""));
        lv_obj_set_style_text_color(pv.marks[i], lv_color_hex(on ? (TH_TERMINAL ? th->text : th->accent) : th->dim), 0);
    }
    lv_label_set_text(pv.about, th_label(about[privacy]));
    lv_label_set_text(pv.calls, TH_TERMINAL ? (calls_on ? "[X]" : "[ ]") : th_label(calls_on ? "вкл" : "выкл"));
    lv_obj_set_style_text_color(pv.calls, lv_color_hex(calls_on ? (TH_TERMINAL ? th->text : th->accent) : th->dim), 0);
}

static void pv_calls_tap(lv_event_t *e)
{
    (void)e;
    phone_set_calls(!calls_on);
    if (calls_cb) calls_cb(calls_on); /* the board keeps it, as the panel's "calls 0|1" */
    pv_show();
    voice_say(VOICE_TICK);
}

static void pv_tap(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i == privacy) return;
    phone_set_privacy(i);
    if (privacy_cb) privacy_cb(privacy); /* the board keeps it, as the panel's "privacy i" */
    pv_show();
    voice_say(VOICE_TICK);
}


void phone_privacy_open(lv_obj_t *scr)
{
    static const char *const names[PHONE_PRIV_COUNT] = {"Открыто", "Скрыто", "Выключено"};
    th_header(scr, shell_title("privacy"), 40);

    const int radius = th->radius ? 16 : 0;
    for (int i = 0; i < PHONE_PRIV_COUNT; i++) {
        lv_obj_t *r = th_box(scr, 73, PV_ROW_Y + i * PV_PITCH, 320, PV_ROW_H, radius, th->panel);
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(r, pv_tap, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_align(th_text(r, th->small, th->text, names[i]), LV_ALIGN_LEFT_MID, 20, 0);
        pv.marks[i] = th_text(r, th->small, th->accent, "");
        lv_obj_align(pv.marks[i], LV_ALIGN_RIGHT_MID, -20, 0);
    }
    pv.about = th_text_at(scr, TH_TERMINAL ? th->tiny : th->small, th->dim, "", -1, PV_ROW_Y + 3 * PV_PITCH); /* what the one that is on means */
    /* calls: a row of their own under that, a little apart - on or off, a tap switches */
    lv_obj_t *r = th_box(scr, 73, PV_CALLS_Y, 320, PV_ROW_H, radius, th->panel);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r, pv_calls_tap, LV_EVENT_CLICKED, NULL);
    lv_obj_align(th_text(r, th->small, th->text, "Звонки"), LV_ALIGN_LEFT_MID, 20, 0);
    pv.calls = th_text(r, th->small, th->accent, "");
    lv_obj_align(pv.calls, LV_ALIGN_RIGHT_MID, -20, 0);
    pv_show();
}

static void nt_build(void)
{
    lv_obj_t *scr = nt.scr;
    lv_obj_clean(scr);
    lv_anim_delete(&nt, nt_exec);
    if (ph.link != PHONE_ON) { offline(scr, shell_title("notify")); return; } /* no phone comes first: then Android, then switched off */
    int di = nt.detail ? note_find(nt.detail_uid) : -1;
    if (di >= 0) {
        note_t *n = &ph.notes[di];
        n->unread = false;
        char t[64];
        snprintf(t, sizeof t, "%s // %02d:%02d", n->app, n->hour, n->min);
        th_header(scr, t, 40);
        lv_obj_t *l = th_text_at(scr, th->body, th->text, n->secret ? "скрыто" : n->title, 83, 86); /* one column of 300 px, centred */
        lv_obj_set_size(l, 300, 152 - 86 - 4), lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); /* as many lines as fit over the text */
        /* the whole text in a window that scrolls with the finger; a thin bar shows where you are */
        lv_obj_t *win = lv_obj_create(scr);
        lv_obj_remove_style_all(win);
        lv_obj_set_pos(win, 83, 152);
        lv_obj_set_size(win, 310, 4 * (lv_font_get_line_height(th->small) + 6) - 6 + 2); /* four whole lines */
        lv_obj_set_scroll_dir(win, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(win, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_style_width(win, 3, LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_opa(win, LV_OPA_COVER, LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_color(win, lv_color_hex(th->dim), LV_PART_SCROLLBAR);
        lv_obj_set_style_radius(win, th->radius ? 2 : 0, LV_PART_SCROLLBAR);
        lv_obj_set_style_pad_right(win, 8, 0);
        l = th_text_at(win, th->small, n->secret ? th->dim : th->text, n->secret ? "код, пароль или банк: смотри на телефоне" : n->text, 0, 2);
        lv_obj_set_width(l, 292);
        lv_obj_set_style_text_line_space(l, 6, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        th_button_at(scr, 128, 300, 210, "прочитано", true, nt_read);
        return;
    }
    nt.detail = false;
    th_header(scr, shell_title("notify"), 40);
    if (ANDROID) { NOT_ON_ANDROID(scr); return; } /* no ANCS there: nothing would ever come */
    if (privacy == PHONE_PRIV_OFF) { app_says(scr, "отключены", "в настройках"); return; }
    if (!ph.n) { app_says(scr, "пусто", "новых нет"); return; }
    for (int i = 0; i < ph.n; i++) {
        note_t *n = &ph.notes[i];
        lv_obj_t *r = nt.rows[i] = lv_obj_create(scr);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, 313, 64);
        lv_obj_set_x(r, 70);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
        th_box(r, 13, 0, 300, 64, TH_TERMINAL ? 0 : 12, TH_TERMINAL ? th->text : th->panel); /* child 0 */
        th_box(r, 27, 15, 9, 9, TH_TERMINAL ? 0 : LV_RADIUS_CIRCLE, TH_TERMINAL ? th->text : th->accent); /* child 1: unread, inside the plate */
        if (!n->unread) lv_obj_add_flag(lv_obj_get_child(r, 1), LV_OBJ_FLAG_HIDDEN);
        char t[64];
        snprintf(t, sizeof t, "%s // %02d:%02d", n->app, n->hour, n->min);
        lv_obj_t *top = th_text_at(r, th->tiny, th_mute(), t, 45, 6); /* child 2 */
        /* to the plate's right margin (the terminal keeps room for its focus mark): "КАЛЕНДАРЬ // 13:00" lost its time at 222 */
        lv_obj_set_size(top, TH_TERMINAL ? 222 : 252, lv_font_get_line_height(th->tiny_bold)), lv_label_set_long_mode(top, LV_LABEL_LONG_DOT);
        lv_obj_t *bottom = th_text_at(r, th->small, th->dim, glance_title(n), 45, 28); /* child 3 */
        lv_obj_set_size(bottom, 222, lv_font_get_line_height(th->button ? th->button : th->small)), lv_label_set_long_mode(bottom, LV_LABEL_LONG_DOT);
    }
    int count = ph.n < 12 ? ph.n : 12, top = SCR / 2 - (count * 11 - 2) / 2;
    for (int i = 0; i < count; i++) nt.cells[i] = (th_cell_t){405, (int16_t)(top + i * 11), 9, 9, th_off()};
    nt.grid = TH_TERMINAL ? th_cells(scr, nt.cells, count) : NULL; /* the position column is the terminal's */
    nt.pos = (float)(nt.focus < ph.n ? nt.focus : ph.n - 1);
    nt.tick = (int)nt.pos;
    nt_layout();
}

static void nt_open_focused(void)
{
    if (!ph.n) return;
    nt.detail = true;
    nt.detail_uid = ph.notes[nt.focus].uid;
    nt_build();
}

static void nt_input(lv_event_t *e)
{
    if (nt.detail || !ph.n || ph.link != PHONE_ON || ANDROID) return;
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    int dx = p.x - nt.press.x, dy = p.y - nt.press.y;
    if (code == LV_EVENT_PRESSED) {
        nt.press = p, nt.vy = 0;
        lv_anim_delete(&nt, nt_exec);
        nt.drag_from = nt.pos;
    } else if (code == LV_EVENT_PRESSING) {
        uint32_t el = lv_tick_elaps(nt.last_tick);
        if (el > 0) nt.vy = 0.6f * nt.vy + 0.4f * (p.y - nt.last.y) / el;
        nt.pos = fmaxf(-0.3f, fminf(ph.n - 0.7f, nt.drag_from - dy / (float)ROW_H));
        nt_layout();
    } else if (code == LV_EVENT_RELEASED) {
        if (abs(dx) < 12 && abs(dy) < 12) { /* tap: the plate opens, a neighbour comes to the middle */
            int row = (int)roundf(nt.pos + (p.y - SCR / 2) / (float)ROW_H);
            if (row == nt.focus) nt_open_focused();
            else if (row >= 0 && row < ph.n) nt_snap((float)row, 220);
        } else {
            float target = fmaxf(0, fminf(ph.n - 1, roundf(nt.pos - nt.vy * 330 / ROW_H)));
            nt_snap(target, (uint32_t)fminf(1000, 240 + 110 * fabsf(target - nt.pos)));
        }
    }
    nt.last = p, nt.last_tick = lv_tick_get();
}

void phone_notify_open(lv_obj_t *scr)
{
    ph.app = "notify";
    banner_hide(); /* it is all here now */
    nt.scr = scr;
    nt.detail = false;
    nt.focus = 0;
    int i = ph.open_uid ? note_find(ph.open_uid) : -1; /* came from the banner: straight to that one */
    if (i >= 0) nt.detail = true, nt.detail_uid = ph.open_uid, nt.focus = i;
    ph.open_uid = 0;
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, nt_input, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, nt_input, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, nt_input, LV_EVENT_RELEASED, NULL);
    nt_build();
}

void phone_notify_close(void)
{
    lv_anim_delete(&nt, nt_exec);
    ph.app = NULL, nt.scr = NULL;
}

bool phone_notify_button(int id)
{
    if (ph.link != PHONE_ON || ANDROID) return false;
    if (nt.detail) {
        if (id == PIP_BTN_PWR) nt_read(NULL);
        else nt.detail = false, nt_build();
        return true;
    }
    if (id != PIP_BTN_PWR) return false;
    nt_open_focused();
    return true;
}

void phone_set_privacy(int mode)
{
    privacy = mode >= 0 && mode < PHONE_PRIV_COUNT ? mode : PHONE_PRIV_HIDE;
    if (privacy == PHONE_PRIV_OFF) ph.n = 0, banner_hide(); /* off: forget what was here too */
    if (nt.scr) nt.detail = false, nt_build();
}

void phone_note(uint32_t uid, const char *app, const char *title, const char *text, int hour, int min, bool quiet)
{
    if (privacy == PHONE_PRIV_OFF || pip_dead() || ANDROID) return; /* dead: nothing comes to him any more */
    if (!ph.notes && !(ph.notes = calloc(MAX_NOTES, sizeof *ph.notes))) return;
    int i = note_find(uid);
    if (i < 0) { /* newest first; the oldest falls off the end */
        memmove(&ph.notes[1], &ph.notes[0], sizeof ph.notes[0] * (size_t)(ph.n < MAX_NOTES ? ph.n : MAX_NOTES - 1));
        if (ph.n < MAX_NOTES) ph.n++;
        i = 0;
        if (nt.scr) nt.focus = nt.focus + 1 < ph.n ? nt.focus + 1 : nt.focus; /* keep the same one in focus (open in full too: back lands on it) */
        if (!quiet) pip_note();
    }
    note_t *n = &ph.notes[i];
    n->uid = uid, n->hour = (uint8_t)hour, n->min = (uint8_t)min, n->unread = true;
    copy(n->app, sizeof n->app, app);
    n->secret = sensitive(app ? app : "", title ? title : "", text ? text : "");
    copy(n->title, sizeof n->title, n->secret ? "" : title); /* a code or a bank is not even kept */
    copy(n->text, sizeof n->text, n->secret ? "" : text);
    drawable_only(n->app), drawable_only(n->title), drawable_only(n->text);
    if (nt.scr) { if (!nt.detail) nt_build(); }
    else if (!quiet && !ph.call_o && !ph.pair_o) banner_show(n);
}

void phone_note_gone(uint32_t uid)
{
    int i = note_find(uid);
    if (i < 0) return;
    memmove(&ph.notes[i], &ph.notes[i + 1], sizeof ph.notes[0] * (size_t)(ph.n - i - 1));
    ph.n--;
    if (ph.banner && ph.banner_uid == uid) banner_hide();
    if (nt.scr) {
        if (nt.detail && nt.detail_uid == uid) nt.detail = false;
        if (nt.focus >= ph.n) nt.focus = ph.n ? ph.n - 1 : 0;
        nt_build();
    }
}

/* ================= music: AMS now-playing, volume by dragging ================= */
static struct {
    lv_obj_t *scr, *head, *title, *artist, *time, *bar, *vol, *vol_label, *vol_fill;
    lv_timer_t *timer;
    lv_point_t press;
    int drag_y;
    uint32_t vol_until;
    char shown[112];
} mu;

#define BAR_W 262
#define VOL_W 220

static float track_pos(void)
{
    float p = ph.tr.pos + (ph.tr.playing ? lv_tick_elaps(ph.tr.at) / 1000.0f : 0);
    return ph.tr.dur > 0 && p > ph.tr.dur ? (float)ph.tr.dur : p;
}

static void width_exec(void *o, int32_t v) { lv_obj_set_width(o, v); }

static void mu_update(lv_timer_t *t)
{
    (void)t;
    if (!mu.title) return;
    bool has = ph.tr.has, playing = has && ph.tr.playing;
    char s[112];
    snprintf(s, sizeof s, "%d%s%s", playing, has ? ph.tr.title : "", ph.tr.artist);
    if (strcmp(s, mu.shown)) { /* the text only when it changes: a new text restarts the running line */
        strcpy(mu.shown, s);
        char h[64];
        snprintf(h, sizeof h, !(playing || !has) ? "%s // пауза" : "%s", shell_title("music"));
        lv_label_set_text(mu.head, th_title(h));
        lv_label_set_text(mu.title, th_label(has ? ph.tr.title : "ничего не играет"));
        lv_label_set_text(mu.artist, has ? th_label(ph.tr.artist) : "");
        lv_obj_set_style_text_color(mu.title, lv_color_hex(playing || !has ? th->text : th->dim), 0);
    }
    float pos = track_pos();
    lv_obj_set_width(mu.bar, has && ph.tr.dur > 0 ? (int32_t)(pos / ph.tr.dur * BAR_W) : 0);
    lv_obj_set_style_bg_color(mu.bar, lv_color_hex(!playing ? th->dim : TH_TERMINAL ? th->text : th->accent), 0);
    char a[16], b[16];
    if (has) {
        int p = (int)pos, left = ph.tr.dur - p > 0 ? ph.tr.dur - p : 0;
        snprintf(a, sizeof a, "%02d:%02d", p / 60, p % 60);
        snprintf(b, sizeof b, "-%02d:%02d", left / 60, left % 60);
    } else strcpy(a, "--:--"), strcpy(b, "--:--");
    th_leader(mu.time, a, b, BAR_W);
    /* the volume plate while it is being changed; the phone's own word again once the finger is off */
    bool vol = mu.vol_until && (int32_t)(mu.vol_until - lv_tick_get()) > 0;
    if (!vol) ph.vol = ph.vol_phone;
    lv_obj_set_flag(mu.vol, LV_OBJ_FLAG_HIDDEN, !vol);
    if (vol) lv_label_set_text_fmt(mu.vol_label, "%s %d%%", th_label("громкость"), (int)lroundf(ph.vol * 100));
}

static void mu_volume_step(bool up)
{
    send(up ? PHONE_VOL_UP : PHONE_VOL_DOWN, 0);
    ph.vol = fmaxf(0, fminf(1, ph.vol + (up ? 1 : -1) / 16.0f)); /* iOS moves in sixteenths */
    mu.vol_until = lv_tick_get() + 1500;
    voice_say(VOICE_TICK);
    lv_anim_t an; /* the fill glides to the new step */
    lv_anim_init(&an);
    lv_anim_set_var(&an, mu.vol_fill);
    lv_anim_set_exec_cb(&an, width_exec);
    lv_anim_set_values(&an, lv_obj_get_width(mu.vol_fill), (int32_t)lroundf(ph.vol * VOL_W));
    lv_anim_set_duration(&an, 120);
    lv_anim_set_path_cb(&an, lv_anim_path_ease_out);
    lv_anim_start(&an);
    mu_update(NULL);
}

static void mu_input(lv_event_t *e)
{
    if (ph.link != PHONE_ON) return;
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (code == LV_EVENT_PRESSED) {
        mu.press = p, mu.drag_y = p.y;
    } else if (code == LV_EVENT_PRESSING) {
        /* up or down: a volume step every 18 px, like turning a knob */
        if (abs(p.y - mu.press.y) > abs(p.x - mu.press.x))
            while (abs(p.y - mu.drag_y) >= 18) {
                bool up = p.y < mu.drag_y;
                mu.drag_y += up ? -18 : 18;
                if (!(mu.vol_until && (int32_t)(mu.vol_until - lv_tick_get()) > 0)) lv_obj_set_width(mu.vol_fill, (int32_t)lroundf(ph.vol * VOL_W)); /* the plate was down */
                mu_volume_step(up);
            }
    } else if (code == LV_EVENT_RELEASED) {
        int dx = p.x - mu.press.x, dy = p.y - mu.press.y;
        if (abs(dx) > 90 && abs(dy) < abs(dx) / 2) send(dx < 0 ? PHONE_NEXT : PHONE_PREV, 0); /* swipe: the next or last track */
    }
}

void phone_music_open(lv_obj_t *scr)
{
    ph.app = "music";
    mu.scr = scr, mu.title = NULL, mu.vol_fill = NULL, mu.vol_until = 0, mu.shown[0] = 1;
    if (ph.link != PHONE_ON) { offline(scr, shell_title("music")); return; }
    if (ANDROID) { th_header(scr, shell_title("music"), 40); NOT_ON_ANDROID(scr); return; } /* no AMS there */
    mu.head = th_header(scr, "", 40);
    /* the title big; a long one runs */
    const lv_font_t *big = th->title ? th->title : th->body;
    mu.title = th_text(scr, big, th->text, "");
    lv_obj_set_width(mu.title, 420), lv_label_set_long_mode(mu.title, LV_LABEL_LONG_SCROLL_CIRCULAR); /* clipped near the rim */
    lv_obj_align(mu.title, LV_ALIGN_TOP_MID, 0, 128);
    mu.artist = th_text_at(scr, th->small, th->dim, "", -1, 0);
    lv_obj_set_style_text_letter_space(mu.artist, 2, 0);
    lv_obj_set_width(mu.artist, 300), lv_label_set_long_mode(mu.artist, LV_LABEL_LONG_DOT);
    lv_obj_align(mu.artist, LV_ALIGN_TOP_MID, 0, 190);
    int bh = TH_TERMINAL ? 9 : 4;
    th_box(scr, SCR / 2 - BAR_W / 2, 250, BAR_W, bh, th->radius ? bh / 2 : 0, th_off()); /* the track's run */
    mu.bar = th_box(scr, SCR / 2 - BAR_W / 2, 250, 0, bh, th->radius ? bh / 2 : 0, th->accent);
    mu.time = th_text_at(scr, th->tiny, th_mute(), "", SCR / 2 - BAR_W / 2, 266);
    /* the volume plate over the title while it changes */
    uint32_t plate = TH_TERMINAL ? th->text : th->panel, ink = TH_TERMINAL ? th->bg : th->text;
    mu.vol = th_box(scr, SCR / 2 - 130, 108, 260, 92, th->radius ? 18 : 0, plate);
    mu.vol_label = th_text_at(mu.vol, th->button ? th->button : th->small, ink, "", -1, 14);
    th_box(mu.vol, 20, 58, VOL_W, 10, th->radius ? 5 : 0, TH_TERMINAL ? th->dim : th->faint);
    mu.vol_fill = th_box(mu.vol, 20, 58, 0, 10, th->radius ? 5 : 0, TH_TERMINAL ? th->bg : th->accent);
    lv_obj_add_flag(mu.vol, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, mu_input, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, mu_input, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, mu_input, LV_EVENT_RELEASED, NULL);
    mu_update(NULL);
    mu.timer = lv_timer_create(mu_update, 250, NULL);
}

void phone_music_close(void)
{
    if (mu.timer) lv_timer_delete(mu.timer), mu.timer = NULL;
    if (mu.vol_fill) lv_anim_delete(mu.vol_fill, width_exec);
    mu.title = NULL, mu.vol_fill = NULL, ph.app = NULL;
}

bool phone_music_button(int id)
{
    if (ph.link != PHONE_ON || ANDROID || id != PIP_BTN_PWR) return false;
    if (ph.tr.has) { /* show it at once; the phone confirms (or corrects) right after */
        ph.tr.pos = track_pos(), ph.tr.at = lv_tick_get();
        ph.tr.playing = !ph.tr.playing;
    }
    send(PHONE_PLAY_PAUSE, 0);
    mu_update(NULL);
    return true;
}

bool phone_playing(void) { return ph.link == PHONE_ON && ph.tr.has && ph.tr.playing; }

void phone_track(const char *title, const char *artist, int dur_s, float pos_s, bool playing)
{
    ph.tr.has = title != NULL;
    copy(ph.tr.title, sizeof ph.tr.title, title);
    copy(ph.tr.artist, sizeof ph.tr.artist, artist);
    drawable_only(ph.tr.title), drawable_only(ph.tr.artist);
    ph.tr.dur = dur_s, ph.tr.pos = pos_s, ph.tr.playing = playing, ph.tr.at = lv_tick_get();
    mu_update(NULL);
}

void phone_volume(float v) /* while the finger is on it ours wins: the phone's echoes of the last steps come late */
{
    ph.vol_phone = fmaxf(0, fminf(1, v));
    if (!(mu.vol_until && (int32_t)(mu.vol_until - lv_tick_get()) > 0)) ph.vol = ph.vol_phone;
    mu_update(NULL);
}

/* ================= camera: the phone's shutter, now or after a countdown ================= */

enum { CAM_RING = 0, CAM_DISC = 28, CAM_CORNERS = 28 + 97, CAM_N = 28 + 97 + 20 };
enum { CD_ONE = 0, CD_TWO = 35, CD_RING = 105, CD_N = 165 };

static struct {
    lv_obj_t *scr, *ready, *count, *chips[3], *grid, *cgrid, *num, *arc;
    th_cell_t cells[CAM_N], cd[CD_N];
    lv_timer_t *timer;
    int delay, left_ms, total_ms;
    uint32_t start;
} cam = {.delay = 1};

static const int cam_delays[3] = {0, 3, 10};

static void cam_chips(void)
{
    for (int i = 0; i < 3; i++) {
        bool on = i == cam.delay;
        lv_obj_set_style_bg_opa(cam.chips[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(cam.chips[i], lv_color_hex(TH_TERMINAL ? th->text : th->accent), 0);
        lv_obj_set_style_border_width(cam.chips[i], on ? 0 : 2, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(cam.chips[i], 0), lv_color_hex(on ? th->bg : th->dim), 0);
    }
}

static void cam_chip(lv_event_t *e)
{
    cam.delay = (int)(intptr_t)lv_event_get_user_data(e);
    cam_chips();
}

static void cam_shoot(void)
{
    send(PHONE_SHUTTER, 0);
    /* one frame of ink over everything: the "flash" */
    lv_obj_t *f = th_box(cam.scr, 0, 0, SCR, SCR, 0, th->text);
    lv_obj_delete_delayed(f, 80);
    voice_say(VOICE_TICK);
}

static void cam_show_count(bool on)
{
    lv_obj_set_flag(cam.ready, LV_OBJ_FLAG_HIDDEN, on);
    lv_obj_set_flag(cam.count, LV_OBJ_FLAG_HIDDEN, !on);
}

static void cam_tick(lv_timer_t *t)
{
    (void)t;
    cam.left_ms = cam.total_ms - (int)lv_tick_elaps(cam.start); /* by the clock: timer ticks can come late */
    if (cam.left_ms <= 0) {
        lv_timer_delete(cam.timer), cam.timer = NULL;
        cam_show_count(false);
        cam_shoot();
        return;
    }
    int sec = (cam.left_ms + 999) / 1000;
    char d[12];
    snprintf(d, sizeof d, "%d", sec);
    if (!TH_TERMINAL) {
        lv_label_set_text(cam.num, d);
        lv_arc_set_angles(cam.arc, 0, (lv_value_precise_t)(360 * cam.left_ms / cam.total_ms));
        return;
    }
    if (sec >= 10) th_blocks_hide(cam.cgrid, cam.cd + CD_ONE, 35), th_blocks_show(cam.cgrid, cam.cd + CD_TWO, d, th->text);
    else th_blocks_hide(cam.cgrid, cam.cd + CD_TWO, 70), th_blocks_show(cam.cgrid, cam.cd + CD_ONE, d, th->text);
    int lit = cam.left_ms * 60 / cam.total_ms;
    for (int i = 0; i < 60; i++) th_cell_set(cam.cgrid, &cam.cd[CD_RING + i], i < lit ? th->text : th_off());
}

static void cam_go(void)
{
    if (cam.timer) return;
    if (!cam_delays[cam.delay]) { cam_shoot(); return; }
    cam.total_ms = cam.left_ms = cam_delays[cam.delay] * 1000;
    cam.start = lv_tick_get();
    cam_show_count(true);
    cam.timer = lv_timer_create(cam_tick, 50, NULL);
    cam_tick(NULL);
}

static void cam_tap(lv_event_t *e)
{
    (void)e;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (hypotf(p.x - SCR / 2, p.y - 176) < 92) cam_go(); /* the shutter itself */
}

#define CAM_DY 22 /* the shutter, the word and the chips together, centred on the glass */

/* ================= the locator: how strong the phone's link is, to walk toward it ("warmer, colder") ===============
   The host reads the link's RSSI only while this is open (phone_on_rssi: the firmware asks the controller ~5 times a
   second and quickens the link for it, back to its easy pace on close). Smoothed (the signal jumps 5-10 dB with a
   hand, a wall, a body between), shown as a dial, a word for how near and a "warmer / colder" for which way it goes,
   with a radar's ping quicker the nearer it is and a "ding-ding" for every step nearer. No link: it says so - not yet
   linked, being paired, or lost while looking (with the last it heard). Only the linked phone can be found: a phone
   gives nothing else to look for, and cannot be made to ring over Bluetooth. */
#define LOC_MS 200                    /* a reading this often */
#define LOC_TAU 0.8f                  /* s: the smoothing */
#define LOC_LO (-92.0f)               /* the dial's ends, dBm: as this keychain hears a phone (its antenna on the board, in the case:
                                         ~-55 with the phone against it, ~-90 a few rooms away) */
#define LOC_HI (-52.0f)
#define LOC_BANDS 5
static const struct { float from; const char *word; float ping_s; } loc_band[LOC_BANDS] = {
    {-200, "Далеко", 1.6f}, {-82, "Неподалёку", 1.15f}, {-74, "Близко", 0.8f}, {-67, "Рядом", 0.5f}, {-60, "Здесь", 0.28f},
};
#define LOC_HYST 3.0f                 /* dB past a band's line before the word changes (no flicker on the line) */
static struct {
    int (*read)(void);                /* the host: the phone link's RSSI now, dBm (0: none, unknown) */
    void (*active)(bool on);          /* the host: start / stop being asked */
    lv_obj_t *scr, *arc, *word, *how, *dbm;
    lv_timer_t *timer;
    float level, trend, ping_t;       /* smoothed dBm; its slope, dB/s, smoothed too; s to the next ping */
    int band, quiet;                  /* the band shown; readings in a row with nothing */
    bool have;                        /* a reading since it opened */
    bool was_on;                      /* the phone was linked while it was open: a drop is "lost", not "never linked" */
    float last;                       /* the last level heard, for "lost" */
} loc;
void phone_on_rssi(int (*read)(void), void (*active)(bool on)) { loc.read = read, loc.active = active; }

static uint32_t loc_color(int band) { return band >= 3 ? th->accent : band == 2 ? th->text : th->dim; }
static void loc_show(void)
{
    if (!loc.arc) return;
    float k = (loc.level - LOC_LO) / (LOC_HI - LOC_LO);
    lv_arc_set_value(loc.arc, (int32_t)lroundf(100 * fmaxf(0, fminf(1, k))));
    lv_obj_set_style_arc_color(loc.arc, lv_color_hex(loc_color(loc.band)), LV_PART_INDICATOR);
    lv_label_set_text(loc.word, th_label(loc_band[loc.band].word));
    lv_obj_set_style_text_color(loc.word, lv_color_hex(loc_color(loc.band)), 0);
    lv_label_set_text(loc.how, th_label(loc.trend > 1.5f ? "теплее" : loc.trend < -1.5f ? "холоднее" : "ищи, двигая брелок"));
    lv_label_set_text_fmt(loc.dbm, "%d %s", (int)lroundf(loc.level), th_label("дБм"));
}
static void loc_step(lv_timer_t *t)
{
    (void)t;
    if (!loc.arc || !loc.read) return;
    int r = loc.read();
    if (r >= 0 || r < -127) { /* nothing this time (a reading failed, the link busy): a few in a row is worth saying */
        if (++loc.quiet == 8) lv_label_set_text(loc.how, th_label("нет данных о сигнале")), lv_obj_set_style_text_color(loc.word, lv_color_hex(th->dim), 0);
        return;
    }
    loc.quiet = 0;
    float dt = LOC_MS / 1000.0f, a = 1 - expf(-dt / LOC_TAU);
    if (!loc.have) loc.level = (float)r, loc.trend = 0, loc.have = true, loc.band = -1;
    float before = loc.level;
    loc.level += ((float)r - loc.level) * a;
    loc.trend += ((loc.level - before) / dt - loc.trend) * a;
    loc.last = loc.level;
    /* the band: up past its line + LOC_HYST, down below the current one's line - LOC_HYST */
    int b = 0;
    while (b + 1 < LOC_BANDS && loc.level >= loc_band[b + 1].from) b++;
    if (loc.band < 0) loc.band = b;
    else if (b > loc.band && loc.level >= loc_band[loc.band + 1].from + LOC_HYST) {
        loc.band++;
        voice_say(VOICE_POINT); /* a step nearer: "ding-ding" */
        loc.ping_t = 0.6f;      /* and the ping keeps out of its way */
    } else if (b < loc.band && loc.level < loc_band[loc.band].from - LOC_HYST) loc.band--;
    loc_show();
    if ((loc.ping_t -= dt) <= 0) { /* the radar's ping: the nearer, the quicker */
        loc.ping_t = loc_band[loc.band].ping_s;
        voice_say(VOICE_TICK);
    }
}
void phone_locate_close(void)
{
    if (loc.timer) lv_timer_delete(loc.timer), loc.timer = NULL;
    if (loc.active && loc.arc) loc.active(false); /* the link back to its easy pace, no more readings */
    if (!relinking) loc.was_on = false; /* left by hand: next time "lost" means lost then */
    loc.arc = NULL, loc.scr = NULL;
    ph.app = NULL;
}
void phone_locate_open(lv_obj_t *scr)
{
    ph.app = "locate";
    loc.scr = scr, loc.arc = NULL, loc.timer = NULL, loc.have = false, loc.quiet = 0, loc.ping_t = 1.0f;
    if (ph.link != PHONE_ON) {
        if (ph.link == PHONE_OFF && loc.was_on) { /* it was linked a moment ago, here: lost while looking */
            th_header(scr, shell_title("locate"), 40);
            th_text_at(scr, th->body, th->text, th_label("Связь пропала"), -1, 170);
            /* two labels: th_label keeps 92 bytes, ~45 letters of Cyrillic */
            lv_obj_t *l = th_text_at(scr, th->small, th->dim, th_label("телефон далеко\nили Bluetooth выключен"), -1, 216);
            lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0), lv_obj_set_style_text_line_space(l, 6, 0);
            char t[64];
            snprintf(t, sizeof t, "последний сигнал %d дБм", (int)lroundf(loc.last));
            th_text_at(scr, th->tiny, th_mute(), th_label(t), -1, 290);
            return;
        }
        offline(scr, shell_title("locate")); /* never linked, or pairing */
        return;
    }
    loc.was_on = true;
    th_header(scr, shell_title("locate"), 40);
    lv_obj_t *a = loc.arc = lv_arc_create(scr); /* a dial as the noise meter's: three quarters of a turn */
    lv_obj_set_size(a, 300, 300);
    lv_obj_align(a, LV_ALIGN_CENTER, 0, 22);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(a, 135);
    lv_arc_set_bg_angles(a, 0, 270);
    lv_arc_set_range(a, 0, 100);
    lv_arc_set_value(a, 0);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(a, 0, LV_PART_KNOB);
    lv_obj_set_style_arc_width(a, 14, 0);
    lv_obj_set_style_arc_color(a, lv_color_hex(th->faint), 0);
    lv_obj_set_style_arc_width(a, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, th->radius > 0, 0);
    lv_obj_set_style_arc_rounded(a, th->radius > 0, LV_PART_INDICATOR);
    loc.word = th_text(scr, th->body, th->dim, th_label("Ищу"));
    lv_obj_align(loc.word, LV_ALIGN_CENTER, 0, 0);
    loc.how = th_text(scr, th->small, th->dim, th_label("ищи, двигая брелок"));
    lv_obj_align(loc.how, LV_ALIGN_CENTER, 0, 44);
    loc.dbm = th_text(scr, th->tiny, th_mute(), "");
    lv_obj_align(loc.dbm, LV_ALIGN_CENTER, 0, 128);
    if (loc.active) loc.active(true);
    loc.timer = lv_timer_create(loc_step, LOC_MS, NULL);
}
/* the sim's status: "band,dBm" while it shows a reading, "-" otherwise */
const char *phone_locate_status(void)
{
    static char b[24];
    if (!loc.arc || !loc.have) return "-";
    snprintf(b, sizeof b, "%d,%d", loc.band, (int)lroundf(loc.level));
    return b;
}

void phone_camera_open(lv_obj_t *scr)
{
    ph.app = "camera";
    cam.scr = scr, cam.timer = NULL;
    if (ph.link != PHONE_ON) { offline(scr, shell_title("camera")); return; }
    /* ready: a shutter of cells in a viewfinder, the timer chips */
    lv_obj_t *r = cam.ready = th_box(scr, 0, 0, SCR, SCR, 0, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    th_header(r, shell_title("camera"), 40);
    int n = 0;
    if (!TH_TERMINAL) { /* a shutter button: an ink ring round an accent disc */
        lv_obj_t *ring = th_box(r, SCR / 2 - 80, 96 + CAM_DY, 160, 160, LV_RADIUS_CIRCLE, th->bg);
        lv_obj_set_style_border_width(ring, 6, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(th->text), 0);
        th_box(r, SCR / 2 - 62, 114 + CAM_DY, 124, 124, LV_RADIUS_CIRCLE, th->accent);
    } else {
    for (int i = 0; i < 28; i++) {
        float a = i / 28.0f * 6.2831853f;
        cam.cells[n++] = (th_cell_t){(int16_t)lroundf(SCR / 2 + cosf(a) * 80 - 4), (int16_t)lroundf(176 + CAM_DY + sinf(a) * 80 - 4), 8, 8, th->text};
    }
    for (int y = -5; y <= 5; y++)
        for (int x = -5; x <= 5; x++)
            if (hypotf((float)x, (float)y) <= 5.2f) cam.cells[n++] = (th_cell_t){(int16_t)(229 + x * 11), (int16_t)(172 + CAM_DY + y * 11), 9, 9, th->dim};
    static const int corners[4][4] = {{118, 92 + CAM_DY, 1, 1}, {339, 92 + CAM_DY, -1, 1}, {118, 252 + CAM_DY, 1, -1}, {339, 252 + CAM_DY, -1, -1}};
    for (int c = 0; c < 4; c++)
        for (int k = 0; k < 3; k++) {
            cam.cells[n++] = (th_cell_t){(int16_t)(corners[c][0] + corners[c][2] * k * 11), (int16_t)corners[c][1], 9, 9, th_mute()};
            if (k) cam.cells[n++] = (th_cell_t){(int16_t)corners[c][0], (int16_t)(corners[c][1] + corners[c][3] * k * 11), 9, 9, th_mute()};
        }
    cam.grid = th_cells(r, cam.cells, n);
    }
    lv_obj_set_style_text_letter_space(th_text_at(r, th->tiny, th_mute(), "таймер", -1, 273 + CAM_DY), 2, 0);
    lv_obj_t *row = lv_obj_create(r);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 302 + CAM_DY);
    static const char *const names[3] = {"выкл", "3 с", "10 с"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = cam.chips[i] = lv_obj_create(row);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, 78, LV_SIZE_CONTENT); /* one width for all three: the row sits square under the shutter */
        lv_obj_set_style_pad_ver(c, 5, 0);
        lv_obj_set_style_radius(c, th->radius, 0);
        lv_obj_set_style_border_color(c, lv_color_hex(th->faint), 0);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c, cam_chip, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_center(th_text(c, th->tiny_bold, th->dim, names[i]));
    }
    cam_chips();
    /* the countdown: one big digit (or two), the ring running out */
    lv_obj_t *k = cam.count = th_box(scr, 0, 0, SCR, SCR, 0, 0);
    lv_obj_set_style_bg_opa(k, LV_OPA_TRANSP, 0);
    char h[64];
    snprintf(h, sizeof h, "%s // %s", shell_title("camera"), shell_title("timer"));
    th_header(k, h, 92);
    if (TH_TERMINAL) {
        th_blocks(cam.cd + CD_ONE, 189, 136, "0", 18, 16);
        th_blocks(cam.cd + CD_TWO, 135, 136, "00", 18, 16);
        th_ring(cam.cd + CD_RING, 60, 196, 9);
        cam.cgrid = th_cells(k, cam.cd, CD_N);
    } else {
        lv_obj_set_y(cam.arc = ring_arc(k, 404, 8), 31);
        cam.num = th_text(k, th->digits, th->text, "");
        lv_obj_align(cam.num, LV_ALIGN_TOP_MID, 0, 150);
    }
    lv_obj_set_style_text_letter_space(th_text_at(k, th->tiny, th->dim, "сек до снимка", -1, 283), 2, 0);
    cam_show_count(false);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, cam_tap, LV_EVENT_CLICKED, NULL);
}

void phone_camera_close(void)
{
    if (cam.timer) lv_timer_delete(cam.timer), cam.timer = NULL;
    ph.app = NULL;
}

bool phone_camera_button(int id)
{
    if (ph.link != PHONE_ON) return false;
    if (id == PIP_BTN_PWR) { cam_go(); return true; }
    if (!cam.timer) return false;
    lv_timer_delete(cam.timer), cam.timer = NULL; /* bottom during the countdown: cancel */
    cam_show_count(false);
    return true;
}

/* ================= Bluetooth: who is connected and how, a reconnect, forgetting everyone ================= */

#define BT_ROWS 2 /* the phone, the panel: PEER_PHONE, PEER_DEBUG */
static struct {
    void (*query)(phone_bt_t *);
    void (*act)(int);
    lv_obj_t *scr, *rows[BT_ROWS][2], *forget; /* per row: the name, "подключён · 12 мин" */
    lv_timer_t *timer;
    uint32_t forget_at; /* the first tap on "забыть всё": a second within 3 s does it */
} bt;

void phone_on_bt(void (*query)(phone_bt_t *), void (*act)(int)) { bt.query = query, bt.act = act; }

#define BT_TOP 76     /* the rows sit centred between the header and the line over the buttons */
#define BT_BOTTOM 322
#define BT_GAP 16     /* between rows */
#define BT_LEFT 76    /* the column's left edge */

static void bt_update(lv_timer_t *t)
{
    (void)t;
    phone_bt_t s = {0};
    if (bt.query) bt.query(&s);
    const char *who[BT_ROWS] = {"Телефон", "Панель управления"}; /* the panel (or the debug page): the data service */
    int h = 0; /* the block's height, for centring it */
    for (int r = 0; r < BT_ROWS; r++) {
        const phone_peer_t *p = NULL;
        for (int i = 0; i < s.n; i++)
            if (s.peer[i].role == r) p = &s.peer[i];
        if (r == PEER_PHONE && p) who[0] = p->android ? "Android" : "iPhone";
        bool good = p && (r == PEER_DEBUG || (p->encrypted && (r != PEER_PHONE || p->ready)));
        const char *off = r == PEER_DEBUG ? "не подключена" : "не подключён", *on = r == PEER_DEBUG ? "подключена" : "подключён";
        char d[64];
        if (!p) snprintf(d, sizeof d, "%s", off);
        else if (!good) snprintf(d, sizeof d, "подключается");
        else if (p->secs < 60) snprintf(d, sizeof d, "%s · только что", on);
        else if (p->secs < 3600) snprintf(d, sizeof d, "%s · %d мин", on, p->secs / 60);
        else snprintf(d, sizeof d, "%s · %d ч %d мин", on, p->secs / 3600, p->secs / 60 % 60);
        lv_label_set_text(bt.rows[r][0], th_label(who[r]));
        lv_obj_set_style_text_color(bt.rows[r][0], lv_color_hex(th->text), 0); /* a heading, connected or not: the line under it says */
        lv_label_set_text(bt.rows[r][1], th_label(d));
        h += lv_font_get_line_height(th->small) + 2 + lv_font_get_line_height(th->tiny);
    }
    /* stacked from the top of the centred block: every row as tall as its lines, the same gap between them */
    /* the gap shrinks if the fonts are too big for the room, so nothing reaches the buttons */
    int gap = (BT_BOTTOM - BT_TOP - h) / (BT_ROWS - 1) < BT_GAP ? (BT_BOTTOM - BT_TOP - h) / (BT_ROWS - 1) : BT_GAP;
    if (gap < 2) gap = 2;
    int y = BT_TOP + (BT_BOTTOM - BT_TOP - h - (BT_ROWS - 1) * gap) / 2;
    if (y < BT_TOP) y = BT_TOP; /* never under the header */
    for (int r = 0; r < BT_ROWS; r++) {
        lv_obj_set_y(bt.rows[r][0], y), y += lv_font_get_line_height(th->small) + 2;
        lv_obj_set_y(bt.rows[r][1], y), y += lv_font_get_line_height(th->tiny);
        y += gap;
    }
    if (bt.forget_at && lv_tick_elaps(bt.forget_at) > 3000)
        bt.forget_at = 0, lv_label_set_text(lv_obj_get_child(bt.forget, 0), th_label("забыть всё"));
}

static void bt_reconnect(lv_event_t *e) { (void)e; if (bt.act) bt.act(BT_RECONNECT_PHONE); }

static void bt_forget(lv_event_t *e)
{
    (void)e;
    if (!bt.forget_at) { bt.forget_at = lv_tick_get() | 1; lv_label_set_text(lv_obj_get_child(bt.forget, 0), th_label("точно?")); return; }
    bt.forget_at = 0;
    lv_label_set_text(lv_obj_get_child(bt.forget, 0), th_label("забыть всё"));
    if (bt.act) bt.act(BT_FORGET_ALL);
}

void phone_bt_open(lv_obj_t *scr)
{
    bt.scr = scr, bt.forget_at = 0;
    th_header(scr, shell_title("bt"), 40);
    for (int r = 0; r < BT_ROWS; r++) /* one left column, clear of the round rim at the top; bt_update stacks them */
        for (int k = 0; k < 2; k++) {
            lv_obj_t *l = bt.rows[r][k] = th_text_at(scr, k ? th->tiny : th->small, k ? th->dim : th->text, "", BT_LEFT, 0);
            lv_obj_set_width(l, SCR - 2 * BT_LEFT), lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        }
    th_button_at(scr, 52, 330, 206, "переподключить", true, bt_reconnect); /* narrower lower down the round */
    bt.forget = th_button_at(scr, 266, 330, 148, "забыть всё", false, bt_forget);
    bt_update(NULL);
    bt.timer = lv_timer_create(bt_update, 1000, NULL);
}

void phone_bt_close(void)
{
    if (bt.timer) lv_timer_delete(bt.timer), bt.timer = NULL;
}
