/* Pip: the character screen. Portable LVGL code, shared by the simulator and the firmware. */
#pragma once
#include "lvgl.h"
#include <stdbool.h>

#include "moods.h"

enum { PIP_BTN_PWR, PIP_BTN_BOOT };
enum { PIP_ACT_FEED, PIP_ACT_PLAY };

typedef struct {
    const char *mood;     /* what the face shows now */
    const char *reaction; /* "" when none */
    int steps;
    float tilt_x, tilt_y, shake;
    bool face_down;
    float frame_ms; /* face update + render, smoothed */
    /* the pet */
    float food, energy, fun, love, walk, day_mood;
    bool asleep, auto_mood;
    int hour;
    int days; /* which day of his life: 1 on the first */
    float clean;  /* 1 just washed .. 0 filthy */
    bool washing; /* the sponge is on him */
    float trust;  /* 0 timid .. 1 bold: builds over weeks (the care app's last meter) */
    float mood_level; /* 0 miserable .. 1 delighted: how he feels on the whole (pet_mood_level), the care app's first meter */
} pip_info_t;

void pip_create(lv_obj_t *parent);
const char *pip_spiral_note(void);              /* the last touch as the spiral reader saw it (debug link) */
void pip_set_mood(int mood);                      /* pin a mood by hand (turns the automatic one off) */
void pip_auto(bool on);                           /* mood follows the pet's needs and day */
void pip_time(int64_t unix_s, int tz_min);        /* wall clock, call often; the pet catches up any gap */
void pip_action(int act);                         /* PIP_ACT_*: feed (same as pip_feed), play */
void pip_feed(void);                              /* a biscuit drops in and he eats it (held bottom key on his screen) */
void pip_play_move(void);                         /* a move in a game with him: a little fun, a little energy */
void pip_attention(int kind, float dt);           /* dt s of you busy off his face: 1 an app or the menu, 2 a game (pet_attention) */
bool pip_wants_play(void);                        /* awake, not too tired, not filthy: games with him are open */
void pip_wash(bool on);                           /* the bottom key tapped then held on his screen: the sponge scrubs while on */
/* The pet state to persist, and restoring it (false if it is from another version). */
const void *pip_pet_state(size_t *size);
bool pip_pet_restore(const void *data, size_t size);
/* New Year's marks (the day greeted on 1 January, 2..10 January's snowfalls still due): kept beside the pet (NVS "ny"), not
   in pet_t, so the pet's save keeps its size; changed: true once after each change (then saved) */
const void *pip_ny_state(size_t *size);
bool pip_ny_restore(const void *data, size_t size);
bool pip_ny_changed(void);
/* Things he reacts to, each can be switched off (the debug page, filming) */
enum { PIP_F_SHAKE, PIP_F_BLOW, PIP_F_FACEDOWN, PIP_F_VOICE, PIP_F_BATTERY, PIP_F_TIME, PIP_F_NOISE, PIP_F_MUSIC, PIP_F_FALL, PIP_F_WAKE,
       PIP_F_TUMBLE, /* turned over and over: the tumbling mood (the panel's "Перевороты") */
       PIP_F_SNEEZE, PIP_F_BURP, PIP_F_TOOT, /* his body's noises (the panel's "Чихает", "Рыгает", "Пукает" under Болтливость) */
       PIP_F_COUNT }; /* <= 16: NVS "featoff", a u16 of the ones switched off (a new one comes on). PIP_F_WAKE: picked up or
                        shaken, a dark (dozed) screen lights up; off, in the dark motion stirs nothing, not a sound */
void pip_feature(int f, bool on);
bool pip_feature_on(int f);
void *pip_pet(void); /* the live pet_t (pet.h), for the debug page */
void pip_dev(bool on);                    /* developer mode: a wireframe of the eyes and a HUD over the face (for filming) */
bool pip_dev_on(void);
void pip_dev_note(const char *line);      /* the host's line for the HUD: memory, the panel */
void pip_power(int seconds);             /* fall asleep (screen off, no drawing) after this long left alone; 0 = never */
int pip_power_after(void);
void pip_hold_awake(bool on);              /* an app that is there to be looked at keeps him awake */
void pip_covered(bool on);                 /* something opaque lies over his whole face (the setup screen): no drawing */
void pip_away(bool on);                    /* the menu or an app is on screen: he says nothing, pulls no faces, feels no
                                              shake, tilt or fall (steps still count); the shell sets it on every screen change */
int pip_display_level(void);               /* the panel: 0 off, 25 dimmed (about to sleep), 100 */
void pip_chat(int level);                   /* how much he talks (the panel's "Болтливость"): 0 quiet, 1 as he is, 2 a chatterbox */
int pip_chat_level(void);
void pip_eco(bool on);                     /* energy saving: fewer frames, the eyes ignore the tilt, no listening for breath or bangs */
bool pip_eco_on(void);
bool pip_mic_wanted(void);                 /* Pip listens: the host may switch the microphone off when he does not */
void pip_wake(void);                       /* something to look at: wake up */
void pip_wake_up(void);                    /* power-on: eyes open out of black, sleepy, a shake, playful */
bool pip_unmet(void);        /* a clean flash: dormant until a button wakes him for the first time (or still waking) */
void pip_hatch(void);        /* that first button: his birth, a little show; day 1 starts now */
const char *pip_imu_trace(float *energy, float *emax, float *amax, float *shake_t, int *shaking); /* debug `imutrace`: peaks since
                                                                                                    the last call; the reaction on */
void pip_birth_show(float at);
/* debug `touchtrace 1`: "down x y speed(px/s) spread(px) petting tickle_s zone reaction" */
const char *pip_touch_trace(char *out, int n);
void pip_drained(void);       /* main.c: the battery ran flat, it powers off now - food and energy lost (save him after) */
void pip_drained_wake(void);  /* main.c at boot: the last power-off was that - the intro ends angry */   /* debug `birth`: the birth played again for a met Pip (it only wakes him; his age, needs stay) */
/* Dead: forgotten until he starved (pet_dead). Everything of him is wiped, the screen says so and how many days he
   lived, nothing answers. The board keeps it in NVS and calls pip_set_dead at boot: only a clean flash ends it. */
bool pip_dead(void);
int pip_dead_days(void);
void pip_set_dead(int days);
void pip_die(void);
bool pip_sleeping_face_down(void); /* asleep face down: notifications and calls make no sound and light nothing */          /* the debug link's "die": now (a met Pip only) */
void pip_meet_quietly(void); /* met already, no show (pipsim, so runs start with an awake pet) */
void pip_forget(void);       /* a fresh, dormant pet (pipsim event O) */
bool pip_pet_changed(void);  /* true once after something worth saving at once happened (the first meeting) */
void pip_shake_ok(bool ok);                /* false while an app wants the shakes for itself */
void pip_battery(int pct, bool charging); /* plugged in he eats and cheers up; under 10% he is tired */
void pip_pet_rebase(int64_t now); /* the clock jumped (a made-up time replaced by the real one): do not age him for it */
void pip_imu(float ax, float ay, float az, float dt); /* accelerometer in g, device axes, see imu.h */
void pip_tap(void);                               /* a tap of the bottom key on his screen: a hello, and a quick series builds up
                                                     (tickled, enough, angry, a spinning head); a slow even beat starts "repeat the rhythm" */
void pip_meet(void);                            /* hand him to someone: he decides if he likes them */
int pip_lit_why(int *seen);                       /* debug: pip.c's line that last lit the screen (and pip_seen's caller) */
bool pip_drawing(void);                           /* his face is on screen and drawn every frame (the host's full CPU clock) */
bool pip_display_on(void);                        /* false: lying face down, the screen may go dark (nothing is drawn) */
void pip_seen(void);                              /* someone touched him or pressed a button: after hours alone he greets or sulks */
enum { PIP_GAME_WON, PIP_GAME_LOST, PIP_GAME_RECORD };
void pip_game(int result);                        /* PIP_GAME_*, his side: he beat you, you beat him, a new best with him */
enum { PIP_CALL_ANSWERED, PIP_CALL_DECLINED, PIP_CALL_MISSED, PIP_PAIRED };
void pip_phone(int what);                         /* PIP_CALL_*: how a call went, the phone paired */
void pip_note(void);                              /* a new notification: a pile of them at once dismays him */
void pip_get_info(pip_info_t *out);
void pip_care(int level);          /* how much looking after he needs, 0..2 (pet.h PET_LEVELS); panel only */
/* The owner's birthday (the panel's "Дни рождения", off after a flash): day 1..31, month 1..12; 0 0 = off. On that day,
   by the real clock only (pip_clock_known), he sings and pops confetti the first time he is seen awake, and is festive
   all day */
void pip_birthday(int day, int month);
bool pip_birthday_get(int *day, int *month); /* false: off */
void pip_clock_known(bool known);            /* the shell's: a phone or the panel has told the real time since power-on */
int pip_care_get(void);
#define PIP_EYES_COUNT 10
void pip_eyes(int set);            /* eye colours (pupil + white), 0 = standard; the Глаза app or the panel */
int pip_eyes_get(void);
const char *pip_eyes_name(int set);
const char *pip_mood_name(int mood);
int pip_mood_sounds(int mood); /* how many sounds the feeling has (0: none, it keeps quiet) */
const char *pip_mood_say(int mood, int k, bool *babble); /* says the k-th of them plainly; its voice (NULL: no such), babble: his talk, made for this feeling */
/* every finger the touch panel sees, each read (main.c touch_read; pipsim h; the web sim's buttons), before LVGL hears of
   the first: two on his eyes drawn apart or together, two elsewhere, a palm (the panel's flag) */
void pip_fingers(int n, const int16_t *x, const int16_t *y, bool palm);
const char *pip_fingers_trace(char *out, int n); /* debug @F: fingers now, most, the gesture, the spread px, on the eyes */
void pip_react(int mood, float secs, const char *name); /* show a mood for a while, e.g. when the timer rings */
void pip_glance(float x, float y, float secs);         /* look at a screen point for a while (0 secs: stop) */
int pip_find_mood(const char *name); /* -1 if unknown */
/* the slowest frame of the last secs seconds (ms, 0: none drawn), and what he showed then (mood, reaction or "") */
float pip_frame_worst(int secs, const char **mood, const char **reaction);
void pip_blow_info(float *t, int *stage, int *go, float *wet); /* debug: the go of breaths (s), its step, in one, how wet */
void pip_perf_take(int *frames, float *sum_ms, float *max_ms); /* Pip's frames since the last call (debug `perf`), then reset */
void pip_keep_lit(void);
void pip_frame_ready(void);                      /* the firmware's background face frame is done (LVGL's task, under its lock) */                          /* the screen stays on (a benchmark): as if he were just handled */
/* Debug keys: '1'..'9','0','-','=' moods, ' ' next mood, 'b' blink, 'm' meet */
void pip_key(char k);
