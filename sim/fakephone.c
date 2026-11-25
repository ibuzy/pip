#include "fakephone.h"
#include "lvgl.h"
#include "phone.h"
#include "shell.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const struct { const char *app, *title, *text; int h, m; } notes[] = {
    {"Telegram", "Аня", "Встречаемся в 7 у входа? Возьми зарядку, у меня телефон на нуле", 14, 27},
    {"Календарь", "Созвон через 15 мин", "Дизайн-ревью, переговорка 3", 13, 0},
    {"Почта", "Счёт за октябрь", "Во вложении счёт и акт за октябрь", 14, 31},
    {"Telegram", "Чат «Студия» 🎬", "Миша: короче, план на завтра — в 10:00 созвон по проекту №4, потом “финальный” монтаж. "
     "Не забудь скинуть исходники 👍 и обложку, а то опять будем сидеть до ночи. Если что-то не успеваешь — пиши заранее, "
     "перенесём. Всем спасибо за сегодня ✌️", 15, 2},
};
static const struct { const char *title, *artist; int dur; } tracks[] = {
    {"Nightcall", "Kavinsky", 258}, {"Midnight City", "M83", 244}, {"Resonance", "Home", 213}, {"Everything In Its Right Place", "Radiohead", 251},
};

static struct {
    int link, note, track, shots;
    uint32_t uid;
    bool playing, music;
    float pos, vol;
    uint32_t at;
    const char *last;
    bool android;                      /* the fake phone is an Android one: no ANCS, AMS or CTS, only the shutter */
    int rssi;                          /* its link's signal, dBm (pipsim i<dBm>) */
} fp = {.vol = 0.5f, .last = "", .rssi = -52};

/* linked: an iPhone tells its clock (CTS), as on the board; an Android one has nothing to tell (the panel does it) */
static void linked(void)
{
    if (!fp.android) shell_clock_known(true);
}

static float pos_now(void) { return fp.pos + (fp.playing ? lv_tick_elaps(fp.at) / 1000.0f : 0); }

static void send_track(void)
{
    if (!fp.music || fp.link != PHONE_ON || fp.android) { phone_track(NULL, NULL, 0, 0, false); return; }
    phone_track(tracks[fp.track].title, tracks[fp.track].artist, tracks[fp.track].dur, fp.pos, fp.playing);
}

static void on_command(int cmd, uint32_t uid)
{
    fp.last = phone_command_name(cmd);
    int n = sizeof tracks / sizeof tracks[0];
    switch (cmd) {
    case PHONE_NOTE_DISMISS: phone_note_gone(uid); break;
    case PHONE_CALL_ANSWER:
    case PHONE_CALL_DECLINE: phone_call(0, NULL, NULL); break;
    case PHONE_PLAY_PAUSE: fp.pos = pos_now(), fp.at = lv_tick_get(), fp.playing = !fp.playing, send_track(); break;
    case PHONE_NEXT:
    case PHONE_PREV: fp.track = (fp.track + (cmd == PHONE_NEXT ? 1 : n - 1)) % n, fp.pos = 0, fp.at = lv_tick_get(), send_track(); break;
    case PHONE_VOL_UP:
    case PHONE_VOL_DOWN: fp.vol = fmaxf(0, fminf(1, fp.vol + (cmd == PHONE_VOL_UP ? 1 : -1) / 16.0f)), phone_volume(fp.vol); break;
    case PHONE_SHUTTER: fp.shots++; break;
    case PHONE_PAIR_OK:
        fp.link = PHONE_ON, phone_kind(fp.android ? PHONE_ANDROID : PHONE_IOS), phone_link(PHONE_ON, 0), phone_volume(fp.vol), send_track();
        linked();
        break;
    case PHONE_PAIR_CANCEL:
        fp.link = PHONE_OFF, phone_link(PHONE_OFF, 0);
        break;
    }
}

static void bt_query(phone_bt_t *s) /* the Bluetooth page: the pretend phone */
{
    s->n = 0, s->bonds = 2, s->advertising = true;
    if (fp.link == PHONE_ON) s->peer[s->n++] = (phone_peer_t){.role = PEER_PHONE, .rssi = fp.rssi, .secs = 754, .encrypted = true, .bonded = true, .ready = true, .ancs = !fp.android, .ams = !fp.android, .cts = !fp.android, .android = fp.android};
}
static void bt_act(int what)
{
    fp.last = what == BT_RECONNECT_PHONE ? "bt reconnect" : "bt forget";
    if (what == BT_FORGET_ALL && fp.link != PHONE_OFF) fp.link = PHONE_OFF, fp.music = false, phone_link(PHONE_OFF, 0); /* forgotten: gone, as on the board */
}

static int rssi_read(void) { return fp.link == PHONE_ON ? fp.rssi : 0; }
static void rssi_active(bool on) { (void)on; } /* no link pace to quicken here */
void fake_phone_rssi(int dbm) { fp.rssi = dbm; }
void fake_phone_init(void) { phone_on_command(on_command), phone_on_bt(bt_query, bt_act), phone_on_rssi(rssi_read, rssi_active); }

const char *fake_link_report(void) { return fp.link != PHONE_ON ? "link=off" : fp.android ? "link=android" : "link=iphone"; }
const char *fake_phone_last(void) { return fp.last; }
int fake_phone_shots(void) { return fp.shots; }

void fake_phone(char kind, int arg)
{
    if (kind == 'L' || kind == 'A') { /* the iPhone, or the Android phone (A): one phone at a time, as on the board */
        fp.android = kind == 'A';
        fp.link = arg == 1 ? PHONE_PAIRING : arg == 2 ? PHONE_ON : PHONE_OFF;
        phone_kind(fp.android ? PHONE_ANDROID : PHONE_IOS);
        phone_link(fp.link, 482913);
        if (fp.link == PHONE_ON) linked();
        if (fp.link == PHONE_ON) phone_volume(fp.vol), send_track();
    } else if (kind == 'N') { /* a notification: comes whether or not the phone is connected, for trying them out */
        int k = fp.note++ % (int)(sizeof notes / sizeof notes[0]);
        phone_note(++fp.uid, notes[k].app, notes[k].title, notes[k].text, notes[k].h, notes[k].m, false);
    } else if (fp.link != PHONE_ON || fp.android) {
        return; /* a phone that is not connected tells us nothing else; an Android one tells nothing at all */
    } else if (kind == 'C') {
        if (arg) phone_call(++fp.uid, "Мама", "мобильный");
        else phone_call(0, NULL, NULL);
    } else if (kind == 'M') {
        fp.music = arg != 0;
        fp.playing = fp.music, fp.pos = fp.music ? 102 : 0, fp.at = lv_tick_get();
        send_track();
    }
}
