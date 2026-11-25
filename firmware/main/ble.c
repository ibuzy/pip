/* The iPhone link over BLE (NimBLE), with nothing to install on the phone.
   Pip advertises asking for Apple's notification and media services; the phone pairs once (numeric comparison: the
   same 6 digits on both screens) and bonds. Then, as a GATT client of the phone:
     ANCS  notifications and incoming calls -> phone_note / phone_call; dismiss, answer, decline go back
     AMS   now playing (title, artist, duration, position, volume) -> phone_track; play/pause, tracks, volume go back
     CTS   the phone's clock and time zone -> the system clock (Pip's day and the clock app)
   Pip is also a tiny Bluetooth keyboard with two media keys (HID): iOS lists keyboards in Settings -> Bluetooth
   (a plain BLE device it would not show), and "volume up" is the Camera app's shutter.
   Connections: the phone and the panel (or the debug page: whoever writes the data service is it). Anything else - a
   computer, a second phone - is left alone: it says it is not a phone (Appearance), or it has none of Apple's services
   and connects from its public address. An Android phone: none of Apple's services, and it says it is a phone or
   connects from a private address: it keeps the phone's place, only the shutter key works (phone_kind PHONE_ANDROID);
   no CTS, so its time comes from the panel (also open in Chrome on that phone, over Web Bluetooth).
   Everything the UI sees goes through phone.h, like the simulator's fake phone. */
#include "board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "nvs.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "phone.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static const char *TAG = "ble";
void ble_store_config_init(void);

/* ---------- Apple's UUIDs ---------- */
static ble_uuid128_t ANCS, ANCS_NS, ANCS_CP, ANCS_DS, AMS, AMS_RC, AMS_EU;
static const ble_uuid16_t CTS = BLE_UUID16_INIT(0x1805), CTS_TIME = BLE_UUID16_INIT(0x2A2B), CTS_LOCAL = BLE_UUID16_INIT(0x2A0F),
                          CCCD = BLE_UUID16_INIT(0x2902);

static void trim_utf8(char *s) /* a string cut short must not end inside a character */
{
    int n = (int)strlen(s), i = n;
    while (i > 0 && ((unsigned char)s[i - 1] & 0xC0) == 0x80) i--;
    if (i > 0) {
        unsigned c = (unsigned char)s[i - 1];
        int need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (n - (i - 1) < need) s[i - 1] = 0;
    }
}

static void uuid(ble_uuid128_t *u, const char *s) /* "7905F431-B5CE-..." -> little-endian bytes */
{
    u->u.type = BLE_UUID_TYPE_128;
    int n = 15;
    for (; *s && n >= 0; s++) {
        if (*s == '-') continue;
        char hex[3] = {s[0], s[1], 0}; /* strtoul, not sscanf: the scanf family alone is ~10 KB of flash */
        u->value[n--] = (uint8_t)strtoul(hex, NULL, 16);
        s++;
    }
}

/* ---------- connection state ---------- */
static struct {
    uint8_t own_addr;
    uint16_t conn;
    bool connected, numcmp, ready; /* ready: discovery done, ANCS requests may go */
    uint16_t svc_start, svc_end; /* the service being looked at */
    uint16_t ns, cp, ds, rc, eu, ct, lt; /* characteristic value handles, 0 = not there */
    uint16_t cccd;
    int step, retries;
    bool is_phone; /* it said so (Appearance): never taken for something else, however slow its services are */
    bool android;  /* a phone with none of Apple's services: only the shutter key works */
    uint32_t call_uid;
} b = {.conn = BLE_HS_CONN_HANDLE_NONE};

static void advertise(void);
static void step(void);
static void key(uint8_t bits);
static void not_ours(uint16_t conn);
static uint16_t pair_conn = BLE_HS_CONN_HANDLE_NONE; /* the connection the numeric comparison is for */

static void ui(void (*fn)(void *), void *arg) /* run UI code under the LVGL lock */
{
    ui_lock();
    fn(arg);
    ui_unlock();
}
static void ui_link_off(void *a) { (void)a; phone_link(PHONE_OFF, 0); }
static void link_pace(uint16_t conn, bool easy);
static bool locating; /* the locator is open: the phone's link at the quick pace, its RSSI read on request */
static void ui_link_on(void *a) { (void)a; phone_kind(b.android ? PHONE_ANDROID : PHONE_IOS), phone_link(PHONE_ON, 0); }
static void ui_link_pair(void *a) { phone_link(PHONE_PAIRING, *(uint32_t *)a); }

/* ---------- ANCS: notifications ---------- */
#define Q_MAX 24
static struct { uint32_t uid; uint8_t category; bool quiet; } q[Q_MAX]; /* waiting for their attributes */
static int q_n;
static bool fetching;
static int64_t fetch_at; /* when the attributes were asked for (esp_timer, us) */
static uint8_t ds_buf[800];
static int ds_len;

static const char *app_name(const char *bundle) /* the apps people have; anything else by its last word */
{
    static const struct { const char *id, *name; } known[] = {
        {"ph.telegra.Telegraph", "Telegram"}, {"com.burbn.instagram", "Instagram"}, {"net.whatsapp.WhatsApp", "WhatsApp"},
        {"com.apple.MobileSMS", "Сообщения"}, {"com.apple.mobilephone", "Телефон"}, {"com.apple.mobilemail", "Почта"},
        {"com.apple.mobilecal", "Календарь"}, {"com.apple.reminders", "Напоминания"}, {"com.google.Gmail", "Gmail"},
        {"com.vk.vkclient", "VK"}, {"com.apple.facetime", "FaceTime"}, {"com.spotify.client", "Spotify"},
    };
    for (size_t i = 0; i < sizeof known / sizeof known[0]; i++)
        if (!strcmp(bundle, known[i].id)) return known[i].name;
    const char *dot = strrchr(bundle, '.');
    return dot ? dot + 1 : bundle;
}

static void ancs_fetch(void);
static void q_pop(void) { if (q_n) memmove(&q[0], &q[1], sizeof q[0] * --q_n); }

/* The phone refuses the request (the notification was cleared meanwhile: "invalid parameter"): no reply will come on
   the Data Source, so that one is dropped and the next asked for. Waiting on it for ever kept every later notification
   in the queue until the phone reconnected. */
static int ancs_asked(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    (void)conn, (void)attr, (void)arg;
    if (!err || !err->status || !fetching) return 0;
    ESP_LOGW(TAG, "notification attributes refused (%d): skipped", err->status);
    fetching = false, ds_len = 0;
    q_pop();
    ancs_fetch();
    return 0;
}

static void ancs_fetch(void)
{
    /* an answer that never came (a reply lost with the link half-up): after 3 s give that one up, take the next */
    if (fetching && esp_timer_get_time() - fetch_at > 3000000) fetching = false, ds_len = 0, q_pop();
    if (fetching || !q_n || !b.cp || !b.ready) return;
    /* Get Notification Attributes: app id, title (64), message (480), date */
    uint8_t cmd[] = {0, 0, 0, 0, 0, 0, 1, 64, 0, 3, 480 & 255, 480 >> 8, 5};
    memcpy(&cmd[1], &q[0].uid, 4);
    fetching = true, ds_len = 0, fetch_at = esp_timer_get_time();
    if (ble_gattc_write_flat(b.conn, b.cp, cmd, sizeof cmd, ancs_asked, NULL)) fetching = false;
}

typedef struct { uint32_t uid; uint8_t category; bool quiet; char app[64], title[66], text[482], date[20]; } note_t;

static void ui_note(void *a)
{
    note_t *n = a;
    if (n->category == 1) { /* an incoming call: the title is who is calling */
        b.call_uid = n->uid;
        phone_call(n->uid, n->title[0] ? n->title : "Звонок", "входящий");
        return;
    }
    int h = 0, m = 0;
    if (strlen(n->date) >= 13) { /* yyyyMMdd'T'HHmmSS */
        const char *d = n->date + 9;
        if (d[0] >= '0' && d[0] <= '9' && d[1] >= '0' && d[1] <= '9') h = (d[0] - '0') * 10 + d[1] - '0';
        if (d[2] >= '0' && d[2] <= '9' && d[3] >= '0' && d[3] <= '9') m = (d[2] - '0') * 10 + d[3] - '0';
    }
    phone_note(n->uid, app_name(n->app), n->title[0] ? n->title : app_name(n->app), n->text, h, m, n->quiet);
}

static void ancs_data(const uint8_t *d, int len) /* Data Source: replies may span notifications */
{
    if (ds_len + len > (int)sizeof ds_buf) ds_len = 0;
    memcpy(ds_buf + ds_len, d, len);
    ds_len += len;
    if (ds_len < 5 || ds_buf[0] != 0) return;
    static note_t n;
    memset(&n, 0, sizeof n);
    memcpy(&n.uid, ds_buf + 1, 4);
    int pos = 5, got = 0;
    while (got < 4 && pos + 3 <= ds_len) {
        int id = ds_buf[pos], alen = ds_buf[pos + 1] | ds_buf[pos + 2] << 8;
        if (pos + 3 + alen > ds_len) return; /* the rest is still coming */
        char *dst = id == 0 ? n.app : id == 1 ? n.title : id == 3 ? n.text : id == 5 ? n.date : NULL;
        size_t cap = id == 0 ? sizeof n.app : id == 1 ? sizeof n.title : id == 3 ? sizeof n.text : sizeof n.date;
        if (dst) {
            int c = alen < (int)cap - 1 ? alen : (int)cap - 1;
            memcpy(dst, ds_buf + pos + 3, c);
            dst[c] = 0;
            trim_utf8(dst);
        }
        pos += 3 + alen, got++;
    }
    if (got < 4) return;
    ds_len = 0, fetching = false;
    if (q_n && q[0].uid == n.uid) {
        n.category = q[0].category, n.quiet = q[0].quiet;
        q_pop();
        ui(ui_note, &n);
    }
    ancs_fetch();
}

static void ui_gone(void *a)
{
    uint32_t uid = *(uint32_t *)a;
    if (uid == b.call_uid) phone_call(0, NULL, NULL), b.call_uid = 0;
    phone_note_gone(uid);
}

static void ancs_source(const uint8_t *d, int len) /* Notification Source: event, flags, category, count, uid */
{
    if (len < 8) return;
    uint32_t uid;
    memcpy(&uid, d + 4, 4);
    if (d[0] == 2) { /* removed: off the screen, and out of the queue if it was still waiting (not the one being asked for) */
        for (int i = fetching ? 1 : 0; i < q_n; i++)
            if (q[i].uid == uid) memmove(&q[i], &q[i + 1], sizeof q[0] * (q_n - i - 1)), q_n--, i--;
        ui(ui_gone, &uid);
        return;
    }
    if (d[0] != 0 || q_n >= Q_MAX) return;        /* only new ones */
    if (d[2] != 1 && phone_privacy() == PHONE_PRIV_OFF) return; /* notifications off: their contents never leave the phone; calls (category 1) still ring */
    q[q_n++] = (typeof(q[0])){uid, d[2], (d[1] & 0x04) != 0}; /* pre-existing: into the list without a banner */
    ancs_fetch();
}

static void ancs_action(uint32_t uid, uint8_t action) /* 0 positive (answer), 1 negative (clear, decline) */
{
    if (!b.cp) return;
    uint8_t cmd[6] = {2, 0, 0, 0, 0, action};
    memcpy(&cmd[1], &uid, 4);
    ble_gattc_write_flat(b.conn, b.cp, cmd, sizeof cmd, NULL, NULL);
}

/* ---------- AMS: now playing ---------- */
static struct { char title[64], artist[48]; float dur, pos; bool playing; float vol; } np;

static void ui_track(void *a)
{
    (void)a;
    phone_track(np.title[0] ? np.title : NULL, np.artist, (int)np.dur, np.pos, np.playing);
}
static void ui_volume(void *a) { (void)a; phone_volume(np.vol); }

static void ams_update(const uint8_t *d, int len) /* entity, attribute, flags, value */
{
    if (len < 3) return;
    char v[80];
    int n = len - 3 < (int)sizeof v - 1 ? len - 3 : (int)sizeof v - 1;
    memcpy(v, d + 3, n);
    v[n] = 0;
    if (d[0] == 0 && d[1] == 1) { /* player: "state,rate,elapsed" */
        int state = 0;
        float rate = 0, elapsed = 0;
        char *e = (char *)v;
        state = (int)strtol(e, &e, 10);
        if (*e == ',') rate = strtof(e + 1, &e);
        if (*e == ',') elapsed = strtof(e + 1, &e);
        (void)rate;
        np.playing = state == 1, np.pos = elapsed;
    } else if (d[0] == 0 && d[1] == 2) {
        np.vol = strtof(v, NULL);
        ui(ui_volume, NULL);
        return;
    } else if (d[0] == 2 && d[1] == 0) snprintf(np.artist, sizeof np.artist, "%.47s", v), trim_utf8(np.artist);
    else if (d[0] == 2 && d[1] == 2) snprintf(np.title, sizeof np.title, "%.63s", v), trim_utf8(np.title);
    else if (d[0] == 2 && d[1] == 3) np.dur = strtof(v, NULL);
    ui(ui_track, NULL);
}

/* ---------- commands from the UI (called in the LVGL task) ---------- */
static void command(int cmd, uint32_t uid)
{
    static const uint8_t remote[] = {[PHONE_PLAY_PAUSE] = 2, [PHONE_NEXT] = 3, [PHONE_PREV] = 4, [PHONE_VOL_UP] = 5, [PHONE_VOL_DOWN] = 6};
    switch (cmd) {
    case PHONE_NOTE_DISMISS: ancs_action(uid, 1); break;
    case PHONE_CALL_ANSWER: ancs_action(uid, 0); break;
    case PHONE_CALL_DECLINE: ancs_action(uid, 1); break;
    case PHONE_PLAY_PAUSE:
    case PHONE_NEXT:
    case PHONE_PREV:
    case PHONE_VOL_UP:
    case PHONE_VOL_DOWN:
        if (b.rc) ble_gattc_write_flat(b.conn, b.rc, &remote[cmd], 1, NULL, NULL);
        break;
    case PHONE_PAIR_OK:
    case PHONE_PAIR_CANCEL:
        if (b.numcmp) {
            struct ble_sm_io io = {.action = BLE_SM_IOACT_NUMCMP, .numcmp_accept = cmd == PHONE_PAIR_OK};
            ble_sm_inject_io(pair_conn, &io);
            b.numcmp = false;
        }
        break;
    case PHONE_SHUTTER: key(0x01); break; /* volume up */
    }
}

/* ---------- CTS: the clock ---------- */
static int cts_tz = -10000;
static void cts_apply(const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg) /* the zone, or the time */
{
    uint8_t v[10] = {0};
    uint16_t n = 0;
    if (err->status || !attr) return;
    ble_hs_mbuf_to_flat(attr->om, v, sizeof v, &n);
    if (arg == &b.lt && n >= 2) { /* zone and DST, in 15 min units; -128 and 255 are "unknown" */
        int z = (int8_t)v[0], dst = v[1] == 255 ? 0 : v[1];
        if (z != -128 && (z + dst) * 15 >= -12 * 60 && (z + dst) * 15 <= 14 * 60) cts_tz = (z + dst) * 15;
    }
    if (arg == &b.ct && n >= 7) {
        struct tm t = {.tm_year = (v[0] | v[1] << 8) - 1900, .tm_mon = v[2] - 1, .tm_mday = v[3], .tm_hour = v[4], .tm_min = v[5], .tm_sec = v[6]};
        int zone = cts_tz > -10000 ? cts_tz : tz_min;
        struct timeval tv = {.tv_sec = mktime(&t) - zone * 60}; /* the phone tells local time; the system keeps UTC */
        settimeofday(&tv, NULL);
        tz_min = zone;
        ESP_LOGI(TAG, "time from the iPhone: %04d-%02d-%02d %02d:%02d, UTC%+d min", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, zone);
    }
}

static int on_read(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    cts_apply(err, attr, arg);
    if (err->status != BLE_HS_EDONE) step();
    return 0;
}

/* Linked for long, the clock (the chip's RC clock while it light-sleeps) drifts: every RESYNC_H hours the zone and the
   time are read again from the phone, as at every connection */
#define RESYNC_H 24
static struct ble_npl_callout resync;
static void resync_arm(void) { ble_npl_callout_reset(&resync, ble_npl_time_ms_to_ticks32(3600 * 1000) * RESYNC_H); }
static int on_resync(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    cts_apply(err, attr, arg);
    if (arg == &b.lt && b.ready && b.ct) ble_gattc_read(b.conn, b.ct, on_resync, &b.ct); /* the zone first, then the time */
    return 0;
}
static void resync_now(struct ble_npl_event *ev)
{
    (void)ev;
    if (!b.ready || !b.ct) return; /* gone, or an Android phone: no clock to ask */
    resync_arm();
    if (!b.lt || ble_gattc_read(b.conn, b.lt, on_resync, &b.lt)) ble_gattc_read(b.conn, b.ct, on_resync, &b.ct);
}

/* ---------- discovery, one GATT procedure after another ---------- */
static int on_written(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    step();
    return 0;
}

static int on_svc(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_svc *svc, void *arg)
{
    if (!err->status && svc) b.svc_start = svc->start_handle, b.svc_end = svc->end_handle;
    if (err->status) step(); /* done (or not found) */
    return 0;
}

static int on_chr(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_chr *c, void *arg)
{
    if (!err->status && c) {
        const ble_uuid_t *u = &c->uuid.u;
        if (!ble_uuid_cmp(u, &ANCS_NS.u)) b.ns = c->val_handle;
        else if (!ble_uuid_cmp(u, &ANCS_CP.u)) b.cp = c->val_handle;
        else if (!ble_uuid_cmp(u, &ANCS_DS.u)) b.ds = c->val_handle;
        else if (!ble_uuid_cmp(u, &AMS_RC.u)) b.rc = c->val_handle;
        else if (!ble_uuid_cmp(u, &AMS_EU.u)) b.eu = c->val_handle;
        else if (!ble_uuid_cmp(u, &CTS_TIME.u)) b.ct = c->val_handle;
        else if (!ble_uuid_cmp(u, &CTS_LOCAL.u)) b.lt = c->val_handle;
    }
    if (err->status) step();
    return 0;
}

static int on_dsc(uint16_t conn, const struct ble_gatt_error *err, uint16_t chr, const struct ble_gatt_dsc *d, void *arg)
{
    if (!err->status && d && !b.cccd && !ble_uuid_cmp(&d->uuid.u, &CCCD.u)) b.cccd = d->handle;
    if (err->status) step();
    return 0;
}

static bool find_svc(const ble_uuid_t *u)
{
    b.svc_start = b.svc_end = 0;
    return !ble_gattc_disc_svc_by_uuid(b.conn, u, on_svc, NULL);
}
static bool find_chrs(void) { return b.svc_start && !ble_gattc_disc_all_chrs(b.conn, b.svc_start, b.svc_end, on_chr, NULL); }
static bool find_cccd(uint16_t val, uint16_t end)
{
    b.cccd = 0;
    return val && !ble_gattc_disc_all_dscs(b.conn, val, end, on_dsc, NULL);
}
static bool subscribe(void)
{
    static const uint8_t on[2] = {1, 0};
    return b.cccd && !ble_gattc_write_flat(b.conn, b.cccd, on, 2, on_written, NULL);
}

/* Android or something else (a computer), when neither Appearance nor Apple's services tell: phones connect from a resolvable private
   address (a random one whose top bits are 01, rotating), Windows and Linux from their public one. A Mac says it is
   not a phone (Appearance) before it gets this far. */
static bool private_addr(uint16_t conn)
{
    struct ble_gap_conn_desc d;
    if (ble_gap_conn_find(conn, &d)) return false;
    return d.peer_ota_addr.type == BLE_ADDR_RANDOM && (d.peer_ota_addr.val[5] >> 6) == 1;
}

static uint16_t ancs_end, ams_end;
static struct ble_npl_callout again; /* runs in the host task, like the GATT callbacks */
static void step(void);
static void look_again(struct ble_npl_event *ev) { (void)ev; if (b.connected) b.step = 0, step(); }

static void step(void)
{
    for (;;) {
        bool started = false;
        switch (b.step++) {
        case 0: started = find_svc(&ANCS.u); break;
        case 1: ancs_end = b.svc_end, started = find_chrs(); break;
        case 2: started = find_svc(&AMS.u); break;
        case 3: ams_end = b.svc_end, started = find_chrs(); break;
        case 4: started = find_svc(&CTS.u); break;
        case 5: started = find_chrs(); break;
        /* Data Source before Notification Source, as Apple asks: no reply may be missed */
        case 6: started = find_cccd(b.ds, ancs_end); break;
        case 7: started = subscribe(); break;
        case 8: started = find_cccd(b.ns, ancs_end); break;
        case 9: started = subscribe(); break;
        case 10: started = find_cccd(b.eu, ams_end); break;
        case 11: started = subscribe(); break;
        case 12: { /* the player's state and volume */
            static const uint8_t player[] = {0, 1, 2};
            started = b.eu && !ble_gattc_write_flat(b.conn, b.eu, player, sizeof player, on_written, NULL);
            break;
        }
        case 13: { /* the track's artist, title and duration */
            static const uint8_t track[] = {2, 0, 2, 3};
            started = b.eu && !ble_gattc_write_flat(b.conn, b.eu, track, sizeof track, on_written, NULL);
            break;
        }
        case 14: started = b.lt && !ble_gattc_read(b.conn, b.lt, on_read, &b.lt); break;
        case 15: started = b.ct && !ble_gattc_read(b.conn, b.ct, on_read, &b.ct); break;
        case 16:
            if (!b.ns && !b.eu && !b.ct && b.retries++ < 3) { /* nothing at all: the phone was not ready to be asked yet */
                ESP_LOGW(TAG, "no services found, looking again in 2 s");
                ble_npl_callout_reset(&again, ble_npl_time_ms_to_ticks32(2000));
                return;
            }
            if (!b.ns && !b.eu && !b.is_phone && !private_addr(b.conn)) { not_ours(b.conn); return; } /* nothing a phone has: a computer */
            b.android = !b.ns && !b.eu; /* a phone (it said so, or it hides behind a private address) with nothing of Apple's */
            ESP_LOGI(TAG, "%s ready: ANCS %s, AMS %s, CTS %s", b.android ? "Android" : "iPhone", b.ns && b.ds && b.cp ? "yes" : "no", b.eu && b.rc ? "yes" : "no", b.ct ? "yes" : "no");
            b.ready = true;
            if (b.ct) resync_arm(); /* and again a day later, while it stays */
            ui(ui_link_on, NULL);
            ancs_fetch(); /* what queued up while we were looking around */
            link_pace(b.conn, !locating);
            return;
        default: return;
        }
        if (started) return; /* its callback comes back here */
    }
}

/* ---------- our own services: a two-key HID keyboard, the battery, who we are ---------- */
static const uint8_t report_map[] = {
    0x05, 0x0C,       /* usage page: consumer */
    0x09, 0x01,       /* usage: consumer control */
    0xA1, 0x01,       /* collection: application */
    0x85, 0x01,       /*   report id 1 */
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02, /* two 1-bit buttons */
    0x09, 0xE9,       /*   volume up: the Camera app's shutter */
    0x09, 0xEA,       /*   volume down */
    0x81, 0x02,       /*   input */
    0x95, 0x06, 0x81, 0x03, /* padding to a byte */
    0xC0,
};
static uint16_t report_handle, battery_handle;

static int read_flat(struct ble_gatt_access_ctxt *ctxt, const void *v, int n)
{
    return os_mbuf_append(ctxt->om, v, n) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}

static int hid_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t id = ble_uuid_u16(ctxt->chr ? ctxt->chr->uuid : ctxt->dsc->uuid);
    static const uint8_t info[4] = {0x11, 0x01, 0x00, 0x02}, ref[2] = {1, 1}, zero = 0, protocol = 1;
    static const uint8_t pnp[7] = {0x02, 0x5E, 0x04, 0x01, 0x00, 0x01, 0x00}; /* USB vendor id space, product 1, v1 */
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR || ctxt->op == BLE_GATT_ACCESS_OP_WRITE_DSC) return 0; /* control point */
    switch (id) {
    case 0x2A4A: return read_flat(ctxt, info, 4);                            /* HID information */
    case 0x2A4B:                                                             /* report map */
        return read_flat(ctxt, report_map, sizeof report_map);
    case 0x2A4D: return read_flat(ctxt, &zero, 1);                            /* the report: nothing pressed */
    case 0x2908: return read_flat(ctxt, ref, 2);                              /* report reference: id, input */
    case 0x2A4E: return read_flat(ctxt, &protocol, 1);                        /* protocol mode: report */
    case 0x2A50: return read_flat(ctxt, pnp, 7);                              /* PnP id */
    case 0x2A29: return read_flat(ctxt, "KeyPet", 6);                         /* manufacturer */
    case 0x2A19: {                                                            /* battery level */
        int pct = shell_battery_level(NULL);
        uint8_t v = pct < 0 ? 100 : (uint8_t)pct;
        return read_flat(ctxt, &v, 1);
    }
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int dbg_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg);
static ble_uuid128_t DBG, DBG_RX, DBG_TX;
static uint16_t dbg_tx;

static const struct ble_gatt_svc_def services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x1812),
     .characteristics = (struct ble_gatt_chr_def[]){
         {.uuid = BLE_UUID16_DECLARE(0x2A4A), .access_cb = hid_access, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC},
         {.uuid = BLE_UUID16_DECLARE(0x2A4B), .access_cb = hid_access, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC},
         {.uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = hid_access, .val_handle = &report_handle,
          .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,
          .descriptors = (struct ble_gatt_dsc_def[]){{.uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ | BLE_ATT_F_READ_ENC,
                                                      .access_cb = hid_access}, {0}}},
         {.uuid = BLE_UUID16_DECLARE(0x2A4C), .access_cb = hid_access, .flags = BLE_GATT_CHR_F_WRITE_NO_RSP},
         {.uuid = BLE_UUID16_DECLARE(0x2A4E), .access_cb = hid_access, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP},
         {0}}},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x180F),
     .characteristics = (struct ble_gatt_chr_def[]){
         {.uuid = BLE_UUID16_DECLARE(0x2A19), .access_cb = hid_access, .val_handle = &battery_handle,
          .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY}, {0}}},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &DBG.u,
     .characteristics = (struct ble_gatt_chr_def[]){
         {.uuid = &DBG_RX.u, .access_cb = dbg_access, .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
         {.uuid = &DBG_TX.u, .access_cb = dbg_access, .val_handle = &dbg_tx, .flags = BLE_GATT_CHR_F_NOTIFY}, {0}}},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x180A),
     .characteristics = (struct ble_gatt_chr_def[]){
         {.uuid = BLE_UUID16_DECLARE(0x2A50), .access_cb = hid_access, .flags = BLE_GATT_CHR_F_READ},
         {.uuid = BLE_UUID16_DECLARE(0x2A29), .access_cb = hid_access, .flags = BLE_GATT_CHR_F_READ}, {0}}},
    {0},
};

static void key(uint8_t bits) /* press, then release */
{
    if (!b.connected || !report_handle) return;
    uint8_t up = 0;
    ble_gatts_notify_custom(b.conn, report_handle, ble_hs_mbuf_from_flat(&bits, 1));
    ble_gatts_notify_custom(b.conn, report_handle, ble_hs_mbuf_from_flat(&up, 1));
}

/* ---------- the debug link: tools/pipdebug.html in Chrome (Web Bluetooth), next to the iPhone ----------
   A second connection. Who is who: one that writes to the debug service within 4 s of connecting is the debug
   page; otherwise it is the phone (or the phone is already here, so it is the page). */
static uint16_t dbg_conn = BLE_HS_CONN_HANDLE_NONE, pending = BLE_HS_CONN_HANDLE_NONE;
static uint16_t pending2 = BLE_HS_CONN_HANDLE_NONE; /* came while the phone was here: the debug page (or nothing of ours) */
static int64_t since[8]; /* when each connection came (handles are small numbers), for the Bluetooth page */
static struct ble_npl_callout claim;
static void advertise(void);

static void phone_take(uint16_t conn)
{
    ble_npl_callout_stop(&claim);
    pending = BLE_HS_CONN_HANDLE_NONE;
    if (b.connected && b.conn != conn) { pending2 = conn; return; } /* the phone's place is taken: the panel, or nothing of ours */
    b = (typeof(b)){.own_addr = b.own_addr, .conn = conn, .connected = true};
    q_n = 0, fetching = false;
    ESP_LOGI(TAG, "connected");
    ble_gattc_exchange_mtu(b.conn, NULL, NULL);
    ble_gap_security_initiate(b.conn); /* bonded: just encrypts; new: the pairing dialog on the phone */
    advertise();                       /* the debug page may still come */
}
static void claim_phone(struct ble_npl_event *ev) { (void)ev; if (pending != BLE_HS_CONN_HANDLE_NONE) phone_take(pending); }

/* A link that is neither the phone nor the panel (a computer left alone, one that never wrote to us) took one of the
   three slots and woke the radio every interval: let go of it FOREIGN_S after it came (the panel in Chrome on a computer
   writes within a second or two; one still unsure, pending2, gets PENDING2_S) */
#define FOREIGN_S 10
#define PENDING2_S 30
static struct ble_npl_callout sweep;
static void sweep_now(struct ble_npl_event *ev)
{
    (void)ev;
    bool more = false;
    int64_t now = esp_timer_get_time();
    for (uint16_t h = 0; h < 8; h++) {
        struct ble_gap_conn_desc d;
        if (h == b.conn || h == dbg_conn || h == pending || ble_gap_conn_find(h, &d) != 0) continue;
        if (now - since[h] < (h == pending2 ? PENDING2_S : FOREIGN_S) * 1000000LL) { more = true; continue; }
        ESP_LOGI(TAG, "neither the phone nor the panel: let go");
        ble_gap_terminate(h, BLE_ERR_REM_USER_CONN_TERM);
    }
    if (more) ble_npl_callout_reset(&sweep, ble_npl_time_ms_to_ticks32(2000));
}

/* Not a phone (a computer, say): out of the phone's place and left alone - no pairing, no services asked for. The
   panel in Chrome on it still works: whatever writes the data service is the panel (dbg_access). */
static void not_ours(uint16_t c)
{
    if (c == pending) ble_npl_callout_stop(&claim), pending = BLE_HS_CONN_HANDLE_NONE;
    if (c == b.conn) b = (typeof(b)){.own_addr = b.own_addr, .conn = BLE_HS_CONN_HANDLE_NONE}, ui(ui_link_off, NULL);
    ESP_LOGI(TAG, "not a phone: left alone, let go soon (sweep)");
    advertise();
}

/* Who is it: every Apple device states its kind in GAP's Appearance (category = value >> 6: 1 phone, 2 computer).
   Guessing from the order they connect in mixed them up when the iPhone and the Mac came back within a second, and
   a phone taken for something else is never asked for its notifications. Without an answer: the phone's place if it
   is free (the services found then decide), nothing of ours if not. */
static void decide(uint16_t c, int cat)
{
    bool slot = c == b.conn || !b.connected;
    if (cat > 1 || (cat != 1 && !slot)) { not_ours(c); return; } /* a Mac calls itself a "media player" (0x0280) */
    if (!slot) { ESP_LOGW(TAG, "a second phone: left alone"); return; }
    if (c != b.conn) {
        b = (typeof(b)){.own_addr = b.own_addr, .conn = c, .connected = true};
        q_n = 0, fetching = false;
        ble_gattc_exchange_mtu(c, NULL, NULL);
        advertise();
    }
    b.is_phone = cat == 1;
    b.step = 0, b.retries = 0;
    step();
}

static uint16_t who_done = BLE_HS_CONN_HANDLE_NONE;
static int on_who(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    if (!err->status && attr) {
        uint16_t v = 0;
        ble_hs_mbuf_to_flat(attr->om, &v, sizeof v, NULL);
        ESP_LOGI(TAG, "it says it is %s (appearance 0x%04x)", v >> 6 == 1 ? "a phone" : v >> 6 ? "not a phone" : "nothing", v);
        who_done = conn;
        decide(conn, v >> 6);
    } else if (who_done != conn) decide(conn, -1); /* no answer */
    if (err->status) who_done = BLE_HS_CONN_HANDLE_NONE;
    return 0;
}

static void who_is(uint16_t c)
{
    if (ble_gattc_read_by_uuid(c, 1, 0xFFFF, BLE_UUID16_DECLARE(0x2A01), on_who, NULL)) decide(c, -1);
}

/* The same question asked the moment it connects, before any pairing (Appearance needs no encryption): a computer
   is then never shown the phone's pairing screen. */
static int8_t kind[8]; /* what each connection said it is: Appearance category, 0 not yet */
static int kind_of(uint16_t c) { return c < 8 ? kind[c] : 0; }

static int on_who_early(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    if (err->status || !attr || conn >= 8) return 0;
    uint16_t v = 0;
    ble_hs_mbuf_to_flat(attr->om, &v, sizeof v, NULL);
    kind[conn] = (int8_t)(v >> 6 ? v >> 6 : -1);
    ESP_LOGI(TAG, "connecting: %s (appearance 0x%04x)", v >> 6 == 1 ? "a phone" : v >> 6 ? "not a phone" : "it does not say", v);
    if (v >> 6 > 1 && conn == pending) not_ours(conn);
    return 0;
}

static int dbg_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return 0;
    if (conn == pending) ble_npl_callout_stop(&claim), pending = BLE_HS_CONN_HANDLE_NONE;
    if (conn == pending2) pending2 = BLE_HS_CONN_HANDLE_NONE;
    static char line[200];
    static int len;
    if (dbg_conn != conn) {
        dbg_conn = conn, len = 0; /* a line cut off by a lost link is not glued to the next page's first */
        ESP_LOGI(TAG, "debug page connected");
        if (conn == b.conn) link_pace(conn, false); /* the panel on the phone's own link: quick while it is there */
        advertise();
    }
    uint8_t d[200];
    uint16_t n = 0;
    ble_hs_mbuf_to_flat(ctxt->om, d, sizeof d, &n);
    for (int i = 0; i < n; i++) {
        if (d[i] == '\n') line[len] = 0, debug_line(line), len = 0;
        else if (len < (int)sizeof line - 1) line[len++] = (char)d[i];
    }
    return 0;
}

bool ble_debug_on(void) { return dbg_conn != BLE_HS_CONN_HANDLE_NONE; }

void ble_debug_send(const char *s, int n) /* notifications as big as the link allows; waits when the buffers are full */
{
    uint16_t c = dbg_conn;
    if (c == BLE_HS_CONN_HANDLE_NONE) return;
    int mtu = ble_att_mtu(c) - 3;
    if (mtu < 20) mtu = 20;
    for (int at = 0, waits = 0; at < n;) {
        int k = n - at < mtu ? n - at : mtu;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(s + at, (uint16_t)k);
        int rc = om ? ble_gatts_notify_custom(c, dbg_tx, om) : BLE_HS_ENOMEM;
        if (rc == BLE_HS_ENOMEM || rc == BLE_HS_EBUSY) { /* full: wait, but not for ever (a stuck link drops the rest) */
            if (++waits > 200) return;
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        if (rc) return; /* gone */
        at += k;
    }
}

/* The phone's link at an easy pace once it is set up: iOS starts at ~30 ms, both radios waking every interval. Asked
   for 30..60 ms with 4 events we may skip when we have nothing to say (a notification or a press still goes at the next
   one: ~300 ms at most), a 5 s timeout - inside Apple's rules (max x (latency + 1) <= 2 s, x 3 < the timeout, min >= 15 ms,
   min + 15 <= max). The panel riding the phone's link (Chrome on Android) gets the quick pace back while it is there. */
static void link_pace(uint16_t conn, bool easy)
{
    struct ble_gap_upd_params p = {.itvl_min = 24, .itvl_max = easy ? 48 : 36, .latency = easy ? 4 : 0,
                                   .supervision_timeout = 500, .min_ce_len = 0, .max_ce_len = 0};
    int rc = ble_gap_update_params(conn, &p);
    ESP_LOGI(TAG, "link pace %s: %d", easy ? "easy" : "quick", rc);
}

/* ---------- GAP ---------- */
static int gap_event(struct ble_gap_event *e, void *arg)
{
    switch (e->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (e->connect.status) { advertise(); break; }
        if (e->connect.conn_handle < 8) since[e->connect.conn_handle] = esp_timer_get_time(), kind[e->connect.conn_handle] = 0;
        ble_gattc_read_by_uuid(e->connect.conn_handle, 1, 0xFFFF, BLE_UUID16_DECLARE(0x2A01), on_who_early, NULL);
        ble_npl_callout_reset(&sweep, ble_npl_time_ms_to_ticks32(2000)); /* whoever it turns out to be */
        if (b.connected || pending != BLE_HS_CONN_HANDLE_NONE) { /* the panel (it writes to us), or nothing of ours */
            pending2 = e->connect.conn_handle;
            advertise();
            break;
        }
        pending = e->connect.conn_handle; /* the phone, unless it turns out to be the debug page */
        ble_npl_callout_reset(&claim, ble_npl_time_ms_to_ticks32(4000));
        advertise();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        if (e->disconnect.conn.conn_handle == dbg_conn) {
            dbg_conn = BLE_HS_CONN_HANDLE_NONE;
            settings_panel(false); /* the panel over Bluetooth went: Pip is his own again at once */
            ESP_LOGI(TAG, "debug page left");
            /* the panel in Chrome on the phone itself rides the phone's own link: it went too */
            if (e->disconnect.conn.conn_handle != b.conn) { advertise(); break; }
        }
        if (e->disconnect.conn.conn_handle == pending) { ble_npl_callout_stop(&claim), pending = BLE_HS_CONN_HANDLE_NONE; advertise(); break; }
        if (e->disconnect.conn.conn_handle == pending2) { pending2 = BLE_HS_CONN_HANDLE_NONE; advertise(); break; }
        if (e->disconnect.conn.conn_handle != b.conn) { advertise(); break; } /* one we left alone */
        ESP_LOGI(TAG, "disconnected (%d)", e->disconnect.reason);
        b.connected = false, b.conn = BLE_HS_CONN_HANDLE_NONE;
        ble_npl_callout_stop(&resync);
        ui(ui_link_off, NULL);
        advertise();
        break;
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        uint16_t pc = e->passkey.conn_handle;
        if (pc == pending2 || kind_of(pc) > 1 || (pc != pending && pc != b.conn)) { /* not the phone: no pairing with us */
            if (e->passkey.params.action == BLE_SM_IOACT_NUMCMP)
                ble_sm_inject_io(pc, &(struct ble_sm_io){.action = BLE_SM_IOACT_NUMCMP, .numcmp_accept = 0});
            break;
        }
        if (pc == pending) phone_take(pending);
        pair_conn = e->passkey.conn_handle;
        if (e->passkey.params.action == BLE_SM_IOACT_NUMCMP) { /* both screens show it; the top button confirms */
            b.numcmp = true;
            uint32_t code = e->passkey.params.numcmp;
            ui(ui_link_pair, &code);
        } else if (e->passkey.params.action == BLE_SM_IOACT_DISP) { /* the other side asks to type ours */
            struct ble_sm_io io = {.action = BLE_SM_IOACT_DISP, .passkey = esp_random() % 1000000};
            ble_sm_inject_io(e->passkey.conn_handle, &io);
            uint32_t code = io.passkey;
            ui(ui_link_pair, &code);
        }
        break;
    }
    case BLE_GAP_EVENT_ENC_CHANGE: {
        uint16_t c = e->enc_change.conn_handle;
        if (e->enc_change.status) {
            ESP_LOGI(TAG, "encryption failed");
            /* its keys are gone here (forgotten): drop it rather than hang "connecting"; it pairs anew next time */
            if (c == b.conn) ui(ui_link_off, NULL), ble_gap_terminate(c, BLE_ERR_REM_USER_CONN_TERM);
            if (c == pending2) pending2 = BLE_HS_CONN_HANDLE_NONE;
            break;
        }
        if (c == pending) phone_take(pending); /* the debug page never asks for encryption */
        if (c == pending2) pending2 = BLE_HS_CONN_HANDLE_NONE;
        ESP_LOGI(TAG, "encryption on");
        if (kind_of(c) > 0) decide(c, kind_of(c)); /* it said so when it connected */
        else who_is(c);                         /* a phone or not: it says so itself */
        break;
    }
    case BLE_GAP_EVENT_CONN_UPDATE: { /* what the other side settled on (link_pace asked) */
        struct ble_gap_conn_desc d;
        if (!e->conn_update.status && !ble_gap_conn_find(e->conn_update.conn_handle, &d))
            ESP_LOGI(TAG, "link: %d.%02d ms, latency %d, timeout %d ms", d.conn_itvl * 125 / 100, d.conn_itvl * 125 % 100, d.conn_latency, d.supervision_timeout * 10);
        break;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING: { /* the phone forgot us: forget it too and pair again */
        struct ble_gap_conn_desc d;
        if (!ble_gap_conn_find(e->repeat_pairing.conn_handle, &d)) ble_store_util_delete_peer(&d.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    case BLE_GAP_EVENT_NOTIFY_RX: {
        uint8_t d[256];
        uint16_t n = 0;
        ble_hs_mbuf_to_flat(e->notify_rx.om, d, sizeof d, &n);
        uint16_t h = e->notify_rx.attr_handle;
        if (h == b.ns) ancs_source(d, n);
        else if (h == b.ds) ancs_data(d, n);
        else if (h == b.eu) ams_update(d, n);
        break;
    }
    }
    return 0;
}

/* As a keyboard (the HID service and the keyboard appearance) named Pip, which iOS lists in Settings -> Bluetooth;
   the scan response asks for ANCS. */
/* ---------- the locator (phone.c phone_locate_*): the phone's link strength, read only while it is open ---------- */
static int rssi_read(void)
{
    if (!b.connected || b.conn == BLE_HS_CONN_HANDLE_NONE) return 0;
    int8_t r = 0;
    return ble_gap_conn_rssi(b.conn, &r) == 0 && r < 0 ? r : 0;
}
static void rssi_active(bool on) /* quick while it looks (a reading every connection event), easy again after */
{
    locating = on;
    if (b.connected && b.ready && b.conn != BLE_HS_CONN_HANDLE_NONE && dbg_conn != b.conn) link_pace(b.conn, !on);
}

/* ---------- the Bluetooth page (phone.c phone_bt_*): who is on which link, a reconnect, forgetting everyone ---------- */
static void bt_peer(phone_bt_t *s, uint16_t c, int role)
{
    struct ble_gap_conn_desc d;
    if (c == BLE_HS_CONN_HANDLE_NONE || s->n >= 5 || ble_gap_conn_find(c, &d)) return;
    int8_t rssi = 0;
    ble_gap_conn_rssi(c, &rssi);
    phone_peer_t *p = &s->peer[s->n++];
    *p = (phone_peer_t){.role = role, .rssi = rssi, .secs = c < 8 ? (int)((esp_timer_get_time() - since[c]) / 1000000) : 0,
                        .encrypted = d.sec_state.encrypted, .bonded = d.sec_state.bonded};
    if (role == PEER_PHONE) p->ready = b.ready, p->ancs = b.ns && b.ds && b.cp, p->ams = b.eu && b.rc, p->cts = b.ct, p->android = b.android;
}

static void bt_query(phone_bt_t *s)
{
    memset(s, 0, sizeof *s);
    bt_peer(s, b.conn, PEER_PHONE);
    bt_peer(s, dbg_conn, PEER_DEBUG); /* may be the phone's own link: Chrome on that phone */
    bt_peer(s, pending, PEER_UNKNOWN);
    bt_peer(s, pending2, PEER_UNKNOWN);
    s->advertising = ble_gap_adv_active();
    ble_store_util_count(BLE_STORE_OBJ_TYPE_OUR_SEC, &s->bonds);
}

/* "забыть всё": off the air for FORGET_QUIET_MS first, so the bonded phone cannot come straight back on its old keys
   while everything goes down; then advertising again, for a fresh pairing */
#define FORGET_QUIET_MS 10000
static bool forgetting;
static struct ble_npl_callout unforget;
static void forget_over(struct ble_npl_event *ev) { (void)ev; forgetting = false; advertise(); }

static void bt_act(int what)
{
    if (what == BT_RECONNECT_PHONE) { /* drop the phone's link: iOS comes straight back and everything is looked up anew */
        if (b.connected) ble_gap_terminate(b.conn, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }
    /* forget everyone: off the air, every link down, every bond */
    forgetting = true;
    ble_gap_adv_stop();
    ble_npl_callout_reset(&unforget, ble_npl_time_ms_to_ticks32(FORGET_QUIET_MS));
    const uint16_t cs[] = {b.conn, dbg_conn, pending, pending2};
    for (unsigned i = 0; i < sizeof cs / sizeof cs[0]; i++)
        if (cs[i] != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(cs[i], BLE_ERR_REM_USER_CONN_TERM);
    ble_store_clear();
    ESP_LOGI(TAG, "forgot every bonded device");
}

static bool up, quiet; /* the host started; asleep in energy saving: off the air */

void ble_quiet(bool q)
{
    if (!up) return;
    quiet = q;
    if (!q) { advertise(); ESP_LOGI(TAG, "awake: advertising again"); return; }
    ble_gap_adv_stop();
    const uint16_t cs[] = {b.conn, dbg_conn, pending, pending2}; /* the iPhone reconnects by itself when we are back */
    for (unsigned i = 0; i < sizeof cs / sizeof cs[0]; i++)
        if (cs[i] != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(cs[i], BLE_ERR_REM_USER_CONN_TERM);
    ESP_LOGI(TAG, "asleep: Bluetooth off the air");
}

static void advertise(void)
{
    if (quiet || forgetting) return; /* a link that just went down would call us back on air */
    static const uint8_t adv[] = {2, 0x01, 0x06,          /* flags: general discoverable, LE only */
                                  3, 0x03, 0x12, 0x18,    /* services: HID */
                                  3, 0x19, 0xC1, 0x03,    /* appearance: keyboard */
                                  4, 0x09, 'P', 'i', 'p'}; /* name */
    uint8_t rsp[18] = {17, 0x15};
    memcpy(rsp + 2, ANCS.value, 16);
    ble_gap_adv_set_data(adv, sizeof adv);
    ble_gap_adv_rsp_set_data(rsp, sizeof rsp);
    if (ble_gap_adv_active()) return;
    struct ble_gap_adv_params p = {.conn_mode = BLE_GAP_CONN_MODE_UND, .disc_mode = BLE_GAP_DISC_MODE_GEN};
    int rc = ble_gap_adv_start(b.own_addr, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc) ESP_LOGW(TAG, "advertising failed: %d", rc);
}

/* Bump when the services table changes: phones and computers bonded to the old one are told once (Service Changed, on
   their next connection) to look at our services again instead of trusting the handles they cached. */
#define GATT_LAYOUT 4 /* 4: no computer's service (3 had media keys for one) */

static void gatt_layout_check(void)
{
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open("keypet", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_get_u8(h, "gatt", &v);
    if (v != GATT_LAYOUT) {
        ble_svc_gatt_changed(0x0001, 0xFFFF);
        if (nvs_set_u8(h, "gatt", GATT_LAYOUT) == ESP_OK) nvs_commit(h);
        ESP_LOGI(TAG, "services changed (%d -> %d): bonded peers will look again", v, GATT_LAYOUT);
    }
    nvs_close(h);
}

static void on_sync(void)
{
    ble_hs_id_infer_auto(0, &b.own_addr);
    gatt_layout_check();
    advertise();
    ESP_LOGI(TAG, "advertising as Pip");
}

static void host_task(void *arg)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_start(void)
{
    uuid(&ANCS, "7905F431-B5CE-4E99-A40F-4B1E122D00D0");
    uuid(&ANCS_NS, "9FBF120D-6301-42D9-8C58-25E699A21DBD");
    uuid(&ANCS_CP, "69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9");
    uuid(&ANCS_DS, "22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB");
    uuid(&AMS, "89D3502B-0F36-433A-8EF4-C502AD55F8DC");
    uuid(&AMS_RC, "9B3C81D8-57B1-4A8A-B8DF-0E56F7CA51C2");
    uuid(&AMS_EU, "2F7CABCE-808D-411F-9A0C-BB92BA96C102");
    uuid(&DBG, "50495044-4247-4B45-5950-455400000001");
    uuid(&DBG_RX, "50495044-4247-4B45-5950-455400000002");
    uuid(&DBG_TX, "50495044-4247-4B45-5950-455400000003");
    if (nimble_port_init() != ESP_OK) { ESP_LOGE(TAG, "NimBLE did not start"); return; }
    ble_npl_callout_init(&again, nimble_port_get_dflt_eventq(), look_again, NULL);
    ble_npl_callout_init(&resync, nimble_port_get_dflt_eventq(), resync_now, NULL);
    ble_npl_callout_init(&claim, nimble_port_get_dflt_eventq(), claim_phone, NULL);
    ble_npl_callout_init(&sweep, nimble_port_get_dflt_eventq(), sweep_now, NULL);
    ble_npl_callout_init(&unforget, nimble_port_get_dflt_eventq(), forget_over, NULL);
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_YESNO; /* numeric comparison: secure, and one button press */
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("Pip");
    ble_svc_gap_device_appearance_set(0x03C1); /* keyboard */
    if (ble_gatts_count_cfg(services) || ble_gatts_add_svcs(services)) ESP_LOGE(TAG, "HID services did not register");
    ble_store_config_init(); /* bonds in NVS: pair once */
    phone_on_command(command);
    phone_on_bt(bt_query, bt_act);
    phone_on_rssi(rssi_read, rssi_active);
    nimble_port_freertos_init(host_task);
    up = true;
}
