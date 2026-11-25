/* The phone (an iPhone, or an Android one with far less) as the UI sees it: what the phone told us and what we ask it to do.
   The host fills it in (firmware: BLE with ANCS, AMS and a HID key; simulator: a fake phone) and carries out the commands.
   Nothing here needs an app on the phone. */
#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

enum { PHONE_OFF, PHONE_PAIRING, PHONE_ON };
enum {
    PHONE_NOTE_DISMISS, /* ANCS negative action: clears it on the phone too */
    PHONE_CALL_ANSWER,  /* ANCS positive / negative on the incoming-call notification */
    PHONE_CALL_DECLINE,
    PHONE_PLAY_PAUSE,   /* AMS remote commands */
    PHONE_NEXT,
    PHONE_PREV,
    PHONE_VOL_UP,
    PHONE_VOL_DOWN,
    PHONE_SHUTTER,      /* HID consumer key "volume up": the Camera app's shutter */
    PHONE_PAIR_OK,      /* the passkey matches */
    PHONE_PAIR_CANCEL,
    PHONE_CMD_COUNT
};

/* ---------- host -> UI ---------- */
/* Which phone: an iPhone (ANCS, AMS, CTS) or an Android one (no app: only the camera's shutter key works; the
   notifications and music apps say it is not supported there). Set before phone_link(PHONE_ON). */
enum { PHONE_IOS, PHONE_ANDROID };
void phone_kind(int kind);
void phone_link(int state, uint32_t code);                                     /* PHONE_PAIRING: show the 6-digit code */
void phone_note(uint32_t uid, const char *app, const char *title, const char *text, int hour, int min, bool quiet); /* quiet: no banner */
void phone_note_gone(uint32_t uid);
void phone_call(uint32_t uid, const char *name, const char *kind);             /* name NULL: the call is over */
void phone_track(const char *title, const char *artist, int dur_s, float pos_s, bool playing); /* title NULL: nothing */
bool phone_playing(void); /* the linked phone is playing music (AMS): he may dance to it though he cannot hear it */
void phone_volume(float v);                                                     /* 0..1 */

/* ---------- UI -> host ---------- */
void phone_on_command(void (*cb)(int cmd, uint32_t uid));
const char *phone_command_name(int cmd);

/* ---------- for the shell and the app list ---------- */
bool phone_button(int id);      /* the pairing, call and banner overlays take the side buttons first */
const char *phone_where(void);  /* "pair", "call" or "banner" while one is up, else NULL */
void phone_notify_open(lv_obj_t *scr);
void phone_notify_close(void);
bool phone_notify_button(int id);
void phone_music_open(lv_obj_t *scr);
void phone_music_close(void);
bool phone_music_button(int id);
/* the locator (Телефон > Локатор): the linked phone's signal strength, to walk toward it. The host gives the link's RSSI
   in dBm (0: no link or no reading) and is told when it is wanted (only while the locator is open: it may quicken the
   link meanwhile, and read nothing at all otherwise) */
void phone_on_rssi(int (*read)(void), void (*active)(bool on));
void phone_locate_open(lv_obj_t *scr);
void phone_locate_close(void);
const char *phone_locate_status(void); /* pipsim: "band,dBm" while it shows a reading, else "-" */
void phone_camera_open(lv_obj_t *scr);
void phone_camera_close(void);
bool phone_camera_button(int id);

/* What notifications show on a screen anyone can glance at. HIDE (the default): the app and the time; the sender and
   the text only once you open one. OPEN: everything. OFF: nothing is fetched from the phone at all (calls still ring).
   Codes, passwords and banks are never kept, in any mode. */
enum { PHONE_PRIV_OPEN, PHONE_PRIV_HIDE, PHONE_PRIV_OFF, PHONE_PRIV_COUNT };

/* The Bluetooth page: what the host knows about each link, and what it can do about them */
enum { PEER_PHONE, PEER_DEBUG, PEER_UNKNOWN }; /* PEER_DEBUG: the panel (or the debug page) */
typedef struct {
    int role, rssi, secs;               /* rssi in dBm (0: unknown), how long it has been connected */
    bool encrypted, bonded, ready, ancs, ams, cts; /* ready..cts: the phone's services */
    bool android;                       /* the phone is an Android one */
} phone_peer_t;
typedef struct { int n, bonds; bool advertising; phone_peer_t peer[5]; } phone_bt_t;
enum { BT_RECONNECT_PHONE, BT_FORGET_ALL };
void phone_on_bt(void (*query)(phone_bt_t *out), void (*act)(int what));
void phone_bt_open(lv_obj_t *scr);
/* Настройки > Приватность: how much of a notification the keychain shows (the panel's "privacy i") */
void phone_privacy_open(lv_obj_t *scr);
void phone_bt_close(void);
void phone_set_privacy(int mode);
int phone_privacy(void);
void phone_on_privacy(void (*cb)(int mode)); /* the host keeps the choice */
/* Calls on the keychain at all (Приватность's "Звонки", the panel's "calls 0|1"): off, a call never shows here */
void phone_set_calls(bool on);
bool phone_calls(void);
void phone_on_calls(void (*cb)(bool on));
