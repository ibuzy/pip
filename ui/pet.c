#include "pet.h"
#include "moods.h"
#include <math.h>
#include <string.h>

/* How much looking after he needs (pet_level, set on the panel): per hour, and what a step of a walk is worth.
   A day is ~16 h awake and ~8 h asleep; asleep he gets hungry at half the rate. A biscuit is +0.4 food, a game with
   him +0.35 fun (+0.04 a move), a stroke +0.05 love. So a day costs, and asks for:
     Неприхотливый  food 0.4  - one biscuit;   walk 0.4  - ~800 steps;  fun 0.24 - strokes and snacks will do
     Уравновешенный   food 0.8  - two;           walk 0.76 - ~2000 steps; fun 0.8  - a game or two
     Требовательный food 1.2  - three;         walk 1.1  - ~5000 steps, a proper walk; fun 1.44 - games every day */
/* And his days: mood_bias shifts the morning's roll (-0.35..0.35), walk_goal / walk_min are yesterday's steps that
   lift (+0.2) or sour (-0.15) it, picky scales the lines under which a need shows (energy aside).
   And losing: beaten sore_after times in a row he takes it badly for sore_min minutes - Неприхотливый is sad, the
   balanced one hurt or cross, the demanding one furious or spoiling for a fight - and it costs him sore_cost of fun
   (half that of love), every loss after that too.
   And dirt: dust settles by the hour awake (a quarter of that asleep) and each step out walking kicks some up, crumbs
   and fingers add a little. Dust shows under pet_tune.dust_line (0.55): a day on Неприхотливый and Уравновешенный
   (0.5 / 0.65 a day with their walks) asks for one wash, Требовательный (1.35 with his long walk) for two. */
typedef struct {
    float food, fun, love, love_asleep, walk, walk_asleep, per_step;
    float mood_bias, picky;
    int32_t walk_goal, walk_min;
    int32_t sore_after;
    float sore_min, sore_cost;
    float dirt, dirt_step; /* clean lost per hour awake, per step */
    float patience;        /* how much pestering it takes to make him cross: annoy counts for 1/patience (pet_patience) */
    float play_cost;       /* how much a game with him tires him: x the energy of a move and of a game's end */
} pet_level_t;
static const pet_level_t levels[PET_LEVELS] = {
    {0.02f, 0.015f, 0.03f, 0.01f, 0.02f, 0.01f, 0.0005f, 0.08f, 0.85f, 1500, 200, 5, 3, 0.04f, 0.03f, 0.00003f, 2.0f, 0.45f},
    {0.04f, 0.05f, 0.05f, 0.015f, 0.04f, 0.015f, 0.00038f, 0, 1, 4000, 500, 3, 6, 0.1f, 0.035f, 0.00004f, 1.5f, 0.65f},
    {0.06f, 0.09f, 0.07f, 0.02f, 0.06f, 0.02f, 0.000224f, -0.07f, 1.2f, 6000, 1500, 2, 10, 0.18f, 0.065f, 0.00006f, 1.0f, 0.85f},
};
#define LOSS_SPAN_S (30 * 60) /* a game this long after the last loss starts the count over */
int pet_level = 1;
bool pet_festive;
float pet_ill;
#define ILL_CLEAN 0.2f /* filthier than this, he falls ill by the hour */
#define LV (&levels[pet_level >= 0 && pet_level < PET_LEVELS ? pet_level : 1])
/* He is kind: pestering (pokes, shakes, a finger drawn about, the bottom key drummed) makes him cross slowly, and how
   slowly is his character - Неприхотливый takes twice what Требовательный does, Уравновешенный half as much again: a
   poke or a shake adds 1/patience of annoy, and the first fed-up is only "put out" for each of them. A throw, a fall, the
   cable fiddled with are not pestering: those keep their own ladder (cross). */
float pet_patience(void) { return LV->patience; }
int pet_walk_goal(void) { return LV->walk_goal; }
/* hours full on the charger before he is overcharged: pet_tune.overcharge_h is Уравновешенный's (4.5); Неприхотливый
   bears 6, Требовательный 3 */
static float over_h(void)
{
    static const float f[PET_LEVELS] = {4 / 3.0f, 1, 2 / 3.0f};
    return pet_tune.overcharge_h * f[LV - levels];
}
#define ENERGY_AWAKE 0.06f  /* tired by midnight, whatever his character */
#define LULL_S (20 * 60) /* s: rocked off, or his eyes closed for him, he sleeps at least this, whatever his energy */
#define ENERGY_ASLEEP 0.18f /* asleep face down; a nap with the screen dark rests pet_tune.nap_rest of it */
#define SLOT 300 /* s: a palette mood holds this long */

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static int after(pet_t *p, int mood, float secs, int why);
static int cross(const pet_t *p);
static float valence_core(const pet_t *p);
static void soothe(pet_t *p, float x);
static void attend(pet_t *p, float units, float fun_share);
static float newborn(const pet_t *p);
static void trust_up(pet_t *p, float x);
static void grudge_add(pet_t *p, int mood);
static int bad_night(pet_t *p);
static float clamp1(float v) { return v < -1 ? -1 : v > 1 ? 1 : v; }
static int64_t local(int64_t now, int tz_min) { return now + (int64_t)tz_min * 60; }
int pet_hour(int64_t now, int tz_min) { return (int)((local(now, tz_min) / 3600) % 24); }
static int32_t pet_day(int64_t now, int tz_min) { return (int32_t)(local(now, tz_min) / 86400); }
int pet_age_days(const pet_t *p, int tz_min)
{
    int d = pet_day(p->last, tz_min) - pet_day(p->born, tz_min) + 1;
    return d < 1 ? 1 : d;
}

static uint32_t next(uint32_t *s) /* xorshift32: deterministic, and saved with the pet */
{
    *s ^= *s << 13, *s ^= *s >> 17, *s ^= *s << 5;
    return *s;
}
static float rnd(uint32_t *s, float a, float b) { return a + (b - a) * (next(s) % 10000) / 10000.0f; }

pet_tune_t pet_tune = {
    .urges = {{0, .10f, PIP_ANGRY, PIP_FURIOUS},      {0, .22f, PIP_ANNOYED, PIP_BELLIGERENT}, /* hangry */
              {1, .12f, PIP_EXHAUSTED, PIP_DULL},     {1, .25f, PIP_TIRED, PIP_SLEEPY},
              {3, .10f, PIP_LONELY, PIP_DESPONDENT},  {3, .22f, PIP_HURT, PIP_SAD},
              {2, .12f, PIP_BORED, PIP_CYNICAL},      {4, .10f, PIP_GLOOMY, PIP_NERVOUS}, /* cooped up, restless */
              {5, .12f, PIP_GLOOMY, PIP_SICK},        {5, .30f, PIP_DISGUSTED, PIP_SKEPTICAL}}, /* grubby, then filthy */
    .great = 0.55f, .good = 0.25f, .meh = -0.1f, .hyst = 0.05f, .evening_h = 20,
    .stale = 30, .drift = 0.08f,
    .rare_chance = 0.1f, .rare_min = 20,
    .sick_line = 0.08f,
    .bat_low = 20, .bat_tired = 10, .bat_empty = 4,
    .waking_min = 15, .walk_now = 20,
    .away_h = 4, .sulk_love = 0.35f,
    .after_win = 180, .after_lose = 120, .after_record = 120, .after_fright = 10, .after_flood = 60, .after_sulk = 180,
    .after_missed = 60, .after_fed_up = 60, .after_proud = 300, .hungry_line = 0.35f, .after_dropped = 90, .after_thrown = 240,
    .loud_db = 74, .loud_jump = 25, .shock_db = 88, /* the board: a quiet knock 62..66, a clap 76..84, a loud one 86 (peaks) */
    .flood_n = 3, .flood_s = 10,
    .furious = 5, .rage = 8, .swear = 11, .berserk = 22, .cross_good = 0.5f, .swear_v = 0.1f, .berserk_v = -0.4f,
    .meet_like = 50, .meet_shy = 0.3f, .meet_wary = 0.5f, .meet_rude = 2,
    .full_line = 0.85f, .disgust_s = 120, .play_tired = 0.15f, .play_bored = 0.3f,
    .rewake_min = 5,
    .perk_shake = 0.3f, .perk_poke = 0.12f, .perk_stroke = 0.06f, .perk_touch = 0.08f, .perk_min = 30,
    .nap_rest = 0.3f, .night_drain = 1.3f,
    .tap_quick = 0.5f, .tap_slow = 1.3f, .tap_even = 0.2f, .tap_tickle = 2, .tap_annoy = 4, .tap_angry = 7, .tap_spin = 10, .tap_wake = 3,
    .after_pester = 90, .pester_love = 0.05f,
    .dust_line = 0.55f, .play_dirty = 0.15f, .wash_rate = 0.4f, .wash_clean = 0.9f, .after_washed = 120,
    .wash_refuse_s = 600, .wash_push = 4.5f,
    .sneeze_poke = 0.025f, .sneeze_min = 15, .burp_dirty = 0.8f, .burp_min = 5,
    .dance_s = 20, .dance_rest = 30, .dance_break = 20, .dance_will = 0.6f,
    .bday_gap = 4, .bday_encore = 0.1f,
    .trust_day = 0.03f, .trust_flinch = 0.3f, .trust_bold = 0.7f,
    .jealous_s = 600, .jealous_chance = 0.4f, .jealous_gap = 30, .after_jealous = 120,
    .chase_s = 4.5f, .die_h = 27, .night_notes = 5, .after_night = 1800,
    .drain_food = 0.4f, .drain_energy = 0.4f, .after_drained = 1200,
    .overcharge_h = 4.5f, .after_overcharge = 1800, .unplug_lo = 80, .unplug_hi = 95, .after_unplug = 300,
    .unplug_min = 15, .unplug_gap = 4,
    .fiddle_n = 3, .fiddle_s = 60, .after_fiddle = 180,
    .grudge_fade = 0.4f, .grudge_show = 0.3f, .grudge_forgive = 0.12f, .grudge_refuse = 0.35f, .grudge_play = 0.45f,
    .coax = 0.006f,
    .vary = 1, .calm = 1, .everyday = 1, .zones = 1, .talk = 1, .hold = 0.6f, .snd_refill = 2.2f, .life_gap = 45, .rock_s = 10, .cold_s = 10, .dry_s = 150, .ill_h = 4,
};

#define K(n, lo, hi) {#n, &pet_tune.n, lo, hi}
const pet_knob_t pet_knobs[] = {
    K(great, -1, 1), K(good, -1, 1), K(meh, -1, 1), K(hyst, 0, 0.3f), K(evening_h, 15, 23), K(stale, 5, 180), K(drift, 0, 0.5f),
    K(rare_chance, 0, 1), K(rare_min, 1, 120), K(sick_line, 0, 0.3f),
    K(bat_low, 0, 50), K(bat_tired, 0, 30), K(bat_empty, 0, 15),
    K(waking_min, 0, 60), K(walk_now, 3, 60), K(away_h, 0.5f, 12), K(sulk_love, 0, 1),
    K(after_win, 0, 900), K(after_lose, 0, 900), K(after_record, 0, 900), K(after_fright, 0, 300), K(after_flood, 0, 300),
    K(after_sulk, 0, 900), K(after_missed, 0, 300), K(after_fed_up, 0, 300), K(after_proud, 0, 900), K(hungry_line, 0, 0.8f),
    K(after_dropped, 0, 900), K(after_thrown, 0, 900),
    K(loud_db, 50, 110), K(loud_jump, 5, 50), K(shock_db, 60, 120), K(flood_n, 2, 8), K(flood_s, 5, 120),
    K(furious, 2, 20), K(rage, 3, 25), K(swear, 4, 30), K(berserk, 5, 40), K(cross_good, 0, 3), K(swear_v, -1, 1), K(berserk_v, -1, 1), K(meet_like, -50, 150), K(meet_shy, 0, 1), K(meet_wary, 0, 1), K(meet_rude, 0, 6),
    K(full_line, 0.5f, 1), K(disgust_s, 10, 600), K(play_tired, 0, 0.5f), K(play_bored, 0, 1), K(rewake_min, 0, 30),
    K(perk_shake, 0, 1), K(perk_poke, 0, 1), K(perk_stroke, 0, 1), K(perk_touch, 0, 1), K(perk_min, 1, 120), K(nap_rest, 0, 1), K(night_drain, 1, 3),
    K(tap_quick, 0.2f, 1), K(tap_slow, 0.6f, 2.5f), K(tap_even, 0.05f, 0.5f), K(tap_tickle, 2, 10), K(tap_annoy, 2, 15), K(tap_angry, 3, 20),
    K(tap_spin, 4, 30), K(tap_wake, 1, 10), K(after_pester, 0, 600), K(pester_love, 0, 0.3f),
    K(dust_line, 0.1f, 1), K(play_dirty, 0, 0.5f), K(wash_rate, 0.05f, 2), K(wash_clean, 0.3f, 1), K(after_washed, 0, 600),
    K(wash_refuse_s, 0, 900), K(wash_push, 1, 15), K(sneeze_poke, 0, 0.45f), K(sneeze_min, 1, 240),
    K(burp_dirty, 0.25f, 1), K(burp_min, 0.02f, 120),
    K(dance_s, 3, 300), K(dance_rest, 0, 600), K(dance_break, 0, 300), K(dance_will, -1, 2),
    K(bday_gap, 0.05f, 120), K(bday_encore, 0, 1),
    K(trust_day, 0, 1), K(trust_flinch, 0, 1), K(trust_bold, 0, 1),
    K(jealous_s, 10, 7200), K(jealous_chance, 0, 1), K(jealous_gap, 0, 600), K(after_jealous, 0, 900),
    K(vary, 0, 1), K(calm, 0, 1), K(everyday, 0, 1), K(zones, 0, 1), K(talk, 0, 2), K(hold, 0, 3), K(snd_refill, 0, 20), K(life_gap, 0, 600), K(rock_s, 2, 60), K(cold_s, 1, 30), K(dry_s, 10, 900), K(ill_h, 0.1f, 48),
    K(chase_s, 1, 30), K(die_h, 1, 720), K(night_notes, 1, 50), K(after_night, 0, 7200),
    K(drain_food, 0, 1), K(drain_energy, 0, 1), K(after_drained, 0, 7200),
    K(overcharge_h, 0.05f, 48), K(after_overcharge, 0, 7200), K(unplug_lo, 0, 100), K(unplug_hi, 0, 100), K(after_unplug, 0, 3600),
    K(fiddle_n, 2, 20), K(fiddle_s, 5, 600), K(after_fiddle, 0, 1800), K(unplug_min, 0, 240), K(unplug_gap, 0, 48),
    K(grudge_fade, 0, 10), K(grudge_show, 0, 1), K(grudge_forgive, 0, 1), K(grudge_refuse, 0, 1), K(grudge_play, 0, 1),
    K(coax, 0, 0.1f),
};
#undef K
const int pet_knob_count = sizeof pet_knobs / sizeof pet_knobs[0];

float *pet_knob(const char *name)
{
    for (int i = 0; i < pet_knob_count; i++)
        if (!strcmp(pet_knobs[i].name, name)) return pet_knobs[i].v;
    return NULL;
}

static void new_day(pet_t *p, int32_t day)
{
    /* yesterday's walks set the tone; the rest is just how the day goes */
    const pet_level_t *L = LV;
    float walked = p->steps_today >= L->walk_goal ? 0.2f : p->steps_today < L->walk_min ? -0.15f : 0;
    p->day_base = p->day_mood = clamp1(rnd(&p->rng, -0.35f, 0.35f) + walked + L->mood_bias);
    p->steps_today = 0;
    p->trust_today = 0;
    p->day = day;
    p->shakes_today = p->strokes_today = 0, p->feeds_today = p->wakes_today = 0;
}

/* ---------- rare cards: twenty odd minutes of something odd. Rolled on the hour while he is up (9:00..21:00), one a
   day at most; how likely, and which, comes from the season, the hour and his day: grubby eyes catch pinkeye, a long
   walk in spring brings hay fever, a winter morning freezes him, a summer noon out walking cooks him, a lot of
   cuddles leave him smitten, a lot of shaking hypnotised, biscuit after biscuit hyper, a broken night a zombie, and
   a lazy evening on the charger... stoned. With nothing pointing anywhere, about one in ten days. ---------- */
const signed char pet_rare_cards[PET_RARE_N] = {PIP_STONED, PIP_PINKEYE, PIP_ALLERGIC, PIP_FROZEN, PIP_OVERHEATED,
                                                PIP_SMITTEN, PIP_HYPNOTIZED, PIP_HYPER, PIP_ZOMBIE};

void pet_date(int64_t now, int tz_min, int *year, int *month, int *day) /* Howard Hinnant's days_from_civil, backwards */
{
    int64_t z = local(now, tz_min) / 86400 + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    int m = (int)(mp < 10 ? mp + 3 : mp - 9);
    if (year) *year = (int)(yoe + era * 400 + (m <= 2));
    if (month) *month = m;
    if (day) *day = (int)(doy - (153 * mp + 2) / 5 + 1);
}
int pet_month(int64_t now, int tz_min) { int m; pet_date(now, tz_min, NULL, &m, NULL); return m; }
int32_t pet_local_day(int64_t now, int tz_min) { return pet_day(now, tz_min); }

void pet_rare_weights(const pet_t *p, int tz_min, float w[PET_RARE_N])
{
    int mon = pet_month(p->last, tz_min), hour = pet_hour(p->last, tz_min);
    bool winter = mon == 12 || mon <= 2, summer = mon >= 6 && mon <= 8, green = mon >= 4 && mon <= 8;
    float walked = fminf(2, (float)p->steps_today / (float)LV->walk_goal), grubby = fmaxf(0, 0.55f - p->clean);
    w[0] = 0.4f + (hour >= 19 ? 1.2f : 0) + (p->charging ? 0.8f : 0) + (p->fun > 0.7f && p->energy < 0.5f ? 0.8f : 0); /* stoned */
    w[1] = 0.15f + 8 * grubby;                                                                       /* pinkeye */
    w[2] = 0.1f + walked * (green ? 1.2f : 0.2f) + 2 * grubby;                                       /* allergic */
    w[3] = winter ? 1.5f + (hour < 11 ? 1.5f : 0) : mon == 11 || mon == 3 ? 0.4f : 0;                /* frozen */
    w[4] = summer ? 0.6f + (hour >= 12 && hour < 17 ? 1.2f : 0) + 0.8f * walked + (p->charging ? 0.5f : 0) : 0.03f; /* overheated */
    w[5] = p->love > 0.85f ? 0.6f + fminf(2, p->strokes_today / 25.0f) : 0.05f;                      /* smitten */
    w[6] = 0.1f + fminf(3, p->shakes_today / 6.0f);                                                  /* hypnotized */
    w[7] = 0.1f + fminf(4, 1.2f * fmaxf(0, p->feeds_today - 2));                                     /* hyper */
    w[8] = 0.1f + (p->energy < 0.35f ? 1.5f : 0) + fminf(3, (float)p->wakes_today);                  /* zombie */
}

static int rare_pick(pet_t *p, const float *w)
{
    float total = 0;
    for (int i = 0; i < PET_RARE_N; i++) total += w[i];
    float r = rnd(&p->rng, 0, total);
    for (int i = 0; i < PET_RARE_N; i++)
        if ((r -= w[i]) < 0) return pet_rare_cards[i];
    return pet_rare_cards[0];
}

static void rare_deal(pet_t *p, int mood, int64_t at, int tz_min)
{
    p->rare_mood = (uint8_t)mood, p->rare_at = at, p->rare_day = pet_day(at, tz_min);
}

static void rare_roll(pet_t *p, int64_t t, int tz_min) /* on the hour */
{
    int hour = pet_hour(t, tz_min);
    if (p->asleep || hour < 9 || hour > 21 || p->rare_day == pet_day(t, tz_min)) return;
    float w[PET_RARE_N], total = 0;
    pet_t now = *p;
    now.last = t; /* weighed at this hour, not the start of the step */
    pet_rare_weights(&now, tz_min, w);
    for (int i = 0; i < PET_RARE_N; i++) total += w[i];
    /* ~1.7 is what an ordinary autumn day weighs: then about rare_chance a day; strong pointers make it likelier */
    float chance = fminf(0.35f, pet_tune.rare_chance / 13 * total / 1.7f);
    if (rnd(&p->rng, 0, 1) < chance) rare_deal(p, rare_pick(p, w), t, tz_min);
}

void pet_init(pet_t *p, int64_t now, int tz_min, uint32_t seed)
{
    memset(p, 0, sizeof *p);
    p->version = PET_VERSION;
    p->last = p->born = now;
    p->energy = p->fun = p->love = p->walk = 0.9f;
    p->food = pet_tune.full_line; /* nearly full (85%), yet not over the line: the first biscuit goes down well */
    p->rng = seed ? seed : 0x9E3779B9u;
    p->steps_today = LV->walk_min; /* no grudge on the first day: between the lines for every character */
    p->clean = 1;
    p->rare_day = -1;
    p->trust = 0.5f;
    p->bat = p->pool = p->hold = -1, p->after_mood = p->rare_mood = p->cur = p->prev = PET_NO_MOOD;
    new_day(p, pet_day(now, tz_min));
}

static void advance(pet_t *p, int64_t t, int dt, int tz_min)
{
    float h = dt / 3600.0f;
    int hour = pet_hour(t, tz_min);
    int32_t day = pet_day(t, tz_min);
    bool on_hour = local(t, tz_min) / 3600 != local(t + dt, tz_min) / 3600;
    /* only forward: a time zone flipped back and forth across midnight (the panel's "time", the iPhone's) is not a new
       day each time (a fresh trust_day, a new walk goal, the day's mood rerolled) */
    if (day > p->day) new_day(p, day);
    else if (on_hour) /* on the hour the day's mood wanders, tethered to the morning's */
        p->day_mood = clamp1(p->day_mood + rnd(&p->rng, -pet_tune.drift, pet_tune.drift) + (p->day_base - p->day_mood) * 0.3f);
    if (on_hour) rare_roll(p, t + dt - local(t + dt, tz_min) % 3600, tz_min); /* at the top of the hour */
    p->clean -= LV->dirt * (p->asleep ? 0.25f : 1) * h; /* dust settles; lying still asleep, less */
    if (p->clean < ILL_CLEAN) pet_ill = fminf(1, pet_ill + h / pet_tune.ill_h); /* filthy: he falls ill */
    else if (p->clean > 0.5f) pet_ill = fmaxf(0, pet_ill - h * (p->asleep ? 0.5f : 0.3f)); /* clean: better in a few hours */
    if (p->asleep) {
        const pet_level_t *L = LV;
        float rest = p->napping ? pet_tune.nap_rest : 1; /* dozing with the screen dark is not a real night's sleep */
        /* a real night (23-7 by his clock) costs a little more than a day's nap or lying face down, however he sleeps */
        float nd = hour >= 23 || hour < 7 ? pet_tune.night_drain : 1;
        p->food -= L->food * 0.5f * nd * h, p->energy += ENERGY_ASLEEP * rest * h, p->love -= L->love_asleep * newborn(p) * nd * h;
        p->walk -= L->walk_asleep * nd * h;
        bool day_time = hour >= 7 && hour < 23;
        if (!p->bedded && t >= p->up_until && ((p->energy >= 0.95f && day_time) || (hour >= 9 && hour < 21 && p->energy >= 0.6f)))
            p->asleep = false, p->woke_at = t + dt, bad_night(p); /* after() times it from p->last, the step's start */
    } else {
        const pet_level_t *L = LV;
        float nb = newborn(p); /* a new Pip runs down faster till he trusts you */
        p->food -= L->food * h, p->energy -= ENERGY_AWAKE * h, p->fun -= L->fun * nb * h;
        p->love -= L->love * nb * h, p->walk -= L->walk * h;
        p->unseen += h;
        bool night = hour >= 23 || hour < 7;
        if (((night && p->energy < 0.6f) || p->energy < 0.06f) && t >= p->up_until) p->asleep = true, p->after_mood = PET_NO_MOOD;
    }
    if (p->grudge > 0) { /* what is left of his anger fades: faster on a good day and asleep, slower while still annoyed */
        static const float hold[PET_LEVELS] = {1.3f, 1, 0.7f};
        float v = fmaxf(-1, fminf(1, valence_core(p)));
        float pace = (1 + 0.6f * v) * (p->asleep ? 2 : 1) * hold[LV - levels] / (1 + 0.5f * p->annoy);
        p->grudge = fmaxf(0, p->grudge - pet_tune.grudge_fade * pace * h);
    }
    p->annoy = fmaxf(0, p->annoy - dt / 90.0f);
    p->stride *= expf(-dt / 30.0f);
    /* forgotten: food at nothing, hour after hour; with no energy left or filthy too it goes faster, and the more care his
       character needs the faster still (left with a full belly he lasts ~72 h Неприхотливый, ~47 h Уравновешенный, ~28 h Требовательный). A meal clears it */
    static const float starve_pace[PET_LEVELS] = {0.85f, 1, 2};
    if (p->food <= 0.001f) p->starve_h += h * starve_pace[LV - levels] * (1 + 0.5f * (p->energy <= 0.001f) + 0.5f * (p->clean <= 0.001f));
    else p->starve_h = 0;
    if (p->food < 0.05f) p->trust = clamp01(p->trust - 0.002f * h); /* left hungry: he counts on you less */
    /* left on the charger long after it is full: overcharged. It sours his day and he feels less cared for, hour by hour,
       asleep or not (asleep he sleeps on: the tree shows it once he is up) */
    if (p->charging && p->bat >= 99) {
        p->full_h += h;
        if (p->full_h > over_h()) p->day_mood = clamp1(p->day_mood - 0.05f * h), p->love = clamp01(p->love - 0.02f * h);
    } else if (!p->charging) p->full_h = 0;
    if (p->after_mood != PET_NO_MOOD && t + dt >= p->after_until) p->after_mood = PET_NO_MOOD;
    p->food = clamp01(p->food), p->energy = clamp01(p->energy), p->fun = clamp01(p->fun);
    p->love = clamp01(p->love), p->walk = clamp01(p->walk), p->clean = clamp01(p->clean);
}

void pet_soil(pet_t *p, float amount) { p->clean = clamp01(p->clean - amount); }

/* ---------- the grudge (pet_t.grudge): anger that outlasts its face ---------- */
/* a cross face leaves this much behind: the worse it was, the more */
static void grudge_add(pet_t *p, int mood)
{
    float x = 0;
    switch (mood) {
    case PIP_ANNOYED: x = 0.04f; break;
    case PIP_INDIGNANT: x = 0.05f; break;
    case PIP_ANGRY: x = 0.12f; break;
    case PIP_BELLIGERENT: x = 0.15f; break;
    case PIP_FURIOUS: x = 0.2f; break;
    case PIP_RAGE: x = 0.3f; break;
    case PIP_SWEARING: x = 0.4f; break;
    case PIP_BERSERK: x = 0.55f; break;
    default: return;
    }
    p->grudge = fminf(1, p->grudge + x);
}
/* kindness wears it down, a little at a time: less the crosser he is (seething, a fifth as much), more on a good day */
static void soothe(pet_t *p, float x)
{
    if (p->grudge <= 0) return;
    float v = fmaxf(-1, fminf(1, valence_core(p)));
    float take = fmaxf(0.35f, fminf(1, 1.2f - p->grudge)) * (0.75f + 0.25f * v);
    p->grudge = fmaxf(0, p->grudge - x * take);
}
/* a line moved by his day: a good one takes more to keep him sore (up to +0.1), a dreadful one less (down to -0.1) */
static float grudge_line(const pet_t *p, float line)
{
    return line + 0.1f * fmaxf(-1, fminf(1, valence_core(p)));
}
bool pet_cold(const pet_t *p) { return p->grudge > grudge_line(p, pet_tune.grudge_forgive); }
bool pet_sulking(const pet_t *p) { return p->grudge > grudge_line(p, pet_tune.grudge_play); }

/* Won round: stroked on and on while still cross. Every half second of it takes pet_tune.coax off, more the longer
   the stroking has gone on without a break (x1 at first, x3 from 30 s), and however cross he is it still takes at least
   half: a minute or so of patient stroking makes up even with a furious Pip, half a minute with a sulky one. The moment
   it brings him under grudge_forgive he has made up: the hurt aftertastes go and he gets a little love and trust. */
bool pet_coax(pet_t *p, float stroked_s)
{
    if (p->grudge <= 0) return false;
    bool was = pet_cold(p);
    float ramp = 1 + fminf(stroked_s, 30) / 15, take = fmaxf(0.5f, fminf(1, 1.2f - p->grudge));
    p->grudge = fmaxf(0, p->grudge - pet_tune.coax * ramp * take);
    if (!was || pet_cold(p)) return false;
    switch (p->after_why) {
    case PET_AFTER_SULK: case PET_AFTER_PESTERED: case PET_AFTER_DROPPED: case PET_AFTER_THROWN: case PET_AFTER_JEALOUS:
    case PET_AFTER_SORE: case PET_AFTER_NIGHT: case PET_AFTER_OVERCHARGED: case PET_AFTER_FIDDLED: case PET_AFTER_FED_UP:
    case PET_AFTER_DRAINED: p->after_mood = PET_NO_MOOD; break;
    default: break;
    }
    p->annoy = 0;
    attend(p, 4, 0); /* made up: a long stroke's worth of you */
    trust_up(p, 0.01f);
    return true;
}

int pet_spurns(pet_t *p, bool food)
{
    const pet_tune_t *k = &pet_tune;
    if (p->asleep) return -1;
    float line = grudge_line(p, k->grudge_refuse);
    if (p->grudge <= line) return -1;
    float chance = (p->grudge - line) / fmaxf(0.05f, 1 - line) * 1.3f; /* seething: almost always */
    if (food && p->food < k->hungry_line) chance *= 0.5f; /* hunger is hard to hold out against */
    if (!food && p->clean < k->dust_line) chance *= 0.7f; /* grubby: a bath is tempting */
    if (rnd(&p->rng, 0, 1) >= chance) return -1;
    soothe(p, 0.02f); /* turned down, but he saw you try */
    bool seething = p->grudge > 0.65f, flip = next(&p->rng) % 2;
    return seething ? (flip ? PIP_ANGRY : PIP_BELLIGERENT) : (flip ? PIP_HURT : PIP_POUTY);
}

float pet_dust(const pet_t *p)
{
    float line = pet_tune.dust_line;
    return line > 0 ? clamp01((line - p->clean) / line) : 0;
}

/* Attention, in units: a minute of an app or the menu is 1, of a game 2, of a game with him 3, of stroking 6; a
   biscuit, a tickle, a bath, a game of catch, a game finished are a few each. A day's worth, attn_day by character -
   ~5, ~12 and ~22 minutes of it in a mix (x2.5 units a minute) - makes up for a day's loss of love (16 h awake and 8
   asleep at L->love, L->love_asleep) and, games being half of it, of fun: less and he gets lonely and bored, more is
   to spare. So he is no chore: five minutes a day keeps an easy-going Pip happy, spread however you like. A newborn
   loses both faster (newborn()) until he trusts you. */
static const float attn_day[PET_LEVELS] = {12.5f, 30, 55};
static void attend(pet_t *p, float units, float fun_share)
{
    const pet_level_t *L = LV;
    float u = units / attn_day[L - levels];
    p->love = clamp01(p->love + u * (L->love * 16 + L->love_asleep * 8));
    p->fun = clamp01(p->fun + u * fun_share * 2 * L->fun * 16);
}

/* a new Pip needs a little more of you while he learns to trust you: his love and fun run down x1.5 for his first two
   days, easing to x1 by his seventh - and sooner the more he trusts you (none of it past trust 0.7) */
static float newborn(const pet_t *p)
{
    float days = p->born ? (float)(p->last - p->born) / 86400 : 0;
    float young = days < 2 ? 1 : days > 7 ? 0 : 1 - (days - 2) / 5, unsure = clamp01((0.7f - p->trust) / 0.2f);
    return 1 + 0.5f * young * unsure;
}

void pet_attention(pet_t *p, float dt, int kind)
{
    static const float per_min[4] = {0, 1, 2, 3}; /* an app, a game, a game with him */
    if (p->asleep || !p->met || kind <= 0 || kind > 3) return;
    attend(p, per_min[kind] * dt / 60, kind >= 2 ? 1 : 0);
    p->unseen = 0; /* you are here */
}

int pet_washed(pet_t *p, float was)
{
    const pet_tune_t *k = &pet_tune;
    if (p->after_mood != PET_NO_MOOD && p->after_why == PET_AFTER_FRIGHT) p->after_mood = PET_NO_MOOD; /* a bath calms a fright too (pet_event calm_fright) */
    if (was >= k->wash_clean) { /* he was clean: what was that for? Indignant, then cross, then furious */
        bool again = p->washed_at && p->last - p->washed_at < (int64_t)k->wash_refuse_s;
        p->overwashed = (int8_t)(again ? (p->overwashed < 9 ? p->overwashed + 1 : 9) : 1), p->washed_at = p->last;
        p->annoy += 1.5f, p->love = clamp01(p->love - 0.02f);
        int m = p->overwashed >= 3 || p->annoy >= k->furious ? PIP_FURIOUS : p->overwashed == 2 || p->annoy >= 2 ? PIP_ANGRY : PIP_INDIGNANT;
        grudge_add(p, m);
        return m;
    }
    attend(p, 2, 0.5f); /* a bath */
    soothe(p, 0.05f);
    if (p->clean < 0.9f) return PIP_CONTENT; /* better, not done */
    if (!pet_cold(p)) after(p, PIP_CONTENT, k->after_washed, PET_AFTER_WASHED); /* fresh: it shows a while (not while still sore) */
    p->overwashed = 0;
    return PIP_HAPPY;
}

bool pet_refuses_wash(const pet_t *p)
{
    return p->overwashed >= 2 && p->washed_at && p->last - p->washed_at < (int64_t)pet_tune.wash_refuse_s;
}

int pet_wash_refused(pet_t *p)
{
    p->overwashed = (int8_t)(p->overwashed < 9 ? p->overwashed + 1 : 9), p->washed_at = p->last;
    p->annoy += 1;
    if (p->overwashed == 3) return PIP_NO;                      /* "no" */
    int m = cross(p); /* and it only gets worse */
    m = m == PIP_ANGRY ? PIP_FURIOUS : m;
    grudge_add(p, m);
    return m;
}

void pet_update(pet_t *p, int64_t now, int tz_min)
{
    if (now - p->last > 30LL * 86400) p->last = now - 30LL * 86400; /* away a month: that's enough misery */
    while (p->last < now) {
        int step = (int)fminf((float)(now - p->last), now - p->last > 3600 ? 600 : 60);
        advance(p, p->last, step, tz_min);
        if (pet_festive && !p->asleep && p->day_mood < 0.5f) /* the owner's birthday: his day lifts by itself, ~0.3 an hour */
            p->day_mood = fminf(0.5f, p->day_mood + step * (0.3f / 3600));
        p->last += step;
    }
}

static int after(pet_t *p, int mood, float secs, int why)
{
    if (secs > 0) p->after_mood = (uint8_t)mood, p->after_why = (int8_t)why, p->after_until = p->last + (int64_t)secs;
    return mood;
}

/* trust comes slowly (kindness: at most trust_day of it a day) and goes at once (rough handling) */
static void trust_up(pet_t *p, float x)
{
    float room = pet_tune.trust_day - p->trust_today;
    if (room <= 0) return;
    x = fminf(x, room);
    p->trust_today += x, p->trust = clamp01(p->trust + x);
}
static void trust_down(pet_t *p, float x) { p->trust = clamp01(p->trust - x); }

bool pet_dead(const pet_t *p) { return p->met && p->starve_h >= pet_tune.die_h; }

/* woken (on his own or not) after a night the phone kept buzzing through: cross all morning */
static int bad_night(pet_t *p)
{
    bool bad = p->night_notes >= pet_tune.night_notes;
    p->night_notes = 0;
    return bad ? after(p, PIP_ANGRY, pet_tune.after_night, PET_AFTER_NIGHT) : -1;
}

static void played(pet_t *p)
{
    /* a whole game with him: on top of its minutes, by his character (play_cost): from a night's rest ~20 games in a row on
       Неприхотливый, ~13 Уравновешенный, ~10 Требовательный (it was ~8 for all) */
    attend(p, 3, 1), p->energy = clamp01(p->energy - 0.04f * LV->play_cost);
}

/* drowsy: awake, with a sleepy face that comes from being tired, the night or just having woken (not from the battery
   or real exhaustion); a shake, a poke, a stroke or a biscuit may chase it off for pet_tune.perk_min minutes */
bool pet_drowsy(const pet_t *p, int tz_min)
{
    if (p->asleep) return false;
    pet_why_t w;
    int m = pet_mood_why(p, tz_min, &w);
    if (w.kind == PET_WHY_NIGHT || (w.kind == PET_WHY_SITU && w.which == PET_SITU_WAKING)) return true;
    if (w.kind == PET_WHY_URGE) return pet_tune.urges[w.which & 15].need == 1 && (m == PIP_TIRED || m == PIP_SLEEPY);
    return w.kind == PET_WHY_POOL && m == PIP_SLEEPY;
}

int pet_rouse(pet_t *p, int tz_min, int ev)
{
    if (!pet_drowsy(p, tz_min)) return -1;
    const pet_tune_t *k = &pet_tune;
    float chance = ev == PET_SHAKE ? k->perk_shake : ev == PET_POKE ? k->perk_poke : ev == PET_STROKE ? k->perk_stroke : k->perk_touch;
    if (rnd(&p->rng, 0, 1) >= chance * (1 + p->perk_tries)) { p->perk_tries = (int8_t)(p->perk_tries < 20 ? p->perk_tries + 1 : 20); return -1; }
    p->perk_tries = 0;
    p->perk_until = p->last + (int64_t)(pet_tune.perk_min * 60);
    if (p->up_until < p->perk_until) p->up_until = p->perk_until; /* and no dropping off meanwhile */
    p->woke_at = 0;                                                /* done waking up */
    p->energy = clamp01(p->energy + 0.02f);
    return PIP_SURPRISED;
}

bool pet_refuses_food(const pet_t *p)
{
    return p->overfed >= 2 && p->food > pet_tune.full_line && p->last - p->stuffed_at < (int64_t)pet_tune.disgust_s;
}

static int event(pet_t *p, int ev);
static void calm_fright(pet_t *p) /* any kindness calms a fright at once: no afraid face coming back after it */
{
    if (p->after_mood != PET_NO_MOOD && p->after_why == PET_AFTER_FRIGHT) p->after_mood = PET_NO_MOOD;
}

int pet_event(pet_t *p, int ev)
{
    switch (ev) {
    case PET_FEED: case PET_PLAY: case PET_STROKE: case PET_MOVE: case PET_TICKLE: case PET_PEEKABOO: case PET_CHASED:
    case PET_TUMBLED: case PET_LULLED: calm_fright(p); break;
    }
    int m = event(p, ev);
    grudge_add(p, m); /* a cross face leaves something behind */
    return m;
}

static int event(pet_t *p, int ev)
{
    const pet_tune_t *k = &pet_tune;
    switch (ev) {
    case PET_FEED: { /* full: eats and feels sick, eats and says "enough!", then won't take it ("no"), then gets angry */
        bool stuffed = p->food > k->full_line, again = stuffed && p->stuffed_at && p->last - p->stuffed_at < (int64_t)k->disgust_s;
        if (!again) p->overfed = 0;
        if (stuffed) p->stuffed_at = p->last;
        if (p->overfed >= 2) { /* refused: no food, and it annoys him more each time */
            p->overfed = p->overfed < 9 ? p->overfed + 1 : 9;
            p->annoy += 1;
            if (p->overfed == 3) return PIP_NO;
            /* past "no" there is no telling how he takes it, only that it gets worse: a pick from a pool that darkens */
            static const struct { uint8_t mood, w; } cross[2][4] = {
                {{PIP_ANGRY, 3}, {PIP_INDIGNANT, 2}, {PIP_ANNOYED, 2}, {PIP_FURIOUS, 1}},
                {{PIP_FURIOUS, 3}, {PIP_SWEARING, 3}, {PIP_RAGE, 2}, {PIP_ANGRY, 1}},
            };
            const int row = p->overfed > 4;
            int total = 0;
            for (int i = 0; i < 4; i++) total += cross[row][i].w;
            int r = (int)(next(&p->rng) % (uint32_t)total);
            for (int i = 0; i < 4; i++)
                if ((r -= cross[row][i].w) < 0) return cross[row][i].mood;
            return PIP_ANGRY;
        }
        bool hungry = p->food < k->hungry_line;
        p->food = clamp01(p->food + 0.4f), attend(p, 1, 0.5f);
        p->feeds_today = (int8_t)(p->feeds_today < 100 ? p->feeds_today + 1 : 100);
        pet_soil(p, 0.015f); /* crumbs */
        p->starve_h = 0;
        if (hungry) trust_up(p, 0.008f); /* fed when he needed it: he can count on you */
        soothe(p, hungry ? 0.08f : 0.04f);
        if (!stuffed) return hungry ? PIP_GRATEFUL : PIP_CONTENT; /* a meal when he needed it: thank you */
        return p->overfed++ ? PIP_DISGUSTED : PIP_NAUSEOUS;
    }
    case PET_PLAY: {
        if (p->energy < k->play_tired) return PIP_NO; /* too tired to play */
        if (p->clean < k->play_dirty) return PIP_DISGUSTED; /* too grubby to feel like it: wash him first */
        if (pet_sulking(p)) { soothe(p, 0.02f); return PIP_HURT; } /* still cross with you: not playing */
        bool bored = p->fun < k->play_bored;
        played(p);
        trust_up(p, 0.004f), soothe(p, 0.05f);
        return bored ? PIP_YES : PIP_HAPPY;
    }
    case PET_STROKE:
        attend(p, 6 * 0.5f / 60, 0.1f); /* half a second of stroking: 6 units a minute */
        p->strokes_today = (int16_t)(p->strokes_today < 30000 ? p->strokes_today + 1 : 30000);
        pet_soil(p, 0.002f); /* fingers on the glass */
        trust_up(p, 0.0004f);
        soothe(p, 0.001f); /* half a second of it: a minute of stroking takes ~0.1 off a mild grudge */
        if (!pet_cold(p) && p->after_mood != PET_NO_MOOD && (p->after_why == PET_AFTER_SULK || p->after_why == PET_AFTER_PESTERED || p->after_why == PET_AFTER_DROPPED ||
                                   p->after_why == PET_AFTER_THROWN || p->after_why == PET_AFTER_JEALOUS)) p->after_mood = PET_NO_MOOD; /* made up (still
                                   cross deep down, pet_cold, he is not: it takes time and more kindness) */
        return -1;
    case PET_POKE: p->annoy += 0.35f / LV->patience, p->love = clamp01(p->love - 0.01f), pet_soil(p, 0.001f), trust_down(p, p->annoy > 3 ? 0.001f : 0); return -1; /* a poke is not much: fed up says more */
    case PET_STEP:
        p->steps_today++, p->walk = clamp01(p->walk + LV->per_step), p->fun = clamp01(p->fun + 0.00005f);
        p->stride = fminf(p->stride + 1, 100);
        pet_soil(p, LV->dirt_step); /* out and about: dust and pollen */
        if (p->steps_today == LV->walk_goal) return after(p, PIP_PROUD, k->after_proud, PET_AFTER_PROUD); /* the day's walk: done */
        return -1;
    case PET_SHAKE:
        p->annoy += 0.5f / LV->patience, attend(p, 0.2f, 1);
        p->shakes_today = (int16_t)(p->shakes_today < 30000 ? p->shakes_today + 1 : 30000);
        return -1;
    case PET_WAKE: {
        if (!p->asleep) return -1;
        if (p->energy < 0.7f) p->wakes_today = (int8_t)(p->wakes_today < 100 ? p->wakes_today + 1 : 100); /* before he had slept enough */
        bool again = p->woke_at && p->last - p->woke_at < (int64_t)(k->rewake_min * 60);
        p->asleep = p->bedded = p->napping = false, p->annoy += 1, p->up_until = p->last + 10 * 60, p->woke_at = p->last;
        trust_down(p, again ? 0.01f : 0.004f);
        if (bad_night(p) >= 0) return PIP_ANGRY;
        return again ? PIP_INDIGNANT : p->energy < 0.5f ? PIP_ANNOYED : PIP_SURPRISED;
    }
    case PET_SLEEP: p->asleep = p->bedded = true, p->napping = false, p->after_mood = PET_NO_MOOD; return -1;
    case PET_LULLED: /* rocked off, or his eyes closed for him: a real sleep, he wakes when rested */
        if (p->asleep) return -1;
        p->asleep = true, p->bedded = p->napping = false, p->after_mood = PET_NO_MOOD;
        p->up_until = p->last + LULL_S; /* by day 60% is rested: rocked off at 70% he was up again within a second */
        trust_up(p, 0.003f);
        return -1;
    case PET_DOZE: /* a dark screen is a nap, unless he is a demanding one (or asleep already: that sleep goes on) */
        if (p->asleep || pet_level >= 2) return -1;
        p->asleep = p->bedded = p->napping = true, p->nap_since = p->last;
        return -1;
    case PET_UNDOZE:
        if (!p->napping) return -1;
        p->asleep = p->bedded = p->napping = false;
        bad_night(p);
        if (p->last - p->nap_since > 30 * 60) p->woke_at = p->last - (int64_t)(k->waking_min * 60) + 90; /* a long nap: drowsy a moment */
        return -1;
    case PET_SEEN: {
        float away = p->unseen;
        p->unseen = 0;
        if (p->asleep || away < k->away_h) return -1;
        return p->love < k->sulk_love ? after(p, PIP_HURT, k->after_sulk, PET_AFTER_SULK) /* left alone: sulks */
                                      : (after(p, PIP_HAPPY, k->after_missed, PET_AFTER_MISSED), PIP_LOVE); /* missed you */
    }
    case PET_MOVE: attend(p, 0.2f, 1), p->energy = clamp01(p->energy - 0.01f * LV->play_cost); return -1;
    case PET_WIN: played(p), p->losses = 0; return after(p, PIP_SMUG, k->after_win, PET_AFTER_WIN);
    case PET_LOSE: { /* "no way!", then sulks; beaten again and again, he takes it badly */
        played(p);
        if (p->lost_at && p->last - p->lost_at > LOSS_SPAN_S) p->losses = 0;
        p->losses = (int8_t)(p->losses < 100 ? p->losses + 1 : 100), p->lost_at = p->last;
        const pet_level_t *L = LV;
        if (p->losses < L->sore_after) { after(p, PIP_INDIGNANT, k->after_lose, PET_AFTER_LOSE); return PIP_NO; }
        p->fun = clamp01(p->fun - L->sore_cost), p->love = clamp01(p->love - L->sore_cost / 2);
        bool flip = next(&p->rng) % 2;
        int m = pet_level <= 0 ? PIP_SAD : pet_level == 1 ? (flip ? PIP_HURT : PIP_ANGRY) : (flip ? PIP_FURIOUS : PIP_BELLIGERENT);
        return after(p, m, L->sore_min * 60, PET_AFTER_SORE);
    }
    case PET_RECORD: return after(p, PIP_AMAZED, k->after_record, PET_AFTER_RECORD);
    case PET_FRIGHT: after(p, PIP_AFRAID, k->after_fright, PET_AFTER_FRIGHT); return -1;
    case PET_FLOOD: return after(p, PIP_DISMAYED, k->after_flood, PET_AFTER_FLOOD);
    case PET_FED_UP: {
        trust_down(p, 0.005f);
        /* up the ladder one rung a fed-up at most: put out first (whatever his character), then angry, furious, rage,
           swearing, berserk - as far as his annoyance (cross) allows, never two at once; ten quiet minutes and it starts
           over. How soon each fed-up comes is his patience (pip.c POKE_FED_UP, a poke's annoy here) */
        static const int rungs[] = {PIP_ANNOYED, PIP_ANGRY, PIP_FURIOUS, PIP_RAGE, PIP_SWEARING, PIP_BERSERK};
        static int64_t fed_at;
        static int rung;
        pet_t calm = *p; /* pestering, not mistreatment: what it has built up counts 1/patience on this ladder */
        calm.annoy /= LV->patience;
        int top = 0, c = cross(&calm);
        while (top < 5 && rungs[top] != c) top++;
        rung = p->last - fed_at > 600 || p->annoy < 2.5f * LV->patience ? 0 : rung + 1 < top ? rung + 1 : top;
        fed_at = p->last;
        int m = rungs[rung];
        after(p, PIP_ANNOYED, k->after_fed_up, PET_AFTER_FED_UP); /* it takes a minute to calm down */
        return m;
    }
    case PET_TICKLE: attend(p, 0.5f, 1), soothe(p, 0.01f); return -1;
    case PET_DROPPED: /* a fright, and a grudge: you let him fall */
        p->annoy += 1, p->love = clamp01(p->love - 0.03f);
        trust_down(p, 0.01f);
        after(p, PIP_HURT, k->after_dropped, PET_AFTER_DROPPED);
        return PIP_SHOCKED;
    case PET_THROWN: { /* that was on purpose: cross, more so each time, and hurt for a good while */
        p->annoy += 3.5f, p->love = clamp01(p->love - 0.08f), p->fun = clamp01(p->fun - 0.03f);
        trust_down(p, 0.04f);
        p->grudge = fminf(1, p->grudge + 0.2f); /* on purpose: that is not forgotten with the face */
        int m = cross(p);
        after(p, PIP_HURT, k->after_thrown, PET_AFTER_THROWN);
        return m;
    }
    case PET_PEEKABOO:
        if (p->asleep) return -1;
        attend(p, 0.5f, 1), soothe(p, 0.01f);
        return PIP_PLAYFUL;
    case PET_PESTER: { /* cross for how much he has had, then hurt a while: a stroke makes it up */
        p->annoy += 2, p->love = clamp01(p->love - k->pester_love);
        trust_down(p, 0.02f);
        p->grudge = fminf(1, p->grudge + 0.12f);
        int m = p->annoy < k->furious ? PIP_ANGRY : PIP_FURIOUS;
        after(p, PIP_HURT, k->after_pester, PET_AFTER_PESTERED);
        return m;
    }
    case PET_JEALOUS: /* the phone again, and not a look for him: pouty, a little less loved; a stroke makes up */
        p->love = clamp01(p->love - 0.03f);
        return after(p, PIP_POUTY, k->after_jealous, PET_AFTER_JEALOUS);
    case PET_NIGHT_NOTE:
        if (!p->asleep) return -1;
        p->energy = clamp01(p->energy - 0.01f);
        p->night_notes = (int8_t)(p->night_notes < 100 ? p->night_notes + 1 : 100);
        return -1;
    case PET_CHASED: /* a game of catch with your finger */
        played(p), trust_up(p, 0.004f), soothe(p, 0.03f);
        return PIP_PLAYFUL;
    case PET_UNPLUGGED: { /* off the charger: how long it was kept full, or how kindly early */
        bool over = p->full_h >= over_h();
        p->full_h = 0;
        if (over) {
            p->love = clamp01(p->love - 0.03f);
            return after(p, PIP_HURT, k->after_overcharge, PET_AFTER_OVERCHARGED);
        }
        if (p->bat < k->unplug_lo || p->bat > k->unplug_hi) return -1;
        p->day_mood = clamp1(p->day_mood + 0.05f), attend(p, 1.5f, 1);
        return after(p, PIP_HAPPY, k->after_unplug, PET_AFTER_UNPLUGGED);
    }
    case PET_DRAINED: /* run flat (main.c, the moment before it powers off): hungry and worn out at once, a little hurt */
        p->food = clamp01(p->food - k->drain_food), p->energy = clamp01(p->energy - k->drain_energy);
        p->love = clamp01(p->love - 0.05f), p->full_h = 0;
        return -1;
    case PET_WOKE_DRAINED: /* switched on after it: angry a while (a grudge after it: pet_event), less sure of you */
        trust_down(p, 0.02f);
        p->day_mood = clamp1(p->day_mood - 0.1f);
        return after(p, PIP_ANGRY, k->after_drained, PET_AFTER_DRAINED);
    case PET_TUMBLED: /* round and round: fun, a touch of trust */
        attend(p, 0.5f, 1), trust_up(p, 0.002f);
        return PIP_TUMBLING;
    case PET_CABLE_FIDDLE: { /* in, out, in, out: it annoys every character, a demanding one most - angry, for longer; each
                                plug more of it (annoy fades a point in 90 s) makes him crosser: angry, furious, then rage */
        static const struct { int mood; float secs, trust, annoy; } f[PET_LEVELS] = {
            {PIP_ANNOYED, 0.7f, 0.005f, 1.5f}, {PIP_ANNOYED, 1, 0.01f, 2}, {PIP_ANGRY, 1.6f, 0.02f, 2.5f}};
        const int l = (int)(LV - levels);
        p->annoy += f[l].annoy;
        trust_down(p, f[l].trust);
        int m = cross(p);
        if (m == PIP_ANGRY && p->annoy < 3.5f) m = f[l].mood; /* the first plugs: as his character takes it */
        p->love = clamp01(p->love - (m == PIP_BERSERK ? 0.05f : m == PIP_SWEARING ? 0.04f : m == PIP_RAGE ? 0.03f : m == PIP_FURIOUS ? 0.015f : 0));
        p->day_mood = clamp1(p->day_mood - 0.02f);
        return after(p, m, k->after_fiddle * f[l].secs, PET_AFTER_FIDDLED);
    }
    }
    return -1;
}

bool pet_mood_sour(int mood)
{
    switch (mood) {
    case PIP_SAD: case PIP_ANGRY: case PIP_OUCH: case PIP_PINKEYE: case PIP_NAUSEOUS: case PIP_SICK: case PIP_EXHAUSTED:
    case PIP_ANNOYED: case PIP_FURIOUS: case PIP_RAGE: case PIP_SWEARING: case PIP_BERSERK: case PIP_INDIGNANT: case PIP_BELLIGERENT: case PIP_HURT:
    case PIP_DISGUSTED: case PIP_SUSPICIOUS: case PIP_CYNICAL: case PIP_AFRAID: case PIP_NERVOUS: case PIP_DESPONDENT:
    case PIP_LONELY: case PIP_GLOOMY: case PIP_DULL: case PIP_SHOCKED: case PIP_DISMAYED: case PIP_NO:
    case PIP_ALLERGIC: case PIP_FROZEN: case PIP_OVERHEATED: case PIP_ZOMBIE: case PIP_POUTY: case PIP_EYEROLL: return true;
    default: return false;
    }
}

int pet_meet(const pet_t *p, int roll, int now_mood)
{
    /* a happy Pip likes almost everyone, a miserable one almost no one; caught in a sour mood (sulking, cross, sad...)
       the best you get is a sceptical look: no love, no blushing */
    const pet_tune_t *k = &pet_tune;
    int like = (int)(k->meet_like + 35 * pet_valence(p) + 40 * (p->trust - 0.5f)); /* a timid one warms to fewer */
    if (roll < like) return pet_mood_sour(now_mood) ? PIP_SKEPTICAL : roll < like * (1 - k->meet_shy) ? PIP_LOVE : PIP_SHY;
    if (p->annoy >= k->meet_rude) return PIP_BELLIGERENT;
    return roll < like + (100 - like) * k->meet_wary ? PIP_SUSPICIOUS : PIP_ANGRY;
}

void pet_rare_now(pet_t *p, int tz_min, int mood)
{
    for (int i = 0; i < PET_RARE_N; i++)
        if (pet_rare_cards[i] == mood) { rare_deal(p, mood, p->last, tz_min); return; }
    float w[PET_RARE_N];
    pet_rare_weights(p, tz_min, w);
    rare_deal(p, rare_pick(p, w), p->last, tz_min);
}

static float valence_core(const pet_t *p) /* how good life is, before the annoyance of the moment */
{
    float avg = (p->food + p->energy + p->fun + p->love + p->walk) / 5;
    float grime = fmaxf(0, 0.5f - p->clean) * 0.8f; /* being clean is no joy in itself; grubby, it weighs on him */
    return (avg - 0.5f) * 2 + p->day_mood * 0.5f - grime;
}
float pet_valence(const pet_t *p) { return fmaxf(-1, fminf(1, valence_core(p) - p->annoy * 0.1f)); }

/* his mood on the whole as a meter, never below zero: piecewise through the palettes' lines, so "so-so" sits a third of the
   way up, "good" past the middle and "great" near the top; his birthday reads great at least */
float pet_mood_level(const pet_t *p)
{
    const pet_tune_t *k = &pet_tune;
    const float x[5] = {-1, k->meh, k->good, k->great, 1}, y[5] = {0, 0.35f, 0.6f, 0.85f, 1};
    float v = pet_valence(p), m = 0;
    for (int i = 0; i < 4; i++)
        if (v <= x[i + 1] || i == 3) { m = y[i] + (y[i + 1] - y[i]) * fmaxf(0, fminf(1, (v - x[i]) / (x[i + 1] - x[i]))); break; }
    return pet_festive && m < 0.85f ? 0.85f : m;
}

/* How cross he gets for the annoyance he has had (annoy): angry, furious, rage, swearing, berserk. His life as a whole
   weighs on it: on a good day it takes more (annoy counts for less, cross_good), swearing only comes on a poor day
   (valence under swear_v) and berserk only on a dreadful one (under berserk_v) - clicking at a happy Pip tops out at rage */
static int cross(const pet_t *p)
{
    const pet_tune_t *k = &pet_tune;
    float v = fmaxf(-1, fminf(1, valence_core(p))), a = p->annoy / (1 + k->cross_good * fmaxf(0, v));
    if (a < k->furious) return PIP_ANGRY;
    if (a < k->rage) return PIP_FURIOUS;
    if (a < k->swear || v > k->swear_v) return PIP_RAGE;
    if (a < k->berserk || v > k->berserk_v) return PIP_SWEARING;
    return PIP_BERSERK;
}

int pet_mood(const pet_t *p, int tz_min) { return pet_mood_why(p, tz_min, NULL); }

/* A palette: moods with weights, the rarer ones light */
typedef struct { uint8_t mood, weight; } pick_t;
static int pick(const pick_t *pool, int n, uint32_t h)
{
    int total = 0, i = 0;
    for (int j = 0; j < n; j++) total += pool[j].weight;
    if (total <= 0) return 0; /* all weighed out: the first */
    for (int r = (int)(h % (uint32_t)total); r >= pool[i].weight; i++) r -= pool[i].weight;
    return i;
}
static uint32_t slot_hash(const pet_t *p, int64_t slot) { return ((uint32_t)slot * 2654435761u ^ p->rng) >> 7; }

static int palette(float v)
{
    return v > pet_tune.great ? 0 : v > pet_tune.good ? 1 : v > pet_tune.meh ? 2 : 3;
}

static int out(pet_why_t *why, int kind, int which, float value, int hold, int mood)
{
    *why = (pet_why_t){kind, which, value, hold};
    return mood;
}

static int mood_tree(const pet_t *p, int tz_min, pet_why_t *why);
/* the owner's birthday (pet_festive): whatever the tree says, never sad or cross that day - a sour face turns into a
   calm, mildly glad one (he need not be over the moon). Reactions to what you do are pip.c's and still show */
int pet_mood_why(const pet_t *p, int tz_min, pet_why_t *why)
{
    pet_why_t w;
    if (!why) why = &w;
    int m = mood_tree(p, tz_min, why);
    if (!pet_festive || !pet_mood_sour(m)) return m;
    static const pick_t mild[4] = {{PIP_CONTENT, 3}, {PIP_COMFY, 2}, {PIP_HAPPY, 1}, {PIP_CURIOUS, 1}};
    int total = 7, r = (int)(slot_hash(p, p->last / SLOT) % (uint32_t)total), i = 0;
    while (r >= mild[i].weight) r -= mild[i++].weight;
    return mild[i].mood;
}

static int mood_tree(const pet_t *p, int tz_min, pet_why_t *why)
{
    pet_why_t w;
    if (!why) why = &w;
    if (p->asleep) return out(why, PET_WHY_ASLEEP, 0, 0, -1, PIP_ASLEEP);
    const pet_tune_t *k = &pet_tune;
    int64_t now = p->last, slot = now / SLOT;
    int hour = pet_hour(now, tz_min), hold = -1;
    bool perked = now < p->perk_until; /* shaken or stroked awake: no drowsy face for a while */
#define HELD (p->hold == hold && now - p->hold_since > (int64_t)(k->stale * 60))

    /* a rare card: twenty minutes of something odd, about once a month */
    int64_t rare_end = p->rare_at + (int64_t)(k->rare_min * 60);
    if (p->rare_mood != PET_NO_MOOD && now >= p->rare_at && now < rare_end) {
        int card = 0;
        while (card < PET_RARE_N - 1 && pet_rare_cards[card] != p->rare_mood) card++;
        return out(why, PET_WHY_RARE, card, (rare_end - now) / 60.0f, -1, p->rare_mood);
    }

    /* the body: the battery is his blood sugar. Low for long, it leaves every other 5 minutes to the rest */
    if (p->charging && p->bat >= 99 && p->full_h >= over_h()) { /* kept on the charger full for hours */
        bool long_ = p->full_h >= 2 * over_h(); /* cross first, then it gets him down */
        int m = long_ ? (slot % 2 ? PIP_SAD : PIP_HURT) : (slot % 2 ? PIP_ANNOYED : PIP_ANGRY);
        return out(why, PET_WHY_BODY, PET_BODY_OVERCHARGED, p->full_h, -1, m);
    }
    if (pet_ill > 0.5f) /* ill: sick and queasy by turns */
        return out(why, PET_WHY_BODY, PET_BODY_ILL, pet_ill, -1, slot % 2 ? PIP_NAUSEOUS : PIP_SICK);
    bool bat = p->bat >= 0 && !p->charging;
    if (bat && p->bat < k->bat_empty) return out(why, PET_WHY_BODY, PET_BODY_EMPTY, p->bat, -1, PIP_SICK);
    if (bat && p->bat < k->bat_low) {
        int b = p->bat < k->bat_tired ? PET_BODY_LOWBAT : PET_BODY_NERVOUS; /* running on empty, or getting there */
        hold = PET_WHY_BODY * 16 + b;
        if (!HELD || slot % 2 == 0) return out(why, PET_WHY_BODY, b, p->bat, hold, b == PET_BODY_LOWBAT ? PIP_TIRED : PIP_NERVOUS);
    }

    /* something that happened lately still shows */
    if (p->after_mood != PET_NO_MOOD && now < p->after_until)
        return out(why, PET_WHY_AFTER, p->after_why, (float)(p->after_until - now), hold, p->after_mood);

    /* the face of his anger has gone, not all of it: sore and wary by turns, cross while still seething */
    if (p->grudge > grudge_line(p, k->grudge_show)) {
        int m = p->grudge > 0.65f ? (slot % 2 ? PIP_ANNOYED : PIP_INDIGNANT) : (slot % 2 ? PIP_HURT : PIP_SUSPICIOUS);
        return out(why, PET_WHY_AFTER, PET_AFTER_GRUDGE, p->grudge, hold, m);
    }

    /* the most pressing need wins, measured by how far it is under its line. Needs about as bad take turns every
       5 minutes and two gone at once make him ill now and then; held long, the need shows its other face, then
       leaves a turn to the rest of the tree */
    const pet_urge_t *urges = k->urges;
    const float needs[PET_NEEDS] = {p->food, p->energy, p->fun, p->love, p->walk, p->clean};
    float sev[PET_URGES], worst = 0;
    int u = -1, gone = 0, turns[PET_URGES], n = 0;
    for (int i = 0; i < PET_NEEDS; i++) gone += needs[i] < k->sick_line;
    float line[PET_URGES]; /* the character moves every line but tiredness's */
    for (int i = 0; i < PET_URGES; i++) line[i] = urges[i].line * (urges[i].need == 1 ? 1 : LV->picky);
    for (int i = 0; i < PET_URGES; i++) {
        sev[i] = (line[i] - needs[urges[i].need]) / line[i];
        if (perked && urges[i].need == 1 && (urges[i].mood == PIP_TIRED || urges[i].mood == PIP_SLEEPY)) sev[i] = 0;
        for (int j = 0; j < PET_URGES; j++) /* of two lines on one need, the deeper one speaks */
            if (urges[j].need == urges[i].need && line[j] < line[i] && needs[urges[j].need] < line[j]) sev[i] = 0;
        if (sev[i] > worst) worst = sev[i], u = i;
    }
    for (int i = 0; i < PET_URGES && u >= 0; i++)
        if (sev[i] > 0 && sev[i] >= worst - 0.1f) turns[n++] = i;
    int m = n + (gone >= 2), turn = m ? (int)(slot % m) : 0;
    if (m) {
        if (hold < 0) hold = PET_WHY_URGE * 16 + (u >= 0 ? u : 15);
        int phase = HELD ? (int)(slot / m % 3) : 0; /* the face, the other face, the rest */
        if (phase < 2 && turn == n) return out(why, PET_WHY_BODY, PET_BODY_SICK, (float)gone, hold, PIP_SICK);
        if (phase < 2) {
            int i = turns[turn];
            return out(why, PET_WHY_URGE, i + 16 * phase, sev[i], hold, phase ? urges[i].alt : urges[i].mood);
        }
    }
#undef HELD
    if ((hour >= 22 || hour < 6) && p->energy < 0.5f && !perked) return out(why, PET_WHY_NIGHT, 0, p->energy, hold, PIP_SLEEPY);

    /* what he is doing: still waking up, out on a walk, on the charger */
    if (p->woke_at && now - p->woke_at < (int64_t)(k->waking_min * 60) && !perked) return out(why, PET_WHY_SITU, PET_SITU_WAKING, 0, hold, PIP_SLEEPY);
    if (p->stride > k->walk_now) return out(why, PET_WHY_SITU, PET_SITU_WALK, p->stride, hold, slot % 2 ? PIP_AMAZED : PIP_CURIOUS);
    if (p->charging && now - p->charge_since > 60) return out(why, PET_WHY_SITU, PET_SITU_CHARGE, 0, hold, PIP_COMFY);

    /* otherwise, how good life is picks the palette (it sticks a little: no flicking across a line), the evening calms
       it down, and the clock picks a mood from it every 5 minutes, never the same twice running */
#define POOL_N 6
    static const pick_t pools[8][POOL_N] = {
        {{PIP_HAPPY, 3}, {PIP_CHEEKY, 2}, {PIP_CURIOUS, 2}, {PIP_AMAZED, 2}, {PIP_CONTENT, 2}, {PIP_SMUG, 1}},
        {{PIP_CONTENT, 3}, {PIP_COMFY, 2}, {PIP_CURIOUS, 2}, {PIP_AMAZED, 1}, {PIP_NEUTRAL, 2}, {PIP_CHEEKY, 1}},
        {{PIP_NEUTRAL, 3}, {PIP_SKEPTICAL, 2}, {PIP_CURIOUS, 2}, {PIP_SUSPICIOUS, 2}, {PIP_BORED, 1}, {PIP_CYNICAL, 1}},
        {{PIP_SAD, 3}, {PIP_DESPONDENT, 2}, {PIP_GLOOMY, 2}, {PIP_CYNICAL, 1}, {PIP_DISMAYED, 1}, {PIP_HURT, 1}},
        /* the evening */
        {{PIP_HAPPY, 2}, {PIP_CONTENT, 3}, {PIP_COMFY, 2}, {PIP_CHEEKY, 1}, {PIP_SMUG, 1}, {PIP_LOVE, 1}},
        {{PIP_COMFY, 3}, {PIP_CONTENT, 3}, {PIP_NEUTRAL, 2}, {PIP_SLEEPY, 1}, {PIP_CURIOUS, 1}, {PIP_SKEPTICAL, 1}},
        {{PIP_NEUTRAL, 3}, {PIP_BORED, 2}, {PIP_SKEPTICAL, 2}, {PIP_SLEEPY, 1}, {PIP_CYNICAL, 1}, {PIP_SUSPICIOUS, 1}},
        {{PIP_SAD, 3}, {PIP_GLOOMY, 3}, {PIP_DESPONDENT, 2}, {PIP_LONELY, 1}, {PIP_TIRED, 1}, {PIP_CYNICAL, 1}},
    };
    /* the everyday faces of each palette: about a third of the slots (pet_tune.everyday) are one of these - seen every day */
    static const pick_t daily[8][2] = {
        {{PIP_ADORING, 1}, {PIP_SLY, 1}}, {{PIP_SLY, 1}, {PIP_BLISSFUL, 1}}, {{PIP_IMPATIENT, 2}, {PIP_WISTFUL, 1}}, {{PIP_WISTFUL, 1}, {PIP_LONELY, 0}},
        {{PIP_BLISSFUL, 2}, {PIP_ADORING, 1}}, {{PIP_WISTFUL, 1}, {PIP_BLISSFUL, 1}}, {{PIP_WISTFUL, 2}, {PIP_IMPATIENT, 1}}, {{PIP_WISTFUL, 1}, {PIP_LONELY, 0}},
    };
    float v = pet_valence(p);
    int pal = pet_festive ? 0 : palette(v); /* the owner's birthday: a great day, whatever else */
    if (!pet_festive && p->pool >= 0 && p->pool < 4 && pal != p->pool && palette(v + k->hyst) <= p->pool && palette(v - k->hyst) >= p->pool) pal = p->pool;
    int which = pal + (hour >= (int)k->evening_h ? 4 : 0);
    const pick_t *pool = pools[which];
    uint32_t h = slot_hash(p, slot);
    if (k->everyday > 0 && (h >> 9) % 1000 < (uint32_t)(340 * k->everyday)) { /* one of the day's everyday faces */
        int d = daily[which][1].weight && (h >> 3) % (uint32_t)(daily[which][0].weight + daily[which][1].weight) >= daily[which][0].weight;
        int before = slot == p->slot ? p->prev : p->cur, m = daily[which][d].mood;
        if (m != before || !daily[which][1 - d].weight) return out(why, PET_WHY_POOL, which, v, hold, m == before ? pool[0].mood : m);
        return out(why, PET_WHY_POOL, which, v, hold, daily[which][1 - d].mood);
    }
    int i = pick(pool, POOL_N, h), before = slot == p->slot ? p->prev : p->cur;
    if (pool[i].mood == before) i = (i + 1) % POOL_N;
    if (perked && pool[i].mood == PIP_SLEEPY) i = (i + 1) % POOL_N;
    if (pool[i].mood == before) i = (i + 1) % POOL_N;
    return out(why, PET_WHY_POOL, which, v, hold, pool[i].mood);
}

int pet_settle(pet_t *p, int tz_min)
{
    pet_why_t w;
    int m = pet_mood_why(p, tz_min, &w);
    int32_t slot = (int32_t)(p->last / SLOT);
    if (slot != p->slot) p->prev = p->cur, p->slot = slot;
    p->cur = (uint8_t)m;
    if (w.kind == PET_WHY_POOL) p->pool = (int8_t)(w.which & 3);
    if (w.hold != p->hold) p->hold = (int8_t)w.hold, p->hold_since = p->last;
    return m;
}
