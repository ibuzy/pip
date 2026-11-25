/* Pet logic checks over simulated days: every branch of the mood tree. Run by sim/check.py. */
#include "moods.h"
#include "pet.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", msg); exit(1); } } while (0)
#define H 3600LL
static const int64_t NOON = 1791633600; /* 2026-10-10 12:00 UTC */

static pet_t fresh(void)
{
    pet_t p;
    pet_init(&p, NOON, 0, 42);
    p.day_mood = p.day_base = 0.2f;
    return p;
}
static int why_kind(const pet_t *p)
{
    pet_why_t w;
    pet_mood_why(p, 0, &w);
    return w.kind;
}

int main(void)
{
    pet_t p = fresh();
    {   /* a time zone flipped back and forth across midnight: one new day, not one a flip (trust_day, the walk goal
           and the day's mood came fresh each time) */
        pet_t z = fresh();
        int64_t t = NOON + 11 * H + 30 * 60; /* 23:30 UTC */
        pet_update(&z, t, 0);
        int32_t d0 = z.day;
        for (int i = 1; i <= 20; i++) pet_update(&z, t + i * 60, i & 1 ? 60 : 0);
        CHECK(z.day == d0 + 1, "time zone flips: the day moves on once");
    }
    CHECK(!p.asleep && pet_mood(&p, 0) != PIP_ASLEEP, "awake at noon");

    /* left alone until 01:00: hungry-ish, tired, and asleep by then */
    pet_update(&p, NOON + 13 * H, 0);
    CHECK(p.asleep, "asleep after midnight");
    CHECK(pet_mood(&p, 0) == PIP_ASLEEP, "sleeping face");
    /* woken at night he grumbles and stays up a while before dozing off again */
    pet_t woken = p;
    pet_event(&woken, PET_WAKE);
    pet_update(&woken, NOON + 13 * H + 300, 0);
    CHECK(!woken.asleep, "stays up 5 min after being woken");
    pet_update(&woken, NOON + 13 * H + 1800, 0);
    CHECK(woken.asleep, "back asleep after a while");
    CHECK(pet_event(&woken, PET_WAKE) != PIP_INDIGNANT, "woken once more after a while: not indignant yet");
    pet_event(&woken, PET_SLEEP);
    CHECK(pet_event(&woken, PET_WAKE) == PIP_INDIGNANT, "woken again within minutes -> indignant");

    /* the night restores energy, and he gets up in the morning, sleepy for a bit */
    pet_t morning = p;
    for (int64_t t = NOON + 13 * H; morning.asleep && t < NOON + 22 * H; t += 60) pet_update(&morning, t, 0);
    CHECK(!morning.asleep && morning.energy > 0.8f, "up in the morning");
    morning.food = morning.fun = morning.love = morning.walk = 0.8f; /* fed and cuddled: no need speaks over it */
    pet_settle(&morning, 0);
    CHECK(pet_mood(&morning, 0) == PIP_SLEEPY && why_kind(&morning) == PET_WHY_SITU, "just woke up -> sleepy (situation)");
    pet_update(&p, NOON + 21 * H, 0); /* 09:00 */
    CHECK(!p.asleep && p.energy > 0.8f, "rested and awake at 9 (up at 7)");

    {   /* asleep at night (23-7) food, love and the walk go night_drain x faster than asleep by day, however he sleeps:
           face down by day vs face down, a nap and his own sleep at night, 2 h each, from the same full needs */
        float loss[4][3];
        for (int k = 0; k < 4; k++) {
            pet_t q = fresh();
            int64_t at = k == 0 ? NOON + 1 * H : NOON + 14 * H; /* 13:00, 02:00 */
            pet_update(&q, at, 0);
            q.food = q.love = q.walk = 1, q.energy = 0.3f, q.last = at;
            pet_event(&q, k == 2 ? PET_DOZE : k == 3 ? PET_LULLED : PET_SLEEP);
            CHECK(q.asleep, "night drain: asleep");
            for (int64_t t = at + 60; t <= at + 2 * H; t += 60) pet_update(&q, t, 0);
            loss[k][0] = 1 - q.food, loss[k][1] = 1 - q.love, loss[k][2] = 1 - q.walk;
        }
        for (int k = 1; k < 4; k++)
            for (int n = 0; n < 3; n++) {
                float r = loss[k][n] / loss[0][n];
                if (r < pet_tune.night_drain - 0.02f || r > pet_tune.night_drain + 0.02f) {
                    fprintf(stderr, "kind %d need %d: night %.4f day %.4f ratio %.3f\n", k, n, loss[k][n], loss[0][n], r);
                    CHECK(0, "night sleep costs night_drain x a day's (face down, nap, his own)");
                }
            }
    }

    /* skip meals and cuddles for a day: hangry, lonely, ill now and then; the faces take turns */
    pet_update(&p, NOON + 30 * H, 0);
    int m = pet_mood(&p, 0), seen[PIP_MOOD_COUNT] = {0}, kinds = 0;
    CHECK(pet_valence(&p) < 0, "neglect hurts");
    for (int i = 0; i < 24; i++) { /* two hours, minute by minute */
        pet_update(&p, NOON + 30 * H + i * 300, 0);
        m = pet_settle(&p, 0);
        kinds += !seen[m]++;
        CHECK(m == PIP_ANGRY || m == PIP_FURIOUS || m == PIP_ANNOYED || m == PIP_LONELY || m == PIP_DESPONDENT || m == PIP_HURT ||
                  m == PIP_SAD || m == PIP_BORED || m == PIP_CYNICAL || m == PIP_GLOOMY || m == PIP_NERVOUS || m == PIP_SICK ||
                  m == PIP_DISMAYED || m == PIP_TIRED || m == PIP_DISGUSTED || m == PIP_SKEPTICAL || m == PIP_NAUSEOUS ||
                  m == PIP_WISTFUL || m == PIP_IMPATIENT, /* the low palettes (their everyday faces too), grubby, ill */
              "neglect shows");
    }
    CHECK(seen[PIP_SICK] && kinds >= 4, "neglect: several faces take turns, sick among them");

    /* care fixes it */
    CHECK(pet_event(&p, PET_PLAY) == PIP_DISGUSTED || p.energy < pet_tune.play_tired, "a day unwashed: too grubby to play");
    p.clean = 1; /* a bath first */
    for (int i = 0; i < 3; i++) pet_event(&p, PET_FEED);
    /* a good long session after a neglected day (pet.c attend: a day's worth of attention and more) */
    for (int i = 0; i < 360; i++) pet_event(&p, PET_STROKE); /* three minutes of stroking */
    for (int i = 0; i < 6; i++) pet_event(&p, PET_PLAY);
    for (int i = 0; i < 10; i++) pet_event(&p, PET_TICKLE); /* worn out he will not play: tickles then */
    for (int i = 0; i < 3000; i++) pet_event(&p, PET_STEP);
    CHECK(p.food > 0.9f && p.love > 0.9f && p.fun > 0.9f && p.walk > 0.9f, "needs refilled");
    CHECK(pet_event(&p, PET_FEED) == PIP_NAUSEOUS, "overfed");
    CHECK(pet_event(&p, PET_FEED) == PIP_DISGUSTED, "overfed twice in a row -> disgusted");
    p.energy = 0.9f, p.clean = 1, pet_ill = 0; /* a nap, a bath, well again: tired, grubby or ill would speak over the walk */
    CHECK(pet_mood(&p, 0) == PIP_AMAZED || pet_mood(&p, 0) == PIP_CURIOUS, "just walked 3000 steps -> out on a walk");
    CHECK(why_kind(&p) == PET_WHY_SITU, "the walk is a situation");

    /* play: too tired to, or can't wait to */
    pet_t q = fresh();
    q.energy = 0.1f;
    CHECK(pet_event(&q, PET_PLAY) == PIP_NO, "too tired to play -> no");
    q = fresh(), q.fun = 0.2f;
    CHECK(pet_event(&q, PET_PLAY) == PIP_YES, "bored, asked to play -> yes");

    /* the palette: never the same mood twice running, and it stays put near a line */
    q = fresh();
    int last = -1, repeats = 0;
    for (int i = 0; i < 60; i++) {
        q.last = NOON + i * 300;
        int b = pet_settle(&q, 0);
        repeats += b == last, last = b;
    }
    CHECK(repeats == 0, "palette moods change every 5 minutes");
    q = fresh();
    q.food = q.energy = q.fun = q.love = q.walk = 0.75f, q.day_mood = 0;
    pet_settle(&q, 0);
    pet_why_t w;
    pet_mood_why(&q, 0, &w);
    CHECK(w.kind == PET_WHY_POOL && w.which == 1, "valence 0.5: the good palette");
    q.food = q.energy = q.fun = q.love = q.walk = 0.79f; /* 0.58: just over great's line, within hyst */
    pet_mood_why(&q, 0, &w);
    CHECK(w.which == 1, "a hair over the line: still good (hysteresis)");
    q.food = q.energy = q.fun = q.love = q.walk = 0.85f;
    pet_mood_why(&q, 0, &w);
    CHECK(w.which == 0, "well over: great");
    q.last = NOON + 9 * H; /* 21:00 */
    pet_mood_why(&q, 0, &w);
    CHECK(w.which == 4, "in the evening the palettes calm down");

    /* a long spell of one need shows its other face too */
    q = fresh();
    q.love = 0.15f;
    int faces = 0;
    for (int i = 0; i < 24; i++) {
        q.last = NOON + i * 300;
        int b = pet_settle(&q, 0);
        faces |= (b == PIP_HURT) | (b == PIP_SAD) << 1 | (b != PIP_HURT && b != PIP_SAD) << 2;
        if (i < 6) CHECK(b == PIP_HURT, "hurt for the first half hour");
    }
    CHECK(faces == 7, "then hurt, sad and the rest of the tree by turns");
    q = fresh();
    q.bat = 15;
    int nervous = 0, rest = 0;
    for (int i = 0; i < 24; i++) {
        q.last = NOON + i * 300;
        int b = pet_settle(&q, 0);
        if (i < 6) CHECK(b == PIP_NERVOUS, "battery low: nervous for the first half hour");
        else nervous += b == PIP_NERVOUS, rest += b != PIP_NERVOUS;
    }
    CHECK(nervous >= 8 && rest >= 8, "then nervous every other 5 minutes");

    /* aftertastes: a win shows for minutes, then it is gone */
    q = fresh();
    CHECK(pet_event(&q, PET_WIN) == PIP_SMUG && pet_mood(&q, 0) == PIP_SMUG, "won -> smug");
    CHECK(why_kind(&q) == PET_WHY_AFTER, "smug is an aftertaste");
    pet_update(&q, q.last + (int64_t)pet_tune.after_win + 60, 0);
    CHECK(pet_mood(&q, 0) != PIP_SMUG || why_kind(&q) != PET_WHY_AFTER, "the win wears off");
    CHECK(pet_event(&q, PET_LOSE) == PIP_NO && pet_mood(&q, 0) == PIP_INDIGNANT, "lost -> no, then indignant");
    CHECK(pet_event(&q, PET_RECORD) == PIP_AMAZED && pet_mood(&q, 0) == PIP_AMAZED, "record -> amazed");
    pet_event(&q, PET_FRIGHT);
    CHECK(pet_mood(&q, 0) == PIP_AFRAID, "a fright -> afraid for a while");
    CHECK(pet_event(&q, PET_FLOOD) == PIP_DISMAYED && pet_mood(&q, 0) == PIP_DISMAYED, "notification flood -> dismayed");
    pet_event(&q, PET_SLEEP);
    CHECK(q.after_mood == PET_NO_MOOD, "sleep wipes an aftertaste");

    /* a good day: poked on and on he goes no further than rage */
    q = fresh();
    q.food = q.energy = q.fun = q.love = q.walk = 0.9f;
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 6; j++) pet_event(&q, PET_POKE);
        int m = pet_event(&q, PET_FED_UP);
        CHECK(m != PIP_SWEARING && m != PIP_BERSERK, "a happy Pip poked on and on: no worse than rage");
    }

    /* a dreadful day, poked again and again: angry, furious, red, swearing, at last berserk; then annoyed a minute -
       Требовательный (patience 1: a poke's annoy as it always was) */
    pet_level = 2;
    q = fresh();
    q.food = q.energy = q.fun = q.love = q.walk = 0.1f, q.day_mood = -0.5f;
    int ladder[] = {PIP_ANNOYED, PIP_ANGRY, PIP_FURIOUS, PIP_RAGE, PIP_RAGE, PIP_SWEARING, PIP_SWEARING, PIP_SWEARING,
                    PIP_SWEARING, PIP_SWEARING, PIP_BERSERK}; /* six pokes a fed-up; the first only annoyed, berserk takes a lot */
    for (int i = 0; i < 11; i++) {
        for (int j = 0; j < 6; j++) pet_event(&q, PET_POKE);
        CHECK(pet_event(&q, PET_FED_UP) == ladder[i], "the poke ladder");
    }
    CHECK(pet_mood(&q, 0) == PIP_ANNOYED, "fed up -> annoyed for a while");
    /* Неприхотливый (patience 2) on the same dreadful day: a poke counts half - six fed-ups and no worse than angry */
    pet_level = 0;
    q = fresh();
    q.food = q.energy = q.fun = q.love = q.walk = 0.1f, q.day_mood = -0.5f;
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 6; j++) pet_event(&q, PET_POKE);
        int m = pet_event(&q, PET_FED_UP);
        CHECK(m == PIP_ANNOYED || m == PIP_ANGRY, "an easy-going Pip poked on: put out, at worst angry");
    }
    pet_level = 1;

    /* games tire him by his character: from a night's rest, an easy-going one plays most, a demanding one fewest */
    int games[PET_LEVELS];
    for (int lv = 0; lv < PET_LEVELS; lv++) {
        pet_level = lv;
        q = fresh();
        q.energy = 0.9f;
        for (games[lv] = 0; q.energy >= pet_tune.play_tired && games[lv] < 100; games[lv]++) {
            for (int m = 0; m < 5; m++) pet_event(&q, PET_MOVE);
            pet_event(&q, games[lv] % 2 ? PET_WIN : PET_LOSE);
        }
        printf("games before he is tired, level %d: %d\n", lv, games[lv]);
    }
    pet_level = 1;
    CHECK(games[0] > games[1] && games[1] > games[2] && games[2] >= 9, "games: easy-going plays most, demanding fewest, all at least 9");

    /* back after hours alone: glad, or sulking until stroked */
    q = fresh();
    pet_update(&q, NOON + 2 * H, 0);
    CHECK(pet_event(&q, PET_SEEN) == -1, "two hours is nothing");
    pet_update(&q, NOON + 7 * H, 0);
    CHECK(pet_event(&q, PET_SEEN) == PIP_LOVE && pet_mood(&q, 0) == PIP_HAPPY, "back after 5 h, loved -> glad");
    q = fresh();
    pet_update(&q, NOON + 7 * H, 0);
    q.love = 0.3f;
    CHECK(pet_event(&q, PET_SEEN) == PIP_HURT && pet_mood(&q, 0) == PIP_HURT, "back after 7 h, neglected -> sulks");
    pet_event(&q, PET_STROKE);
    CHECK(q.after_mood == PET_NO_MOOD, "a stroke makes up");

    /* meeting someone: love, shy, wary, angry, belligerent */
    q = fresh();
    q.food = q.energy = q.fun = q.love = q.walk = 0.5f, q.day_mood = 0; /* valence 0: half like */
    CHECK(pet_meet(&q, 10, PIP_NEUTRAL) == PIP_LOVE && pet_meet(&q, 45, PIP_NEUTRAL) == PIP_SHY, "liked: love, or shy");
    CHECK(pet_meet(&q, 60, PIP_NEUTRAL) == PIP_SUSPICIOUS && pet_meet(&q, 95, PIP_NEUTRAL) == PIP_ANGRY, "disliked: wary, or angry");
    CHECK(pet_meet(&q, 10, PIP_HURT) == PIP_SKEPTICAL && pet_meet(&q, 45, PIP_ANGRY) == PIP_SKEPTICAL, "sulking or cross: no love, no shy");
    CHECK(pet_meet(&q, 95, PIP_HURT) == PIP_ANGRY, "sulking and disliked: angry as ever");
    q.annoy = 3;
    CHECK(pet_meet(&q, 99, PIP_NEUTRAL) == PIP_BELLIGERENT, "disliked while annoyed: belligerent");

    /* the battery: nervous, tired, ill; on the charger he gets comfy */
    q = fresh();
    q.bat = 15;
    CHECK(pet_mood(&q, 0) == PIP_NERVOUS, "battery 15% -> nervous");
    q.bat = 7;
    CHECK(pet_mood(&q, 0) == PIP_TIRED, "battery 7% -> tired");
    q.bat = 2;
    CHECK(pet_mood(&q, 0) == PIP_SICK && why_kind(&q) == PET_WHY_BODY, "battery 2% -> sick");
    q.charging = true, q.charge_since = q.last - 120;
    CHECK(pet_mood(&q, 0) == PIP_COMFY, "charging -> comfy");

    /* rare cards: rolled on the hour while he is up, one a day at most; a card shows its minutes, then is gone */
    q = fresh();
    int cards = 0, kinds_seen[PIP_MOOD_COUNT] = {0};
    int64_t last_at = 0;
    for (int d = 1; d <= 365; d++) {
        pet_update(&q, NOON + d * 24 * H, 0);
        if (q.rare_at != last_at && q.rare_mood != PET_NO_MOOD) cards++, kinds_seen[q.rare_mood]++, last_at = q.rare_at;
        q.food = q.energy = q.fun = q.love = q.walk = q.clean = 0.9f;
    }
    CHECK(cards >= 15 && cards <= 90, "a rare card every week or two over a year");
    CHECK(kinds_seen[PIP_FROZEN] > 0, "winter brings frozen");
    float chance = pet_tune.rare_chance;
    pet_tune.rare_chance = 100; /* the cap: a third an hour */
    q = fresh();
    pet_update(&q, NOON + 8 * H, 0); /* by 20:00 one has surely come */
    pet_tune.rare_chance = chance;
    CHECK(q.rare_mood != PET_NO_MOOD && q.rare_at > NOON && q.rare_at % 3600 == 0, "a card is dealt on the hour");
    int64_t at = q.rare_at;
    q.asleep = false, q.last = at + 60;
    q.food = q.energy = q.fun = q.love = q.walk = q.clean = 0.9f;
    CHECK(pet_mood(&q, 0) == q.rare_mood && why_kind(&q) == PET_WHY_RARE, "the card shows");
    q.last = at + (int64_t)pet_tune.rare_min * 60 + 60;
    CHECK(pet_mood(&q, 0) != q.rare_mood, "and is gone after its minutes");
    /* which one depends on him and the season */
    float rw[PET_RARE_N];
    const int64_t JAN_MORNING = 1767571200 + 9 * H, JULY_NOON = 1783296000 + 14 * H; /* 2026-01-05 09:00, 2026-07-06 14:00 UTC */
    CHECK(pet_month(JAN_MORNING, 0) == 1 && pet_month(JULY_NOON, 0) == 7 && pet_month(NOON, 0) == 10, "months");
    q = fresh(), q.last = JAN_MORNING;
    pet_rare_weights(&q, 0, rw);
    CHECK(rw[3] > 2 && rw[4] < 0.1f, "a winter morning: frozen, not overheated");
    q.last = JULY_NOON, q.steps_today = 6000;
    pet_rare_weights(&q, 0, rw);
    CHECK(rw[4] > 2 && rw[3] == 0 && rw[2] > 1, "a summer noon out walking: overheated, hay fever, never frozen");
    q = fresh(), q.clean = 0.05f;
    pet_rare_weights(&q, 0, rw);
    float most = 0;
    int top = 0;
    for (int i = 0; i < PET_RARE_N; i++) if (rw[i] > most) most = rw[i], top = i;
    CHECK(pet_rare_cards[top] == PIP_PINKEYE, "filthy: pinkeye is the likeliest");
    q = fresh(), q.feeds_today = 6;
    pet_rare_weights(&q, 0, rw);
    CHECK(rw[7] > 4, "biscuit after biscuit: hyper");
    q = fresh();
    pet_rare_now(&q, 0, PIP_SMITTEN);
    CHECK(pet_mood(&q, 0) == PIP_SMITTEN, "a card on request (the debug page)");

    /* dirt: dust settles by the hour, walks kick up more; a day asks for about one wash (two when demanding) */
    for (int lv = 0; lv < PET_LEVELS; lv++) {
        static const int steps[] = {800, 2000, 5000};
        pet_level = lv;
        q = fresh();
        int64_t t = NOON;
        int washes = 0;
        for (int d = 0; d < 6; d++)                  /* six days; washed once the dust is plain to see */
            for (int hr = 0; hr < 24; hr++) {
                t += H;
                pet_update(&q, t, 0);
                if (!q.asleep)
                    for (int k = 0; k < steps[lv] / 16; k++) pet_event(&q, PET_STEP);
                if (pet_dust(&q) > 0.3f) q.clean = 1, washes++;
            }
        CHECK(lv < 2 ? washes >= 4 && washes <= 8 : washes >= 10 && washes <= 16, "washes a day: ~1 (easy, balanced), ~2 (demanding)");
    }
    pet_level = 1;
    q = fresh();
    CHECK(pet_dust(&q) == 0, "fresh: not a speck");
    q.clean = 0.6f;
    CHECK(pet_dust(&q) == 0, "a little grubby: still no dust to see");
    q.clean = 0.2f;
    CHECK(pet_dust(&q) > 0.5f, "grubby: plenty");
    q.clean = 0.25f;
    CHECK(pet_mood(&q, 0) == PIP_DISGUSTED && why_kind(&q) == PET_WHY_URGE, "grubby: disgusted (a need)");
    q.clean = 0.05f;
    CHECK(pet_event(&q, PET_PLAY) == PIP_DISGUSTED, "filthy: no games");
    float before = pet_valence(&q);
    CHECK(pet_washed(&q, q.clean) == PIP_CONTENT, "washed a little: better");
    q.clean = 1;
    CHECK(pet_washed(&q, 0.1f) == PIP_HAPPY && pet_mood(&q, 0) == PIP_CONTENT && why_kind(&q) == PET_WHY_AFTER, "washed clean: glad, fresh for a while");
    CHECK(pet_valence(&q) > before, "clean feels better");
    q = fresh();
    CHECK(pet_washed(&q, 1) == PIP_INDIGNANT, "washing a clean pet: indignant");
    CHECK(pet_washed(&q, 1) == PIP_ANGRY, "again: angry");
    CHECK(pet_refuses_wash(&q), "washed clean twice in a row: he will not have a third");
    CHECK(pet_wash_refused(&q) == PIP_NO, "refused: no");
    CHECK(pet_wash_refused(&q) == PIP_FURIOUS, "and then furious");
    q.last += (int64_t)pet_tune.wash_refuse_s + 60;
    CHECK(!pet_refuses_wash(&q), "a few minutes on he lets it happen again");

    /* the newer feelings the tree hands out: the day's walk done (proud a while), a meal when hungry (grateful),
       face down a moment and back (peekaboo: not bedtime) */
    q = fresh();
    int proud = -1;
    while (q.steps_today < 20000 && proud < 0) proud = pet_event(&q, PET_STEP);
    CHECK(proud == PIP_PROUD && pet_mood(&q, 0) == PIP_PROUD && why_kind(&q) == PET_WHY_AFTER, "the day's walk done: proud, for a while");
    CHECK(pet_event(&q, PET_STEP) < 0, "proud once a day, at the goal");
    q = fresh();
    q.food = 0.2f;
    CHECK(pet_event(&q, PET_FEED) == PIP_GRATEFUL, "fed when hungry: grateful");
    CHECK(pet_event(&q, PET_FEED) == PIP_CONTENT, "fed again, no longer hungry: content");
    q = fresh();
    CHECK(pet_event(&q, PET_PEEKABOO) == PIP_PLAYFUL, "peekaboo: playful");
    pet_event(&q, PET_SLEEP);
    CHECK(pet_event(&q, PET_PEEKABOO) < 0 && q.asleep, "asleep: no peekaboo");

    /* dropped: shocked, hurt after; thrown: angry, worse the second time, hurt after; a stroke makes either up */
    q = fresh();
    CHECK(pet_event(&q, PET_DROPPED) == PIP_SHOCKED && pet_mood(&q, 0) == PIP_HURT, "dropped: shocked, then hurt");
    pet_event(&q, PET_STROKE);
    CHECK(pet_mood(&q, 0) != PIP_HURT, "dropped, then stroked: made up");
    q = fresh();
    CHECK(pet_event(&q, PET_THROWN) == PIP_ANGRY && pet_mood(&q, 0) == PIP_HURT, "thrown: angry, then hurt");
    CHECK(pet_event(&q, PET_THROWN) == PIP_FURIOUS, "thrown again: furious");

    /* a month away is caught up without hanging and leaves a starving pet */
    pet_update(&p, NOON + 400 * 24 * H, 0);
    CHECK(p.food == 0, "a month away");
    puts("pet_test ok");
    return 0;
}
