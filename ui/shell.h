/* Navigation: Pip's face <-> app carousel <-> apps. Hosts call these instead of the pip_* ones. */
#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>


void shell_create(void);
void shell_time(int64_t unix_s, int tz_min);            /* wall clock for Pip and the apps */
void shell_imu(float ax, float ay, float az, float dt); /* accelerometer in g, device axes */
void shell_button(int id);                              /* top PIP_BTN_PWR = menu / open, bottom PIP_BTN_BOOT = back / meet */
void shell_button_up(int id);                           /* a key let go (only the bottom one tells: the top is the PMU's) */
int shell_mic(void);                                     /* the microphone: 0 off, 1 the level alone (the noise meter), 2 all (Pip listens) */
bool shell_in_setup(void);                               /* "РЕЖИМ НАСТРОЙКИ" over everything: rings and bells wait */
const char *shell_where(void);                          /* "pip", "menu" or the open app's id */
void shell_set_theme(int theme);                        /* TH_* from theme.h: restyles the menu and the apps */
bool shell_open(const char *id);                        /* jump straight into an app */
void shell_brightness(int pct);                         /* the screen's brightness, % (10..100): the host sets it from its */
int shell_brightness_get(void);                         /* settings, the Яркость app changes it and the host follows */

/* ---------- for the apps ---------- */
typedef struct {
    const char *id, *name;
    void (*open)(lv_obj_t *scr);
    void (*close)(void);                                  /* may be NULL */
    void (*imu)(float ax, float ay, float az, float dt); /* may be NULL */
    bool (*button)(int id);                               /* may be NULL; true = the app used the side button */
    const char *group;                                    /* NULL: in the menu; else the id of the app that lists it (back goes there) */
    bool with_pip;                                        /* a game with him: while he does not want to play, app_tired opens instead */
} app_t;

extern const app_t apps[];
extern const app_t app_tired; /* "Пип устал": in place of a with_pip game */
extern const int app_total;
void apps_init(void);                  /* background work that outlives an open app (the timer) */
bool apps_alert_on(void);              /* the timer's "time's up" card is on top of everything */
const char *apps_repeat(void);         /* the same for "Повтори за мной" */
void apps_flight_auto(bool on);        /* debug `flauto 1`: the flight game plays itself (a frame-rate bench) */
const char *apps_flight(void);         /* pipsim's status: "points,state" while the flight game is open (0 ready 1 flying 2 over), else "-" */
const char *apps_snake(void);          /* pipsim's status: "len,state" while the snake is open, else "-" */
void apps_alert_close(void);
void shell_home(void);                 /* back to Pip */
void shell_updating(void);             /* "Обновление" over everything, held lit: the panel is about to flash */
void shell_setup(bool on);            /* "РЕЖИМ НАСТРОЙКИ" over everything while the panel is linked: no touch, keys or motion */
int64_t shell_local_time(void);        /* seconds, already shifted to the local zone: feed it to gmtime_r */
void shell_clock_known(bool known);    /* false: no phone or panel has told the time since power-on (the clock app says so) */
bool shell_clock_is_known(void);
void shell_battery(int percent, bool charging); /* from the power chip; -1 = unknown */
int shell_battery_level(bool *charging);        /* -1 = unknown */
const char *shell_battery_text(void);           /* "82%", "82%+" while charging, "--" */

/* The menu's layout, set on the panel: text lines "put <top|block> <id> <id>…" (appended in that order; a block
   is an app with no .open, it lives on the top list only) and "name <id> <text>" (empty text = its own name).
   Apps no line places go where they are by default; a block nothing is put in is not shown. NULL or "" = the
   default menu. Callable before shell_create (main.c loads it from NVS); after it the list is rebuilt at once. */
#define MENU_TEXT_MAX 2048
#define MENU_NAME_MAX 40   /* bytes of UTF-8 */
#define MENU_NAME_CHARS 16 /* letters: the panel's NAME_MAX */
bool shell_menu_set(const char *text);                     /* false: too long; the menu is left as it was */
const char *shell_menu_text(void);                         /* what is set, "" for the default */
void shell_menu_on_change(void (*cb)(const char *text));   /* main.c keeps it in NVS */
int shell_menu_json(char *out, int cap);                   /* [["care","Пип",""],["games","Игры","",[[…]…]]…] */
const char *shell_app_name(int i);                         /* apps[i] as the menu calls it */
const char *shell_title(const char *id);                   /* the app's screen title: its name in the menu (renamed too) */
