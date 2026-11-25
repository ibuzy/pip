/* Pip's needs and day: the tamagotchi part. Pure C, no LVGL; the whole state is one POD struct to persist. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PET_LEVELS 3 /* how much looking after he needs: 0 Неприхотливый, 1 Уравновешенный, 2 Требовательный */
extern int pet_level; /* a setting, not part of pet_t (main.c keeps it in NVS as "care") */
extern bool pet_festive; /* today is the owner's birthday (pip.c, from the real clock only): the day's palette is the great one */
/* how ill he is, 0..1: left filthy (clean under ILL_CLEAN) it creeps up by the hour (pet_tune.ill_h to the full), a chill
   while wet adds to it (pip.c), and clean again it fades over a few hours. Over 0.5 it is his body's mood (why 2.5: sick
   and queasy by turns) and he sneezes more. Not kept across a restart: a filthy Pip falls ill again in its time */
extern float pet_ill;

#define PET_NO_MOOD 255 /* after_mood, rare_mood, cur, prev: none (moods.h has fewer than this) */
#define PET_VERSION 14 /* a save of another version is not read back: he starts anew */

typedef struct {
    uint32_t version;
    int64_t last;     /* unix time this state is valid for */
    int64_t up_until; /* woken up: no dozing off again before this; lulled off (PET_LULLED): no waking by himself before it */
    float food;       /* 1 full .. 0 starving */
    float energy;     /* 1 rested .. 0 exhausted */
    float fun;        /* 1 entertained .. 0 bored */
    float love;       /* attention: 1 cuddled .. 0 lonely */
    float walk;       /* 1 just walked .. 0 cooped up */
    float day_mood;   /* -1..1, rolled every morning (day_base), wandering a little hour by hour */
    float annoy;      /* recent pokes, fades in a couple of minutes */
    int32_t day;      /* local day number day_mood belongs to */
    int32_t steps_today;
    uint32_t rng;
    bool asleep;
    bool bedded;              /* put to bed (face down): no waking up on his own until picked up */
    bool met;                 /* woken for the first time by a button: until then (a clean flash) he lies dormant */
    bool napping;             /* asleep because the screen went dark (not Требовательный): wakes, calmly, when it lights */
    int8_t overfed;           /* offers of food while full, in a row (within pet_tune.disgust_s): 2 and on he refuses */
    /* the mood's memory */
    bool charging;            /* the senses the tree reads, kept up to date by pip.c */
    int8_t bat;               /* %, -1 unknown */
    uint8_t after_mood;       /* an aftertaste of something that happened, until after_until (PET_NO_MOOD: none) */
    int8_t after_why;         /* PET_AFTER_* */
    uint8_t rare_mood;        /* today's rare card, from rare_at for pet_tune.rare_min minutes (PET_NO_MOOD: none today) */
    int8_t pool;              /* the palette shown last: a line must be crossed by pet_tune.hyst to leave it */
    int8_t hold;              /* what has held since hold_since: a battery state or the worst urge (pet_why_t.hold) */
    uint8_t cur, prev;        /* the base mood in 5-minute slot `slot`, and in the slot before: no palette repeats */
    int32_t slot;
    float day_base;
    float stride;             /* recent steps, fading in half a minute: walking now */
    float unseen;             /* hours awake since anyone touched him */
    int64_t after_until, rare_at, hold_since, charge_since, woke_at, stuffed_at;
    int64_t born;             /* unix time he came to be: his age */
    int64_t nap_since;
    int64_t perk_until;       /* shaken or stroked out of his drowsiness: no sleepy face before this */
    int8_t perk_tries;        /* tries at it since the last time it worked: each one makes the next likelier */
    int8_t losses;            /* games he lost in a row lately (a win, or half an hour without a game, clears them) */
    int64_t lost_at;          /* the last one */
    float clean;              /* 1 just washed .. 0 filthy: dust settles by the hour, faster out walking (the 6th need) */
    int32_t rare_day;         /* the day a rare card was last dealt: one a day at most */
    /* what today has been like, for the rare cards (cleared at midnight) */
    int16_t shakes_today, strokes_today;
    int8_t feeds_today, wakes_today;
    int8_t overwashed;        /* washes while clean, in a row (within pet_tune.wash_refuse_s): 2 and on he refuses */
    int64_t washed_at;        /* the last of them */
    /* trust: a trait that builds over weeks, 0 timid .. 1 bold (0.5 to begin with). Rough handling (thrown, dropped,
       pestered, woken, poked a lot) takes it at once; kindness (stroked, fed when hungry, washed when grubby, played
       with) gives it only pet_tune.trust_day a day. Timid he flinches and likes fewer people; bold he says hi himself. */
    float trust, trust_today;
    /* starving: hours his food has been at nothing (faster with no energy or filthy as well); pet_tune.die_h of it and he
       dies (pet_dead). Any meal clears it. */
    float starve_h;
    int8_t night_notes;       /* notifications that came while he slept at night: night_notes of them and he wakes cross */
    float full_h;             /* hours on the charger at 100%: past pet_tune.overcharge_h he is overcharged and it sours him */
    int32_t bday_greeted;     /* the local day he last sang for the owner's birthday: once a day, even across a restart */
    /* what is left of his anger once the face of it has gone, 0 none .. 1 seething: every cross reaction (angry,
       furious, rage... thrown, pestered most) adds to it; it fades by the hour (pet_tune.grudge_fade: faster on a good
       day and asleep, slower for a demanding character and while he is still being annoyed), and kindness (strokes,
       food, a bath, games) only wears it down a little at a time, less the crosser he is. Over grudge_show it shows
       as a sour base mood (why 3.19), over grudge_forgive a stroke does not make up his hurt, past grudge_refuse he may
       turn down food and the sponge, past grudge_play no games ("Пип обиделся"); every line rises on a good day */
    float grudge;
} pet_t;
_Static_assert(sizeof(pet_t) <= 256, "the firmware reads it back into a 256-byte buffer (main.c pet_load)");

enum { PET_FEED, PET_PLAY, PET_STROKE, PET_POKE, PET_STEP, PET_SHAKE, PET_WAKE, PET_SLEEP,
       PET_SEEN,     /* someone touched him or pressed a button: after hours alone he greets or sulks */
       PET_MOVE,     /* a move in a game with him (noughts and crosses): a little fun, a little energy */
       PET_DOZE,     /* the screen went dark on its own: a nap (Неприхотливый, Уравновешенный; Требовательный sleeps face down only) */
       PET_UNDOZE,   /* the screen lit up again: the nap is over, no grudge */
       PET_WIN,      /* he beat you at a game */
       PET_LOSE,     /* you beat him */
       PET_RECORD,   /* a new best score with him */
       PET_FRIGHT,   /* a bang or a drop: afraid for a while */
       PET_FLOOD,    /* a pile of notifications at once */
       PET_FED_UP,   /* poked once too often: the face for how much he has had */
       PET_TICKLE,   /* a few quick taps of the bottom key: tickled, a little fun */
       PET_PESTER,   /* kept tapping it long past funny: angry, and it leaves him hurt (costs love) */
       PET_PEEKABOO, /* face down a moment and straight back (imu.c IMU_PEEK): a game of hide and seek, playful */
       PET_DROPPED,  /* fell (imu.c IMU_FALL): shocked, then hurt a while; a stroke makes up */
       PET_THROWN,   /* thrown (IMU_THROW): angry (worse each time), then hurt longer; it costs love */
       PET_JEALOUS,  /* the phone got all the attention (a pile of notifications, a long song) and he none: pouty a while */
       PET_NIGHT_NOTE, /* a notification while he sleeps at night (pip.c knows the hour): broken sleep, counted for the morning */
       PET_CHASED,   /* a finger chased about the glass long enough to be a game (pip.c chase): playful, once a game */
       PET_UNPLUGGED, /* off the charger (p->bat as it was): left on it full for hours, he sulks; taken off at
                         unplug_lo..unplug_hi %, kind to his battery, he is pleased */
       PET_CABLE_FIDDLE, /* the cable plugged in and out again and again (pip.c counts it): annoyed */
       PET_TUMBLED,      /* turned upside down and back, over and over (pip.c tumble_turn): a game, a little fun */
       PET_LULLED,       /* rocked to sleep, or his eyes closed with a finger (pip.c life): asleep, not put to bed -
                            he wakes on his own once rested */
       PET_DRAINED,      /* the battery ran flat: main.c powers him off (under EMPTY_MV for EMPTY_S, no cable) - food and
                            energy drop by drain_food / drain_energy at once, a little love */
       PET_WOKE_DRAINED }; /* switched on again after that: angry (aftertaste PET_AFTER_DRAINED, a grudge), less trust */
/* What an aftertaste came from */
enum { PET_AFTER_WIN, PET_AFTER_LOSE, PET_AFTER_RECORD, PET_AFTER_FRIGHT, PET_AFTER_FLOOD, PET_AFTER_SULK, PET_AFTER_MISSED,
       PET_AFTER_FED_UP, PET_AFTER_SORE, PET_AFTER_PESTERED, PET_AFTER_WASHED, PET_AFTER_PROUD, PET_AFTER_DROPPED, PET_AFTER_THROWN,
       PET_AFTER_JEALOUS, PET_AFTER_NIGHT, PET_AFTER_OVERCHARGED, PET_AFTER_UNPLUGGED, PET_AFTER_FIDDLED,
       PET_AFTER_GRUDGE, /* not an aftertaste of its own: what is left of his anger (pet_t.grudge), value the grudge */
       PET_AFTER_DRAINED /* woken after his battery ran flat: angry */ };
#define PET_NEEDS 6 /* food, energy, fun, love, walk, clean */
void pet_soil(pet_t *p, float amount); /* dirt from something that happened: a drop, a biscuit's crumbs, a finger */
/* Washed (the bottom key tapped, then held, on his screen): `was` is how clean he was when it began. Glad if he needed it (an
   aftertaste of feeling fresh), indignant and then angry if he did not. */
int pet_washed(pet_t *p, float was);
/* dt s of you busy with the keychain off his face: kind 1 an app or the menu, 2 a game, 3 a game with him - attention
   in units (pet.c attend: love, and fun from games), his character setting how much a day he needs */
void pet_attention(pet_t *p, float dt, int kind);
/* Washed while clean once too often lately (as with food): he will not have the sponge. pet_wash_refused counts the
   try and says how he takes it: "no", then furious, then swearing. */
bool pet_refuses_wash(const pet_t *p);
int pet_wash_refused(pet_t *p);
float pet_dust(const pet_t *p); /* how much dust shows on the glass, 0 (none, until clean drops under dust_line) .. 1 */
/* Rare cards: what each would weigh if dealt now (the season, the hour, the state and what today has been like) */
#define PET_RARE_N 9
extern const signed char pet_rare_cards[PET_RARE_N];
void pet_rare_weights(const pet_t *p, int tz_min, float w[PET_RARE_N]);
int pet_month(int64_t now, int tz_min); /* 1..12 */
void pet_date(int64_t now, int tz_min, int *year, int *month, int *day); /* the local civil date */
int32_t pet_local_day(int64_t now, int tz_min); /* days since 1970 in the local zone */

void pet_init(pet_t *p, int64_t now, int tz_min, uint32_t seed);
/* Advance to `now`, catching up any gap (minutes or days off) in small steps, sleep included. */
void pet_update(pet_t *p, int64_t now, int tz_min);
/* Returns a mood to react with (PIP_*), or -1. */
int pet_event(pet_t *p, int ev);
bool pet_refuses_food(const pet_t *p); /* stuffed and already told you so: the next biscuit is turned away */
/* Still cross deep down (pet_t.grudge): a kindness offered (food: a biscuit, else the sponge) may be turned down - a roll, likelier the crosser he is and the worse his day, less so when he needs it
   (hungry, grubby). Turned down: his face for it (hurt, pouty; angry or belligerent when seething), and it still
   softens him a little. -1: he takes it. */
int pet_spurns(pet_t *p, bool food);
bool pet_sulking(const pet_t *p); /* too cross to play with you: games with him are shut ("Пип обиделся") */
bool pet_coax(pet_t *p, float stroked_s); /* a half second of stroking while he is cross (stroked_s: how long this
                                             stroke has gone on): wears the grudge down faster the longer it lasts;
                                             true when it has just made it up (no longer pet_cold) */
bool pet_cold(const pet_t *p);    /* too cross to warm to kindness: he takes it, but no thanks, no making up */
bool pet_dead(const pet_t *p);         /* forgotten: starved pet_tune.die_h hours. pip.c ends him (pip_dead) */
bool pet_drowsy(const pet_t *p, int tz_min); /* awake but showing a sleepy face a shake or a stroke can chase away */
/* Something done to a drowsy Pip: a shake (PET_SHAKE, the surest), a poke (PET_POKE), half a second of stroking
   (PET_STROKE) or anything else (a meeting, a biscuit, a move in a game: PET_SEEN). Not always the first time, he perks
   up for pet_tune.perk_min minutes. Returns PIP_SURPRISED when he does, else -1. */
int pet_rouse(pet_t *p, int tz_min, int ev);
/* Who he thinks you are when handed over: roll is 0..99. */
int pet_meet(const pet_t *p, int roll, int now_mood); /* now_mood: what he showed when you came; a sour one rules out the warm answers */
bool pet_mood_sour(int mood);                          /* hurt, cross, sad, sick, afraid, sneering...: no mood to like anyone */
/* The base mood for the current needs, day mood and hour. Changes every few minutes for variety. */
int pet_mood(const pet_t *p, int tz_min);
/* The same, saying why. The tree, first match wins:
     0 asleep
     1 a rare card (which: its index in pet_rare_cards; value: minutes left)
     2 the body (which: PET_BODY_*; value: battery % or how many needs are gone)
     3 an aftertaste (which: PET_AFTER_*; value: seconds left)
     4 a need under its line (which: the urge, +16 when its other face shows after a long spell; value: how far under, 0..1)
     5 a tired night (value: energy)
     6 a situation (which: PET_SITU_*)
     7 the day's palette (which: 0 great .. 3 low, +4 in the evening; value: the valence)
   A battery state or a need that has held for pet_tune.stale minutes stops hogging the face: the battery gives every
   other 5 minutes to the rest of the tree, a need turns face, other face, the rest. hold says which one is held
   (kind * 16 + which, -1 none), whether or not it shows right now. */
typedef struct { int kind, which; float value; int hold; } pet_why_t;
enum { PET_WHY_ASLEEP, PET_WHY_RARE, PET_WHY_BODY, PET_WHY_AFTER, PET_WHY_URGE, PET_WHY_NIGHT, PET_WHY_SITU, PET_WHY_POOL };
enum { PET_BODY_EMPTY, PET_BODY_SICK, PET_BODY_LOWBAT, PET_BODY_NERVOUS, PET_BODY_OVERCHARGED, PET_BODY_ILL };
enum { PET_SITU_WAKING, PET_SITU_WALK, PET_SITU_CHARGE };
int pet_mood_why(const pet_t *p, int tz_min, pet_why_t *why);
/* pet_mood, remembering what it showed (the palette for hysteresis, how long an urge has held). Call it often. */
int pet_settle(pet_t *p, int tz_min);
/* A rare card now (mood < 0 or not a card: picked by the weights), for the debug page. */
void pet_rare_now(pet_t *p, int tz_min, int mood);

/* The knobs: each urge is a need (0 food, 1 energy, 2 fun, 3 love, 4 walk, 5 clean) under a line giving a mood, and another
   mood it alternates with when it has held for `stale` minutes; the rest are named in pet_knobs[]. */
#define PET_URGES 10
typedef struct { int need; float line; int mood, alt; } pet_urge_t;
typedef struct {
    pet_urge_t urges[PET_URGES];
    float great, good, meh, hyst;       /* palettes by valence; how far past a line it must go to switch */
    float evening_h;                    /* from this hour the palettes calm down */
    float stale;                        /* min: an urge face this old alternates with its other face */
    float drift;                        /* how far the day's mood wanders each hour */
    float rare_chance, rare_min;        /* a rare card: the chance a day brings one, minutes it lasts */
    float sick_line;                    /* two needs under it at once: sick */
    float bat_low, bat_tired, bat_empty; /* %: nervous, tired, sick */
    float waking_min, walk_now;         /* sleepy after waking up; steps lately that make a walk */
    float away_h, sulk_love;            /* back after hours alone: sulks if love is under sulk_love, glad otherwise */
    float after_win, after_lose, after_record, after_fright, after_flood, after_sulk, after_missed, after_fed_up; /* s */
    float after_proud, hungry_line;     /* s proud after the day's walk goal; fed under hungry_line of food he is grateful */
    float after_dropped, after_thrown;  /* s hurt after a fall, after being thrown */
    float loud_db, loud_jump, shock_db; /* a bang: this loud, this much over the room; shocked over shock_db */
    float flood_n, flood_s;             /* notifications: this many within this many seconds */
    float furious, rage, swear, berserk; /* annoyance (pokes) for each step up from angry when fed up (berserk: beside himself) */
    float cross_good, swear_v, berserk_v; /* ...weighed by his life (pet.c cross): a good day x(1 + cross_good*valence) more of
                                             it; swearing only under swear_v valence, berserk only under berserk_v */
    float meet_like, meet_shy, meet_wary, meet_rude; /* meeting: % chance to like at valence 0 (±35 at ±1); the share of
                                                        likes that come out shy, of dislikes that stay wary; annoyed
                                                        this much a dislike is belligerent */
    float full_line, disgust_s;         /* fed over full_line: nauseous, again within disgust_s: disgusted, then he refuses */
    float play_tired, play_bored;       /* play: refuses under this energy, can't wait under this fun */
    float rewake_min;                   /* woken again this soon after the last time: indignant */
    float perk_shake, perk_poke, perk_stroke, perk_touch, perk_min; /* drowsy: the chance a shake / a poke / half a
                                         second of stroking / anything else rouses him (each miss adds as much again),
                                         and for how many minutes it holds */
    float nap_rest;                     /* a nap (the screen dark on its own) rests him this much of a proper sleep */
    float night_drain;                  /* asleep 23-7 (any sleep: his own, face down, a nap) food, love and the walk go x this */
    /* taps of the bottom key on his screen, each one answered at once; a series is taps under tap_quick s apart.
       From tap_tickle on he is tickled, from tap_annoy he has had enough, at tap_angry he is angry and hurt for
       after_pester s (it costs pester_love), at tap_spin his head spins. Asleep, tap_wake of them wake him (grumpy).
       Slow even taps (tap_quick..tap_slow s apart, within tap_even of each other) are a beat he picks up. */
    float tap_quick, tap_slow, tap_even, tap_tickle, tap_annoy, tap_angry, tap_spin, tap_wake, after_pester, pester_love;
    /* dirt: dust shows under dust_line of clean; games are refused under play_dirty; washing takes wash_rate of clean
       a second; started at wash_clean (0.9: really clean, not just no dust showing - at the dust line nearly every bath
       was "not dirty") or cleaner it only annoys him; after_washed s of feeling fresh */
    float dust_line, play_dirty, wash_rate, wash_clean, after_washed;
    float wash_refuse_s, wash_push; /* clean washes this close count up to refusing; scrubbed clean this long, he flings the sponge */
    float sneeze_poke, sneeze_min;  /* each poke's tickle adds this to the chance of a sneeze (to 0.2, none for a minute after one); on his own about every sneeze_min..3x minutes */
    float burp_dirty, burp_min;     /* grubby (clean under dust_line) he burps more: after a biscuit up to burp_dirty of the
                                       time when filthy (clean 1/4 of the time); on his own every burp_min..3x minutes, each a
                                       roll won by how grubby (so filthy ~2x burp_min apart, half as dirty half as often) */
    float bday_gap, bday_encore; /* the owner's birthday: a burst of confetti every bday_gap..2x minutes all day; this
                                    share of the bursts is the song again */
    float dance_s, dance_rest, dance_break; /* to music: a bout lasts dance_s..1.6x s, then he sits out dance_rest..2x s;
                                               disturbed (a touch, a button, a reaction of his own) he stops for dance_break s */
    float dance_will;                       /* whether he feels like dancing: this + his valence as a chance (a cheerful
                                               mood always, a sore, sad or poorly one never); not now: asked again after a rest */
    float trust_day, trust_flinch, trust_bold; /* trust: kindness gives at most trust_day of it a day; under trust_flinch
                                               he may flinch from a finger or a jolt (the lower, the likelier), over
                                               trust_bold he may say hi himself when picked up or looked at */
    float jealous_s, jealous_chance, jealous_gap, after_jealous; /* the phone's attention without a touch for him: each
                                               notification counts 60 s, its music its own time; past jealous_s, a
                                               jealous_chance roll (at most once in jealous_gap min) makes him pouty
                                               for after_jealous s; a stroke makes up */
    float chase_s;                          /* s of a finger chased about the glass before it is a game (pip.c) */
    float night_notes, after_night;         /* this many notifications in his night's sleep: he wakes angry, for
                                               after_night s (each one also costs a little energy) */
    float drain_food, drain_energy, after_drained; /* the battery run flat: food and energy lost at once (0..1), and how
                                                      long he is angry once switched on again (s) */
    float overcharge_h, after_overcharge;    /* hours on the charger at 100% before it sours him, for Уравновешенный
                                               (Неприхотливый x4/3 = 6 h, Требовательный x2/3 = 3 h: pet.c over_h); annoyed and angry by
                                               turns, sad and hurt past twice that, a little day mood and love lost an
                                               hour; asleep he sleeps on), and how long he sulks (hurt, s) once taken off */
    float unplug_lo, unplug_hi, after_unplug; /* taken off the charger at this much % (kind to the battery): pleased, s */
    float unplug_min, unplug_gap;             /* ...only after a real charge (minutes on it), once in this many hours (pip.c) */
    float fiddle_n, fiddle_s, after_fiddle; /* the cable in this many times within fiddle_s s: annoyed for after_fiddle s
                                               (Неприхотливый x0.7; Требовательный angry, x1.6, and more trust lost);
                                               each plug more: angry, then furious and rage at annoy's furious/rage */
    float grudge_fade, grudge_show, grudge_forgive, grudge_refuse, grudge_play; /* the grudge (pet_t.grudge): how much
                                               of it fades an hour at valence 0 (x0.4 on a dreadful day .. x1.6 on a
                                               great one, x2 asleep; Неприхотливый x1.3, Требовательный x0.7), and the
                                               lines: it shows as his base mood, a stroke no longer makes up, he may
                                               refuse food and washing, no games (each ±0.1 by his day) */
    float coax;                             /* a half second of stroking while he is cross takes this off the grudge
                                               (x1..x3 by how long the stroke has gone on, at least half however cross:
                                               pet_coax) - the way to win him round */
    /* being alive (pip.c life): how much his answers to the same thing vary (0: always the classic one), the mean s
       between things he does of his own accord (0: never), s of gentle rocking that put a worn-out Pip to sleep, s of
       blowing on him before he is cold, s for a wet Pip to dry on his own, hours filthy before he is fully ill */
    float vary, life_gap, rock_s, cold_s, dry_s, ill_h;
    float calm;                             /* 1: a mood that only drifted is reached by degrees (pip.c calm_base); 0: at once */
    /* the everyday faces (adoring, sly, blissful, wistful, impatient...): how often a palette slot is one of them, 0..1
       (0: the classic palettes alone); zones: 1 his cheeks, chin, sides and an eye held answer touch of their own (0: a
       plain poke and stroke there, as before) */
    float everyday, zones;
    /* his talk and his sounds (pip.c, the voice section): how much he tells about what just happened and recalls it later
       (0: never, 1 as he is, 2 a chatterbox); s a reaction holds before a feeling of his own may take its place (an answer
       to you is at once); s for one sound's worth of his voice to come back (0: no budget, every feeling speaks) */
    float talk, hold, snd_refill;
    float die_h;                            /* hours his food stays at nothing before he dies (sooner with no energy
                                               or filthy too: each of those at zero counts the hours half again), for
                                               Уравновешенный; Неприхотливый x0.85 the pace, Требовательный x2 (pet.c starve_pace) */
} pet_tune_t;
extern pet_tune_t pet_tune;
typedef struct { const char *name; float *v; float min, max; } pet_knob_t;
extern const pet_knob_t pet_knobs[];
extern const int pet_knob_count;
float *pet_knob(const char *name); /* NULL if unknown */
float pet_valence(const pet_t *p); /* -1 miserable .. 1 delighted */
int pet_walk_goal(void);             /* the steps of his day's walk, by his character */
float pet_patience(void);           /* his character's patience: 2 Неприхотливый, 1.5 Уравновешенный, 1 Требовательный */
float pet_mood_level(const pet_t *p); /* the same as 0..1 for people: the palette lines at 35%, 60% and 85% */
int pet_hour(int64_t now, int tz_min);
int pet_age_days(const pet_t *p, int tz_min); /* which day of his life it is: 1 on the day he came, 2 the next... */
