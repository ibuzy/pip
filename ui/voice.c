#include "voice.h"
#include <math.h>
#include <string.h>

#define MAX_SYL 16 /* the birthday tune is the longest: 12 notes and a flourish */
#define TWO_PI 6.2831853f

/* One syllable: a pitched source through a "mouth" filter that opens and closes. */
typedef struct {
    float f0, f1;       /* pitch glide, Hz */
    float dur, gap;     /* s */
    float vib_rate, vib_depth;
    float rough;        /* growl: amplitude flutter */
    float bright;       /* mouth cutoff as a multiple of the pitch */
    float open;         /* how much the mouth opens mid-syllable (cutoff x) */
    float noise;        /* breath */
    float amp;
    float formant;      /* Hz: the mouth at a fixed vowel instead of following the pitch (0: f * bright) */
    float damp;         /* the mouth's damping (0: 1.25, gentle); lower rings more, like a vowel */
    float jitter, shimmer; /* creak: how much each glottal period's length and strength wander */
    uint8_t wave;       /* 0 soft (sine + harmonic), 1 creak (glottal pulses: a throat, for the burp) */
    float gurgle;       /* creak only: pulses dropped or halved now and then, every other one weaker (fry): a wet rattle */
    float formant2;     /* creak only: Hz of a second vowel resonance mixed in (0: none) - two make a vowel, one a tube */
} syl_t;

typedef struct { syl_t s[MAX_SYL]; int n; float gain; } phrase_t;

/* Each sound's gain to the same loudness: the RMS of what it renders, measured by sim/voice_test (which checks they
   stay within a narrow band) and divided out. A filter that opens less, a short click or a noisy burst all come out
   as loud as a chirp. */
static const float level[VOICE_COUNT] = {
    [VOICE_POKE] = 0.94f, [VOICE_HAPPY] = 0.95f, [VOICE_SAD] = 1.18f, [VOICE_GROWL] = 2.00f, [VOICE_OH] = 0.96f, [VOICE_LOVE] = 0.90f,
    [VOICE_GIGGLE] = 1.00f, [VOICE_NOM] = 1.27f, [VOICE_BURP] = 4.7f, /* 5.3 peaked over 0.7 about once in a hundred */ [VOICE_YAWN] = 1.66f, [VOICE_DIZZY] = 0.95f, [VOICE_DERP] = 1.18f,
    [VOICE_HM] = 1.01f, [VOICE_CENSOR] = 1.69f, [VOICE_ALARM] = 0.96f, [VOICE_HMPH] = 1.72f, [VOICE_TICK] = 1.32f, [VOICE_TOCK] = 1.49f,
    [VOICE_SNEEZE] = 0.98f, [VOICE_RING] = 1.06f,
    [VOICE_WHEE] = 0.96f, [VOICE_HUM] = 1.09f, [VOICE_POUT] = 1.24f, [VOICE_TADA] = 0.92f, [VOICE_THINK] = 1.04f,
    [VOICE_AWW] = 0.93f, [VOICE_PEEK] = 0.93f, [VOICE_PHEW] = 1.11f, [VOICE_LAUGH] = 0.99f, [VOICE_THANKS] = 1.01f,
    [VOICE_BIRTHDAY] = 0.95f, [VOICE_LOOP] = 0.97f, [VOICE_RANT] = 1.40f, [VOICE_SNORT] = 1.48f, [VOICE_BRR] = 1.85f, [VOICE_LULLABY] = 1.18f, [VOICE_CHAT] = 1.0f, [VOICE_MUTTER] = 1.6f, [VOICE_MUMBLE] = 1.12f, [VOICE_TEASE] = 1.0f,
    [VOICE_NOTE0] = 1.0f, [VOICE_NOTE1] = 1.0f, [VOICE_NOTE2] = 1.0f, [VOICE_NOTE3] = 1.0f, [VOICE_FLAP] = 1.0f, [VOICE_POINT] = 1.0f, [VOICE_BABBLE] = 1.0f,
    [VOICE_TAPTAP] = 1.03f, [VOICE_SIGH] = 1.03f, [VOICE_MMM] = 1.04f, [VOICE_OOH] = 0.94f, [VOICE_SNICKER] = 1.05f, [VOICE_EHE] = 1.02f,
    [VOICE_HUP] = 1.11f, [VOICE_AHA] = 1.06f, [VOICE_UGH] = 1.1f, [VOICE_KYA] = 1.05f, [VOICE_HIC] = 1.14f, [VOICE_TOOT] = 3.2f,
    [VOICE_SPARK] = 1.42f, [VOICE_STARDUST] = 1.13f, [VOICE_BLOOM] = 1.0f,
    [VOICE_MMPH] = 1.06f, [VOICE_BOING] = 0.96f, [VOICE_EEK] = 0.96f, [VOICE_PINCH] = 1.0f, [VOICE_YAY] = 0.92f, [VOICE_NUZZLE] = 1.04f,
    [VOICE_OOHOOH] = 0.94f, [VOICE_YUM] = 1.00f, [VOICE_FANFARE] = 0.92f, [VOICE_DEFLATE] = 0.98f, [VOICE_SQUEAK] = 1.18f, [VOICE_GROGGY] = 1.03f,
    [VOICE_WOW] = 0.85f, [VOICE_TWINKLE] = 1.09f, [VOICE_LALA] = 0.86f, [VOICE_ARIA] = 0.86f, [VOICE_MWAH] = 0.95f, [VOICE_SMOOCH] = 1.02f,
    [VOICE_HEYYOU] = 0.89f, [VOICE_WINK] = 1.28f, [VOICE_XYLO] = 0.97f, [VOICE_PRISM] = 0.90f, [VOICE_KNOCK] = 0.97f, [VOICE_POPUP] = 0.95f,
    [VOICE_OM] = 1.14f, [VOICE_BOWL] = 1.20f, [VOICE_SNORE] = 1.43f, [VOICE_DREAMSONG] = 1.05f, [VOICE_MELT] = 0.87f, [VOICE_COO] = 0.90f,
    [VOICE_CLAPS] = 1.12f, [VOICE_BRAVO] = 0.86f, [VOICE_BASSLINE] = 1.15f, [VOICE_YEAH] = 0.95f, [VOICE_WHIMPER] = 0.86f, [VOICE_YIP] = 0.92f,
    [VOICE_FLUTTER] = 1.07f, [VOICE_HUSHED] = 1.00f, [VOICE_BLUB] = 0.93f, [VOICE_BUBBLEPOP] = 0.92f, [VOICE_BEEPBOOP] = 0.88f, [VOICE_SERVO] = 1.18f,
    [VOICE_PURR] = 1.80f, [VOICE_MEOW] = 0.87f, [VOICE_BLBLBL] = 1.28f, [VOICE_YOYO] = 0.91f, [VOICE_PFFT] = 0.99f, [VOICE_CRACKUP] = 1.05f,
    [VOICE_CRACKLE] = 1.49f, [VOICE_COZY] = 1.06f, [VOICE_SHOOTING] = 1.36f, [VOICE_WISH] = 1.25f, [VOICE_ZAP] = 0.93f, [VOICE_POWERUP] = 0.92f,
    [VOICE_GULP] = 0.96f, [VOICE_SKYLIT] = 1.22f, [VOICE_HEART] = 1.35f, [VOICE_RISER] = 1.07f, [VOICE_GRAND] = 0.95f, [VOICE_ANTHEM] = 0.95f,
    [VOICE_FILLUP] = 1.00f,
    [VOICE_HEEHEE] = 0.95f, [VOICE_WHEEZE] = 0.88f, [VOICE_ROLLING] = 0.89f, [VOICE_HOHO] = 0.97f, [VOICE_BOINGS] = 0.89f,
    [VOICE_YIPPEE] = 0.87f, [VOICE_PETALS] = 1.02f, [VOICE_SPRINGSONG] = 0.87f, [VOICE_LUBDUB] = 0.99f, [VOICE_SWOON] = 0.85f,
    [VOICE_ROCKETUP] = 1.18f, [VOICE_OOHAAH] = 0.86f, [VOICE_MEDALIST] = 0.86f, [VOICE_MHM] = 0.92f, [VOICE_DISCOBEAT] = 0.97f,
    [VOICE_GROOVY] = 0.90f, [VOICE_EYEPOP] = 0.89f, [VOICE_WHOANELLY] = 0.86f, [VOICE_IDEAPING] = 1.05f, [VOICE_GOTIT] = 0.90f,
    [VOICE_PEEKSHY] = 1.01f, [VOICE_BASHFUL] = 0.89f, [VOICE_HELLOO] = 0.86f, [VOICE_YOOHOO] = 0.85f, [VOICE_ROYAL] = 0.87f,
    [VOICE_HARRUMPH] = 1.09f, [VOICE_HALOSONG] = 0.94f, [VOICE_WHISTLING] = 0.92f, [VOICE_MWAHAHA] = 0.90f, [VOICE_TEEHEE] = 0.98f,
    [VOICE_HOPLA] = 0.87f, [VOICE_CIRCUS] = 0.87f, [VOICE_LIFTOFF] = 0.92f, [VOICE_POPFALL] = 0.94f, [VOICE_LICK] = 0.90f,
    [VOICE_FREEZE] = 0.87f, [VOICE_PITTER] = 1.19f, [VOICE_DRYHUM] = 0.91f, [VOICE_HEAVE] = 0.97f, [VOICE_STRONG] = 0.88f,
    [VOICE_ABRACADABRA] = 0.94f, [VOICE_RABBIT] = 0.95f, [VOICE_DAWN] = 0.94f, [VOICE_STRETCH] = 0.90f, [VOICE_BAA] = 0.91f,
    [VOICE_COUNTING] = 0.91f, [VOICE_HIYA] = 0.86f, [VOICE_SHADOW] = 1.22f, [VOICE_GROWING] = 0.94f, [VOICE_BLOSSOM] = 1.00f,
    [VOICE_GREYSIGH] = 0.99f, [VOICE_DRIZZLE] = 1.09f, [VOICE_BAWL] = 0.86f, [VOICE_SOBS] = 0.94f, [VOICE_CRACKED] = 0.95f,
    [VOICE_LETGO] = 0.99f, [VOICE_KETTLE] = 1.10f, [VOICE_GRRMPH] = 1.48f, [VOICE_SMOULDER] = 1.43f, [VOICE_ROARLET] = 1.24f,
    [VOICE_HMFF] = 0.94f, [VOICE_NYEH] = 0.97f, [VOICE_TEETH] = 0.96f, [VOICE_BOOO] = 1.00f, [VOICE_EMPTYWIND] = 1.17f,
    [VOICE_MEH] = 0.96f, [VOICE_POWERDOWN] = 1.12f, [VOICE_LOWBEEP] = 0.92f, [VOICE_RUMBLE] = 1.13f, [VOICE_NOCOOKIE] = 0.89f,
    [VOICE_BIRDIES] = 1.17f, [VOICE_KAPUT] = 0.94f, [VOICE_LOVESMENOT] = 0.92f, [VOICE_FADING] = 1.04f, [VOICE_SNIFFLE] = 1.03f,
    [VOICE_ACHOOLET] = 0.91f, [VOICE_POPFULL] = 1.0f, [VOICE_CHARGEDUP] = 1.0f, [VOICE_YANK] = 0.92f, [VOICE_NOTFULL] = 1.0f, [VOICE_POWERFAIL] = 0.95f, [VOICE_FEEBLE] = 1.1f, [VOICE_RETCH] = 1.22f,
    [VOICE_JINGLE] = 1.18f, [VOICE_OHTREE] = 1.0f, [VOICE_HANGUP] = 1.24f, [VOICE_AAH] = 1.0f,
};
/* the babble's kinds to the same loudness (voice_test measures them over the whole range of valence and arousal); a
   rough grumble loses some to its flutter, made up by its own factor in compose */
static const float babble_level[BABBLE_KINDS] = {
    [BABBLE_SAY] = 1.0f, [BABBLE_ASK] = 1.0f, [BABBLE_EXCLAIM] = 1.0f, [BABBLE_COMPLAIN] = 1.1f, [BABBLE_SOB] = 1.04f,
    [BABBLE_WOOZY] = 1.0f, [BABBLE_GIGGLY] = 1.0f, [BABBLE_INTERJ] = 1.0f,
};

static struct {
    int rate;
    float volume;
    phrase_t cur;
    volatile int pending; /* set by voice_say from the UI side, picked up by the audio side */
    volatile float pending_gain; /* its Р—РІСѓРєРё level, set before it */
    volatile int bb_kind;           /* voice_babble's order, set before pending */
    volatile float bb_v, bb_a;
    volatile float tone_semis, tone_tempo; /* voice_tone: for the next of his own sounds */
    volatile bool hush;   /* voice_hush: the playing sound fades out over HUSH_S */
    float fade;           /* 1 while it plays; falling to 0 once hushed */
    float gain;           /* the playing sound's */
    int i;                /* current syllable, == cur.n when idle */
    int k, len, gap;      /* samples into it, its sounding length and its silent tail */
    /* the syllable's running values, set up when it starts so the per-sample loop has no pow/exp/sin calls */
    float f, glide, inv_len, vib_ph, vib_inc, rough_ph, rough_inc, period_k, pulse_amp;
    float phase, low, band, hp, hp2, prev_src, prev_hp; /* the creak's source, high-passed twice */
    float low2, band2;  /* the creak's second vowel resonance */
    int periods;        /* glottal periods so far in this syllable */
    float warm, warm_k; /* a one-pole low-pass over everything */
    uint32_t rng, noise;
} v = {.rate = 16000, .volume = 0.8f, .pending = -1, .pending_gain = 1, .tone_tempo = 1, .gain = 1, .period_k = 1, .warm_k = 0.7f, .fade = 1};
#define HUSH_S 0.03f /* quick, and still no click */

static uint32_t next(uint32_t *s) { *s ^= *s << 13, *s ^= *s >> 17, *s ^= *s << 5; return *s; }
static float rnd(float a, float b) { return a + (b - a) * (next(&v.rng) % 10000) / 10000.0f; }
static float semi(float n) { return powf(2, n / 12); }

static void add(phrase_t *p, syl_t s)
{
    if (p->n < MAX_SYL) {
        if (!s.amp) s.amp = 1;
        if (!s.bright) s.bright = 4;
        if (!s.open) s.open = 1.6f;
        p->s[p->n++] = s;
    }
}

/* sin(2 pi p) for any p >= 0, without libm: a parabola with Bhaskara's correction, within 0.1% - more than a tiny
   speaker can tell, at a fraction of sinf's cost on the ESP32 (it runs several times a sample, 16000 samples a second) */
static inline float sin01(float p)
{
    p -= (float)(int)p;
    float u = p < 0.5f ? p * 2 : p * 2 - 1, y = 4 * u * (1 - u);
    y *= 0.775f + 0.225f * y;
    return p < 0.5f ? y : -y;
}

/* His talk, made to order: never the same phrase twice. How he feels sets the pitch, the line and the voice (glad:
   higher, brighter, bouncing; cross: low and rough; glum: slow, falling, breathy); how worked up he is, the pace and the
   number of syllables; the kind, the shape of the sentence. Every syllable is a plain soft one (no new sources), so it
   costs what the other sounds cost. */
static void babble(phrase_t *p, float base, int kind, float val, float aro)
{
    val = val < -1 ? -1 : val > 1 ? 1 : val, aro = aro < 0 ? 0 : aro > 1 ? 1 : aro;
    float b = base * semi(val * 2.2f + aro * 1.8f - 0.6f);
    float sdur = .15f - .075f * aro, sgap = .075f - .045f * aro; /* calm: long, spaced syllables; worked up: a patter */
    float rough = val < -0.2f && (kind == BABBLE_COMPLAIN || kind == BABBLE_SAY || kind == BABBLE_INTERJ) ? (-val - 0.2f) * (0.4f + 0.4f * aro) : 0;
    float bright = 2.1f + 0.6f * (val + 1), open = 1.3f + 0.6f * aro + 0.2f * (val > 0 ? val : 0);
    float breath = val < 0 ? -val * 0.05f : 0.01f;
    if (kind == BABBLE_INTERJ) { /* one sound: glad "o!" up, curious "m?" up a little, cross "eh" down and rough */
        float up = val > 0.2f ? 1.35f : val > -0.2f ? 1.15f : 0.8f;
        add(p, (syl_t){.f0 = b, .f1 = b * up, .dur = rnd(.11f, .16f) * (1.2f - 0.3f * aro), .rough = rough, .bright = bright,
                       .open = open + 0.3f, .noise = breath, .vib_rate = 9, .vib_depth = .015f});
        p->gain *= babble_level[kind] * (1 + 0.9f * rough);
        return;
    }
    int n = 3 + (int)(aro * 4 + rnd(0, 3.5f));
    if (kind == BABBLE_EXCLAIM) n = 3 + (int)rnd(0, 3);
    if (kind == BABBLE_SOB) n = 3 + (int)rnd(0, 3);
    int catch_at = kind == BABBLE_SOB ? 1 + (int)rnd(0, n - 1) : -1, giggle_at = kind == BABBLE_GIGGLY ? n / 2 + (int)rnd(-1, 1.5f) : -1;
    for (int i = 0; i < n; i++) {
        float x = n > 1 ? (float)i / (n - 1) : 0, line = 1, d = sdur * rnd(.75f, 1.25f), g = sgap * rnd(.7f, 1.4f), glide = semi(rnd(-1.5f, 2));
        float vr = 9, vd = .015f, nz = breath, rg = rough;
        switch (kind) {
        case BABBLE_SAY: /* up to a peak early on, then down; glad bounces more, glum hardly rises */
            line = 1 + (0.12f + 0.12f * val) * (1 - fabsf(x * 2 - 0.6f)) - (val < 0 ? 0.12f * x : 0.04f * x);
            if (i == n - 1) d *= 1.4f, glide = semi(val > 0 ? -1 : -2.5f);
            break;
        case BABBLE_ASK: /* level, then up at the end */
            line = 1 + 0.05f * (1 - x) + (x > 0.6f ? 0.32f * (x - 0.6f) / 0.4f : 0);
            if (i == n - 1) d *= 1.5f, glide = semi(3.5f);
            break;
        case BABBLE_EXCLAIM: /* high and climbing, the last one up and held */
            line = 1.08f + 0.2f * x;
            if (i == n - 1) d *= 1.9f, glide = semi(4), vd = .03f;
            break;
        case BABBLE_COMPLAIN: /* low, falling, clipped and rough */
            line = 0.78f - 0.12f * x, d *= 0.9f, glide = semi(rnd(-2.5f, 0.3f)), rg = rough > 0.25f ? rough : 0.25f;
            break;
        case BABBLE_SOB: /* wavering down, breathy; at the catch the voice breaks up and the next comes after a gulp */
            line = 0.95f - 0.2f * x, d *= 1.3f, vr = 6, vd = .05f, nz = .06f, glide = semi(-1.5f);
            if (i == catch_at) line *= 1.3f, d *= 0.6f, g += .12f, glide = semi(-4);
            break;
        case BABBLE_WOOZY: /* the line swims, each syllable sliding */
            line = 1 + 0.18f * sin01(x * 1.3f + 0.1f), vr = 3.5f, vd = .06f, glide = semi(rnd(-3, 3));
            break;
        case BABBLE_GIGGLY: /* a glad sentence with a run of giggles in its middle */
            line = 1.05f + 0.12f * (1 - fabsf(x * 2 - 0.7f));
            break;
        }
        float f = b * line * semi(rnd(-1.2f, 1.2f) * (0.5f + aro));
        add(p, (syl_t){.f0 = f, .f1 = f * glide, .dur = d, .gap = g + (i == n / 2 ? .05f : 0), .rough = rg, .bright = bright,
                       .open = open * rnd(.9f, 1.1f), .noise = nz, .vib_rate = vr, .vib_depth = vd});
        if (i == giggle_at) /* "hee-hee-hi" */
            for (int k = 0; k < 3; k++) {
                float gf = b * (1.6f - k * 0.08f);
                add(p, (syl_t){.f0 = gf, .f1 = gf * 1.12f, .dur = rnd(.045f, .06f), .gap = .035f, .bright = 2.6f, .open = 1.8f,
                               .vib_rate = 16, .vib_depth = .02f, .noise = .03f});
            }
    }
    p->gain *= babble_level[kind] * (1 + 0.9f * rough);
}

/* Every call makes a slightly different phrase, so he never sounds like a recording. The palette is a little
   robot's (WALL-E's, more or less): round sine voices that glide and warble, a soft mouth, hardly any noise or
   buzz. Nothing harsh: a growl is a rumble, the censor's bleep a sine, and the one throaty source (glottal pulses)
   is kept for the burp, where a throat is the point. */
static void compose(phrase_t *p, int sound)
{
    memset(p, 0, sizeof *p);
    p->gain = sound >= 0 && sound < VOICE_COUNT && level[sound] > 0 ? level[sound] : 1;
    float base = 520 * semi(rnd(-2, 2));
    switch (sound) {
    case VOICE_POKE: /* a soft rising "eep" */
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 2.0f, .dur = .1f, .bright = 2.5f, .open = 1.4f, .vib_rate = 12, .vib_depth = .015f});
        break;
    case VOICE_HAPPY:
        for (int i = 0, n = 2 + (int)rnd(0, 2); i < n; i++)
            add(p, (syl_t){.f0 = base * semi(i * 3), .f1 = base * semi(i * 3 + 5), .dur = rnd(.08f, .12f), .gap = .04f, .vib_rate = 9,
                           .vib_depth = .02f, .bright = 3});
        break;
    case VOICE_SAD:
        add(p, (syl_t){.f0 = base * .8f, .f1 = base * .55f, .dur = .45f, .gap = .08f, .vib_rate = 5, .vib_depth = .03f, .bright = 2.2f, .open = 1.2f});
        add(p, (syl_t){.f0 = base * .62f, .f1 = base * .42f, .dur = .5f, .vib_rate = 5, .vib_depth = .04f, .bright = 2, .open = 1.1f, .noise = .02f});
        break;
    case VOICE_GROWL: /* a low grumbling "mrrm", not a buzz */
        for (int i = 0, n = 1 + (int)rnd(0, 2); i < n; i++)
            add(p, (syl_t){.f0 = base * .6f, .f1 = base * .48f, .dur = rnd(.2f, .28f), .gap = .06f, .rough = .6f, .bright = 3, .open = 1.3f, .amp = .85f});
        break;
    case VOICE_OH:
        add(p, (syl_t){.f0 = base * .9f, .f1 = base * 1.8f, .dur = .16f, .gap = .03f, .bright = 3, .open = 2.2f});
        add(p, (syl_t){.f0 = base * 1.6f, .f1 = base * 2.1f, .dur = .1f, .bright = 3});
        break;
    case VOICE_LOVE:
        add(p, (syl_t){.f0 = base * 1.6f, .f1 = base * 1.1f, .dur = .7f, .vib_rate = 10, .vib_depth = .05f, .bright = 2.8f, .open = 1.7f});
        break;
    case VOICE_GIGGLE: /* "hee-hee-hee-hi": quick bright bubbles, each a little hop up then down, the run falling */
        for (int i = 0, n = 4 + (int)rnd(0, 3); i < n; i++) {
            float f = base * (1.75f - i * 0.08f) * (i % 2 ? 0.94f : 1);
            add(p, (syl_t){.f0 = f, .f1 = f * 1.12f, .dur = rnd(.045f, .06f), .gap = rnd(.03f, .045f), .bright = 2.6f, .open = 1.8f,
                           .vib_rate = 16, .vib_depth = .02f, .noise = .03f});
        }
        break;
    case VOICE_NOM:
        for (int i = 0; i < 3; i++)
            add(p, (syl_t){.f0 = base * .55f, .f1 = base * .5f, .dur = .08f, .gap = .07f, .bright = 1.8f, .open = 1.1f, .amp = .8f});
        add(p, (syl_t){.f0 = base, .f1 = base * 1.5f, .dur = .12f, .bright = 3});
        break;
    case VOICE_BURP: { /* a real throat, never twice the same: a gulp of air pushed up (a breathy onset), then slow uneven
                          glottal pulses (~100-125 Hz rising a touch, then sagging to ~70: each period a little off in
                          length and strength, every other one weaker and now and then one swallowed - the wet rattle)
                          through two vowel resonances, an "o" opening to an "a" (~520/900 -> ~800/1250 Hz, where a small
                          speaker sings); it ends in a breath, or the lips close on a soft "p". Short "urp", the usual
                          "bruuaarp", a long gurgling one, or two in a row (pip.c times his face to it: under ~1.2 s).
                          The pulses are rounded and high-passed, so it rumbles rather than buzzes */
        int kind = (int)rnd(0, 100), parts = kind < 18 ? 2 : 1; /* two in a row now and then */
        bool lng = kind >= 18 && kind < 38, shrt = kind >= 38 && kind < 58;
        for (int k = 0; k < parts; k++) {
            float f = (parts == 2 ? 118 : 108) * semi(rnd(-1.6f, 1.6f)), d = parts == 2 ? rnd(.16f, .22f) : lng ? rnd(.62f, .78f) : shrt ? rnd(.2f, .27f) : rnd(.4f, .52f);
            float o = rnd(480, 560), wet = lng ? rnd(.45f, .7f) : rnd(.15f, .4f);
            add(p, (syl_t){.wave = 1, .f0 = f * .85f, .f1 = f, .dur = .05f, .formant = o * .8f, .formant2 = o * 1.7f, .open = 1.2f, .damp = .5f,
                           .jitter = .2f, .shimmer = .3f, .noise = .3f, .amp = .55f, .gurgle = wet}); /* the gulp: breathy, rising */
            add(p, (syl_t){.wave = 1, .f0 = f, .f1 = f * rnd(.62f, .72f), .dur = d, .formant = o, .formant2 = o * rnd(1.7f, 1.9f),
                           .open = rnd(1.4f, 1.65f), .damp = .3f, .jitter = .16f, .shimmer = .26f, .noise = .05f, .vib_rate = rnd(4, 7),
                           .vib_depth = .05f, .gurgle = wet, .gap = k + 1 < parts ? rnd(.06f, .1f) : 0,
                           .amp = (lng ? 1.18f : parts == 2 ? .92f : shrt ? 1.06f : 1) * sqrtf(108 / f) * (1.15f - 0.4f * wet)}); /* every kind, pitch and rattle as loud as the rest */
        }
        if (rnd(0, 1) < 0.45f) /* the lips close on it */
            add(p, (syl_t){.wave = 1, .f0 = 78, .f1 = 62, .dur = .06f, .formant = 420, .formant2 = 760, .open = 1, .damp = .9f, .jitter = .1f,
                           .shimmer = .2f, .noise = .35f, .amp = .5f});
        else /* or it trails off in a breath */
            add(p, (syl_t){.wave = 1, .f0 = 70, .f1 = 58, .dur = rnd(.1f, .16f), .formant = 600, .formant2 = 1100, .open = 1, .damp = .8f,
                           .jitter = .25f, .shimmer = .4f, .noise = .6f, .amp = .35f, .gurgle = .6f});
        break;
    }
    case VOICE_YAWN:
        add(p, (syl_t){.f0 = base * .6f, .f1 = base * .95f, .dur = .45f, .bright = 2, .open = 2.4f, .noise = .05f, .amp = .7f});
        add(p, (syl_t){.f0 = base * .9f, .f1 = base * .45f, .dur = .6f, .bright = 2, .open = 1.4f, .noise = .04f, .amp = .6f});
        break;
    case VOICE_DIZZY:
        add(p, (syl_t){.f0 = base * 1.3f, .f1 = base * .6f, .dur = .8f, .vib_rate = 6, .vib_depth = .12f, .bright = 3, .open = 1.4f});
        break;
    case VOICE_DERP:
        add(p, (syl_t){.f0 = base * .7f, .f1 = base * .5f, .dur = .25f, .vib_rate = 12, .vib_depth = .07f, .bright = 2.4f});
        add(p, (syl_t){.f0 = base * .5f, .f1 = base * .75f, .dur = .2f, .vib_rate = 12, .vib_depth = .07f, .bright = 2.4f});
        break;
    case VOICE_HM:
        add(p, (syl_t){.f0 = base * .7f, .f1 = base * 1.05f, .dur = .28f, .bright = 1.8f, .open = 1.6f});
        break;
    case VOICE_RANT: { /* a tantrum: gabble squeaky, then low, then squeaky again, each bit bleeped at its own pitch, and
                          a strangled "aargh" falling away at the end; funny rather than nasty */
        static const float lo[3] = {1.25f, .5f, 1.5f}, hi[3] = {1.9f, .78f, 2.1f}, bleep[3] = {1100, 740, 1320};
        for (int b = 0; b < 3; b++) {
            for (int i = 0; i < 3; i++)
                add(p, (syl_t){.f0 = base * rnd(lo[b], hi[b]), .f1 = base * rnd(lo[b], hi[b]), .dur = rnd(.045f, .07f), .gap = .012f,
                               .rough = .55f, .bright = 2.8f, .open = 1.3f});
            add(p, (syl_t){.f0 = bleep[b], .f1 = bleep[b], .dur = .13f, .gap = .03f, .bright = 1.5f, .open = 1, .amp = .5f});
        }
        add(p, (syl_t){.f0 = base * 1.7f, .f1 = base * .7f, .dur = .38f, .rough = .7f, .bright = 3, .open = 2, .vib_rate = 9, .vib_depth = .04f});
        break;
    }
    case VOICE_SNORT: /* air through the nose: a breathy "pff" up front, then a short nasal "hng" bump, sometimes twice */
        for (int i = 0, n = rnd(0, 1) < 0.3f ? 2 : 1; i < n; i++) {
            add(p, (syl_t){.f0 = base * .7f, .f1 = base * .6f, .dur = .06f, .bright = 6, .open = 1, .noise = 1.5f, .amp = .5f});
            add(p, (syl_t){.f0 = base * .62f, .f1 = base * .5f, .dur = rnd(.08f, .11f), .gap = .07f, .bright = 1.6f, .open = 1.3f,
                           .noise = .25f, .rough = .35f});
        }
        break;
    case VOICE_BRR: /* "brrr": a low lip-trill (fast flutter) shivering in pitch, then the teeth chattering in quick ticks */
        add(p, (syl_t){.f0 = base * .55f, .f1 = base * .5f, .dur = rnd(.45f, .6f), .gap = .05f, .rough = 1, .vib_rate = 11, .vib_depth = .06f,
                       .bright = 2.2f, .open = 1.2f, .noise = .12f});
        for (int i = 0; i < 4; i++)
            add(p, (syl_t){.f0 = base * 1.8f, .f1 = base * 1.7f, .dur = .025f, .gap = .045f, .bright = 3, .open = 1, .noise = .5f, .amp = .55f});
        break;
    case VOICE_LULLABY: { /* a sleepy hummed lullaby: mi re do, re do la, slow, each note softer, the mouth closing */
        static const float tune[6] = {4, 2, 0, 2, 0, -3};
        for (int i = 0; i < 6; i++)
            add(p, (syl_t){.f0 = base * .8f * semi(tune[i]), .f1 = base * .8f * semi(tune[i] - .2f), .dur = i == 2 || i == 5 ? .5f : .3f,
                           .gap = i == 2 ? .18f : .05f, .vib_rate = 4.5f, .vib_depth = .02f, .bright = 1.4f, .open = 1.15f,
                           .amp = 1 - i * 0.09f});
        break;
    }
    case VOICE_CHAT: { /* glad babble: 5..8 quick syllables, each its own little glide, the line rising then falling (or,
                          now and then, rising at the end: a question) */
        int n = 5 + (int)rnd(0, 4);
        bool ask = rnd(0, 1) < 0.3f;
        for (int i = 0; i < n; i++) {
            float x = (float)i / (n - 1), line = ask && x > 0.7f ? 1.25f + (x - 0.7f) : 1.15f + 0.25f * (1 - fabsf(x * 2 - 0.8f));
            float f = base * line * semi(rnd(-2.5f, 2.5f));
            add(p, (syl_t){.f0 = f, .f1 = f * semi(rnd(-2, 3)), .dur = rnd(.065f, .11f), .gap = rnd(.025f, .06f) + (i == n / 2 ? .07f : 0),
                           .vib_rate = 9, .vib_depth = .015f, .bright = 2.8f, .open = rnd(1.4f, 2.0f)});
        }
        break;
    }
    case VOICE_MUTTER: /* a cross mutter: low, rough, clipped syllables, falling */
        for (int i = 0, n = 5 + (int)rnd(0, 3); i < n; i++) {
            float f = base * (0.66f - i * 0.025f) * semi(rnd(-1.5f, 1.5f));
            add(p, (syl_t){.f0 = f, .f1 = f * semi(rnd(-2, 0.5f)), .dur = rnd(.08f, .13f), .gap = rnd(.02f, .05f), .rough = .5f,
                           .bright = 2.2f, .open = 1.25f, .noise = .04f});
        }
        break;
    case VOICE_MUMBLE: /* a glum mumble: slow, falling, the mouth hardly open */
        for (int i = 0, n = 3 + (int)rnd(0, 3); i < n; i++) {
            float f = base * (0.8f - i * 0.06f) * semi(rnd(-1, 1));
            add(p, (syl_t){.f0 = f, .f1 = f * semi(-1.5f), .dur = rnd(.15f, .22f), .gap = rnd(.06f, .1f), .vib_rate = 4, .vib_depth = .02f,
                           .bright = 1.8f, .open = 1.15f, .noise = .03f});
        }
        break;
    case VOICE_TEASE: { /* "be-be-be!": three bleated syllables (a quick goat's warble), the last one higher and longer */
        static const float tune[3] = {0, 0, 3};
        for (int i = 0; i < 3; i++)
            add(p, (syl_t){.f0 = base * 1.3f * semi(tune[i]), .f1 = base * 1.3f * semi(tune[i] - .5f), .dur = i == 2 ? .24f : .12f, .gap = .05f,
                           .vib_rate = 24, .vib_depth = .06f, .bright = 3.2f, .open = 2.0f});
        break;
    }
    case VOICE_CENSOR: /* cross mumbling, then the bleep (a sine, not a square) */
        for (int i = 0; i < 5; i++)
            add(p, (syl_t){.f0 = base * rnd(.45f, .8f), .f1 = base * rnd(.45f, .8f), .dur = rnd(.05f, .09f), .gap = .015f, .rough = .4f, .bright = 2.8f});
        add(p, (syl_t){.f0 = 880, .f1 = 880, .dur = .32f, .bright = 1.5f, .open = 1, .amp = .5f});
        break;
    case VOICE_ALARM: { /* a little rising tune, sung twice */
        static const float notes[] = {0, 4, 7, 12};
        for (int r = 0; r < 2; r++)
            for (int i = 0; i < 4; i++)
                add(p, (syl_t){.f0 = 660 * semi(notes[i]), .f1 = 660 * semi(notes[i] + .5f), .dur = .12f, .gap = i == 3 ? .25f : .03f,
                               .vib_rate = 7, .vib_depth = .015f, .bright = 2.5f});
        break;
    }
    case VOICE_RING: { /* "doo-dee-doo-dee" warbling up, twice, the second a fourth higher: a friendly ringtone */
        static const float notes[] = {0, 7, 4, 9};
        for (int r = 0; r < 2; r++)
            for (int i = 0; i < 4; i++)
                add(p, (syl_t){.f0 = 620 * semi(notes[i] + r * 5), .f1 = 620 * semi(notes[i] + r * 5 + .4f), .dur = .11f,
                               .gap = i == 3 ? .2f : .025f, .vib_rate = 11, .vib_depth = .025f, .bright = 2.4f, .open = 1.5f, .amp = .9f});
        break;
    }
    case VOICE_NOTE0: case VOICE_NOTE1: case VOICE_NOTE2: case VOICE_NOTE3: { /* a sung "doo": C5 E5 G5 C6, no wander */
        static const float hz[4] = {523.3f, 659.3f, 784.0f, 1046.5f};
        float f = hz[sound - VOICE_NOTE0];
        add(p, (syl_t){.f0 = f * 0.97f, .f1 = f, .dur = .22f, .vib_rate = 6, .vib_depth = .012f, .bright = 2.4f, .open = 1.4f, .amp = .95f});
        break;
    }
    case VOICE_TICK: /* not Pip: a soft low wooden "tok", like a picker wheel's detent; no pitch to squeak */
        add(p, (syl_t){.f0 = 760, .f1 = 520, .dur = .028f, .bright = 1.5f, .open = 1, .noise = .12f});
        break;
    case VOICE_TOCK: /* his own drum: low, round, over at once (the pitch wanders a little, as all his sounds) */
        add(p, (syl_t){.f0 = base * .62f, .f1 = base * .5f, .dur = .07f, .bright = 1.6f, .open = 1.1f, .amp = .9f});
        break;
    case VOICE_SNEEZE: /* "a-a-a-CHOO!": two open breaths climbing, the hissed "ch" and a round falling "oo" (pip.c
                          times the face to it: the burst comes SNEEZE_BURST s in) */
        add(p, (syl_t){.f0 = base * .85f, .f1 = base * 1.1f, .dur = .16f, .gap = .05f, .bright = 2.4f, .open = 2.6f, .noise = .12f, .amp = .8f});
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 1.5f, .dur = .21f, .gap = .03f, .bright = 2.4f, .open = 2.8f, .noise = .14f, .amp = .9f});
        add(p, (syl_t){.f0 = base * 2.2f, .f1 = base * 1.8f, .dur = .06f, .bright = 6, .open = 1, .noise = 2.2f, .amp = .9f}); /* ch */
        add(p, (syl_t){.f0 = base * 1.35f, .f1 = base * .7f, .dur = .24f, .bright = 1.8f, .open = 1.3f, .noise = .1f, .vib_rate = 8, .vib_depth = .03f});
        break;
    case VOICE_HMPH:
        add(p, (syl_t){.f0 = base * .5f, .f1 = base * .36f, .dur = .22f, .rough = .3f, .bright = 2.4f, .open = 1.2f, .noise = .08f});
        break;
    /* the ten newer feelings: each its own shape, all of the same soft sine palette */
    case VOICE_WHEE: /* "wheee!": a long climb that warbles faster as it goes, a little "ee" on top */
        add(p, (syl_t){.f0 = base * 1.05f, .f1 = base * 2.3f, .dur = .42f, .gap = .02f, .vib_rate = 9, .vib_depth = .03f, .bright = 2.6f, .open = 1.9f});
        add(p, (syl_t){.f0 = base * 2.3f, .f1 = base * 2.5f, .dur = .14f, .vib_rate = 14, .vib_depth = .025f, .bright = 2.4f, .open = 1.3f, .amp = .8f});
        break;
    case VOICE_HUM: { /* a hummed little tune with the mouth shut ("mm"): sol, mi, a long do, slow and wavering */
        static const float tune[3] = {0, -3, -7};
        for (int i = 0; i < 3; i++)
            add(p, (syl_t){.f0 = base * .95f * semi(tune[i]), .f1 = base * .95f * semi(tune[i] - .3f), .dur = i == 2 ? .5f : .26f, .gap = .04f,
                           .vib_rate = 5, .vib_depth = .022f, .bright = 1.5f, .open = 1.2f, .noise = .01f});
        break;
    }
    case VOICE_POUT: /* cheeks puffed: "mf!" through the nose, then a sulky low "mm" that turns away */
        add(p, (syl_t){.f0 = base * .74f, .f1 = base * .66f, .dur = .1f, .gap = .07f, .bright = 1.5f, .open = 1.1f, .noise = .3f, .amp = .85f});
        add(p, (syl_t){.f0 = base * .7f, .f1 = base * .56f, .dur = .28f, .vib_rate = 4, .vib_depth = .03f, .bright = 1.4f, .open = 1.15f});
        break;
    case VOICE_LOOP: /* round the loop twice, up and over and down, the second higher; then a giddy little "hi-hi" */
        for (int r = 0; r < 2; r++) {
            float k = r ? 1.12f : 1;
            add(p, (syl_t){.f0 = base * 1.1f * k, .f1 = base * 2.3f * k, .dur = .17f, .bright = 2.6f, .open = 1.8f});
            add(p, (syl_t){.f0 = base * 2.3f * k, .f1 = base * .95f * k, .dur = .22f, .gap = .05f, .vib_rate = 9, .vib_depth = .03f,
                           .bright = 2.4f, .open = 1.6f});
        }
        for (int i = 0; i < 2; i++)
            add(p, (syl_t){.f0 = base * 1.9f, .f1 = base * 2.1f, .dur = .06f, .gap = .05f, .bright = 2.8f, .open = 1.6f, .amp = .85f});
        break;
    case VOICE_BIRTHDAY: { /* "Happy Birthday to You", twice the first line, sung in his voice: notes in semitones from
                              the tonic, lengths in beats (a dotted pick-up, then even notes, the line's end held) */
        static const int8_t note[12] = {-5, -5, -3, -5, 0, -1, -5, -5, -3, -5, 2, 0};
        static const uint8_t beats4[12] = {3, 1, 4, 4, 4, 8, 3, 1, 4, 4, 4, 8}; /* in quarter beats */
        const float beat = .23f, tonic = 784; /* G5: high enough for the little speaker */
        for (int i = 0; i < 12; i++) {
            float f = tonic * semi(note[i]), d = beat * beats4[i] / 4;
            bool held = beats4[i] == 8;
            add(p, (syl_t){.f0 = f * semi(-.3f), .f1 = f, .dur = d * .86f, .gap = d * .14f + (held && i < 11 ? .08f : 0),
                           .vib_rate = held ? 6 : 0, .vib_depth = held ? .025f : 0, .bright = 2.6f, .open = 1.5f});
        }
        break;
    }
    case VOICE_TADA: /* "ta-daa!": a short pick-up and a held fifth above it, opening wide, a proud vibrato */
        add(p, (syl_t){.f0 = base * .95f, .f1 = base, .dur = .08f, .gap = .05f, .bright = 3, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 1.52f, .dur = .42f, .vib_rate = 6.5f, .vib_depth = .028f, .bright = 3, .open = 2});
        break;
    case VOICE_THINK: /* "pi-pi-pip": three quiet blips, the last one up, like a little computer working it out */
        for (int i = 0; i < 3; i++) {
            float f = base * (i == 2 ? 2.25f : i ? 1.85f : 2.0f);
            add(p, (syl_t){.f0 = f, .f1 = f, .dur = .045f, .gap = .07f, .bright = 1.6f, .open = 1});
        }
        break;
    case VOICE_AWW: /* "a-aww": a soft rise and a long breathy fall with a moved quaver in it */
        add(p, (syl_t){.f0 = base * 1.15f, .f1 = base * 1.3f, .dur = .12f, .bright = 2.4f, .open = 1.8f, .noise = .03f});
        add(p, (syl_t){.f0 = base * 1.3f, .f1 = base * .85f, .dur = .6f, .vib_rate = 6, .vib_depth = .04f, .bright = 2.3f, .open = 2, .noise = .04f});
        break;
    case VOICE_PEEK: /* "koo-koo!": a cuckoo's two notes, a major third down, round and bright */
        add(p, (syl_t){.f0 = base * 1.62f, .f1 = base * 1.66f, .dur = .13f, .gap = .08f, .bright = 2.6f, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 1.29f, .f1 = base * 1.26f, .dur = .18f, .bright = 2.6f, .open = 1.6f});
        break;
    case VOICE_PHEW: /* "f-phew": a hiss of breath, then a voiced sigh sinking out of it */
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base, .dur = .08f, .bright = 5, .open = 1, .noise = 1.4f, .amp = .55f});
        add(p, (syl_t){.f0 = base * .9f, .f1 = base * .5f, .dur = .48f, .bright = 2.4f, .open = 1.5f, .noise = .3f});
        break;
    case VOICE_LAUGH: /* "ha-ha-ha-ha": breathy, open bursts stepping down, slower and lower than a giggle */
        for (int i = 0, n = 4 + (int)rnd(0, 2); i < n; i++) {
            float f = base * (1.2f - i * 0.06f);
            add(p, (syl_t){.f0 = f, .f1 = f * .92f, .dur = rnd(.075f, .09f), .gap = rnd(.05f, .065f), .bright = 2.4f, .open = 2.3f, .noise = .16f});
        }
        break;
    case VOICE_THANKS: /* "mrr-nya!": a little purred thank-you, rolled low, then up and bright */
        add(p, (syl_t){.f0 = base * .78f, .f1 = base * .82f, .dur = .13f, .gap = .03f, .rough = .35f, .bright = 1.6f, .open = 1.2f});
        add(p, (syl_t){.f0 = base * 1.15f, .f1 = base * 1.5f, .dur = .17f, .vib_rate = 8, .vib_depth = .02f, .bright = 3, .open = 2.1f});
        break;
    case VOICE_FLAP: /* "fwip": a short breathy swoop up - quick enough for a tap a third of a second */
        add(p, (syl_t){.f0 = base * 0.9f, .f1 = base * 1.9f, .dur = .075f, .bright = 3.2f, .open = 1.7f, .noise = .35f, .amp = 1});
        break;
    case VOICE_POINT: { /* an achievement: two clear bell notes a fifth apart, the second held and shimmering (fixed pitch,
                           so every point sounds the same "you did it") */
        static const float note[2] = {1046.5f, 1568.0f}; /* C6, G6 */
        add(p, (syl_t){.f0 = note[0], .f1 = note[0] * 1.01f, .dur = .06f, .gap = .015f, .bright = 2.2f, .open = 1.5f, .vib_rate = 0});
        add(p, (syl_t){.f0 = note[1], .f1 = note[1] * 1.012f, .dur = .13f, .bright = 2.2f, .open = 1.6f, .vib_rate = 7, .vib_depth = .012f});
        break;
    }
    /* the everyday faces' sounds: the same soft palette */
    case VOICE_TAPTAP: /* "hm-hm-hm-hm": quick blips with the mouth shut, level, the last a touch up */
        for (int i = 0, n = 3 + (int)rnd(0, 2); i < n; i++) {
            float f = base * .85f * (i == n - 1 ? 1.08f : 1) * semi(rnd(-.4f, .4f));
            add(p, (syl_t){.f0 = f, .f1 = f * 1.04f, .dur = .07f, .gap = .065f, .bright = 1.6f, .open = 1.15f, .noise = .02f});
        }
        break;
    case VOICE_SIGH: /* a breath in, then a long soft "haaah" sinking, with a slow quaver */
        add(p, (syl_t){.f0 = base, .f1 = base * 1.1f, .dur = .12f, .gap = .04f, .bright = 5, .open = 1, .noise = .9f, .amp = .45f});
        add(p, (syl_t){.f0 = base * 1.05f, .f1 = base * .68f, .dur = .7f, .vib_rate = 5, .vib_depth = .03f, .bright = 2.2f, .open = 1.6f, .noise = .12f});
        break;
    case VOICE_MMM: /* "mmm-hmm": low, warm, the mouth shut, the second part sinking contentedly */
        add(p, (syl_t){.f0 = base * .74f, .f1 = base * .8f, .dur = .3f, .gap = .05f, .vib_rate = 5, .vib_depth = .02f, .bright = 1.4f, .open = 1.15f});
        add(p, (syl_t){.f0 = base * .86f, .f1 = base * .68f, .dur = .45f, .vib_rate = 4.5f, .vib_depth = .025f, .bright = 1.4f, .open = 1.2f, .noise = .02f});
        break;
    case VOICE_OOH: /* "oooh~": up into wonder, then held with a sparkly quaver */
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 1.6f, .dur = .24f, .bright = 2.4f, .open = 1.9f});
        add(p, (syl_t){.f0 = base * 1.6f, .f1 = base * 1.45f, .dur = .45f, .vib_rate = 7, .vib_depth = .035f, .bright = 2.4f, .open = 1.8f});
        break;
    case VOICE_SNICKER: /* "heh-heh-heh": low, breathy, quick, falling a little */
        for (int i = 0, n = 3 + (int)rnd(0, 2); i < n; i++) {
            float f = base * (.95f - i * .04f);
            add(p, (syl_t){.f0 = f, .f1 = f * .9f, .dur = rnd(.05f, .065f), .gap = rnd(.045f, .06f), .bright = 2.2f, .open = 1.5f, .noise = .25f});
        }
        break;
    case VOICE_EHE: /* "e-he...": a small step up, then a little embarrassed fall */
        add(p, (syl_t){.f0 = base * 1.05f, .f1 = base * 1.15f, .dur = .09f, .gap = .06f, .bright = 2.2f, .open = 1.4f, .noise = .04f});
        add(p, (syl_t){.f0 = base * 1.22f, .f1 = base * .95f, .dur = .24f, .vib_rate = 6, .vib_depth = .03f, .bright = 2.2f, .open = 1.5f, .amp = .9f});
        break;
    case VOICE_HUP: /* "hup!": a breath, then one short punchy rise */
        add(p, (syl_t){.f0 = base, .f1 = base, .dur = .05f, .gap = .01f, .bright = 5, .open = 1, .noise = 1, .amp = .4f});
        add(p, (syl_t){.f0 = base * .9f, .f1 = base * 1.15f, .dur = .1f, .bright = 3, .open = 2.2f});
        break;
    case VOICE_AHA: /* "a-HA!": a low "a", the "HA" leaping up, and a little bell over it */
        add(p, (syl_t){.f0 = base * .85f, .f1 = base * .9f, .dur = .1f, .gap = .04f, .bright = 2.6f, .open = 1.8f});
        add(p, (syl_t){.f0 = base * 1.3f, .f1 = base * 1.75f, .dur = .2f, .gap = .02f, .vib_rate = 8, .vib_depth = .02f, .bright = 3, .open = 2.2f});
        add(p, (syl_t){.f0 = base * 3, .f1 = base * 3.03f, .dur = .12f, .bright = 1.5f, .open = 1, .amp = .5f});
        break;
    case VOICE_UGH: /* "uuugh": long, flat and sinking, a little rough */
        add(p, (syl_t){.f0 = base * .95f, .f1 = base * .6f, .dur = .55f, .rough = .25f, .vib_rate = 4, .vib_depth = .02f, .bright = 2, .open = 1.3f, .noise = .1f});
        break;
    case VOICE_KYA: /* "kya!": a click, a high squeak, and two giggling bubbles */
        add(p, (syl_t){.f0 = base * 2, .f1 = base * 2, .dur = .025f, .bright = 6, .open = 1, .noise = 1, .amp = .4f});
        add(p, (syl_t){.f0 = base * 1.9f, .f1 = base * 2.4f, .dur = .11f, .gap = .06f, .bright = 2.8f, .open = 1.8f});
        for (int i = 0; i < 2; i++) {
            float f = base * (1.7f - i * .1f);
            add(p, (syl_t){.f0 = f, .f1 = f * 1.12f, .dur = .05f, .gap = .04f, .bright = 2.6f, .open = 1.8f, .vib_rate = 16, .vib_depth = .02f, .noise = .03f, .amp = .85f});
        }
        break;
    case VOICE_HIC: /* "hic!": abrupt - a catch of breath and a short leap up */
        add(p, (syl_t){.f0 = base * 1.4f, .f1 = base * 1.4f, .dur = .02f, .bright = 5, .open = 1, .noise = .8f, .amp = .5f});
        add(p, (syl_t){.f0 = base * 1.55f, .f1 = base * 2.3f, .dur = .06f, .bright = 3.4f, .open = 2.4f, .noise = .12f});
        break;
    case VOICE_TOOT: { /* "pfrrt": the lips buzzing - glottal pulses through a low vowel, like the burp's throat but a short
                          sharp "p" first and a buzz that wobbles; a short "pft", a squeaky rising "piiip", or a long one */
        int kind = (int)rnd(0, 100);
        bool squeak = kind < 30, lng = kind >= 30 && kind < 55;
        add(p, (syl_t){.f0 = base, .f1 = base, .dur = .025f, .gap = .005f, .bright = 6, .open = 1, .noise = 1.2f, .amp = .5f}); /* p */
        float f = squeak ? rnd(170, 200) : rnd(88, 104), d = squeak ? rnd(.22f, .3f) : lng ? rnd(.5f, .62f) : rnd(.14f, .2f);
        float o = squeak ? rnd(620, 700) : rnd(440, 520);
        add(p, (syl_t){.wave = 1, .f0 = f, .f1 = f * (squeak ? 1.25f : rnd(.75f, .85f)), .dur = d, .formant = o, .formant2 = o * 1.8f,
                       .open = 1.3f, .damp = .35f, .jitter = squeak ? .05f : .2f, .shimmer = .3f, .noise = .08f, .vib_rate = squeak ? 9 : 14,
                       .vib_depth = squeak ? .04f : .08f, .gurgle = squeak ? 0 : .35f, .amp = squeak ? .7f : lng ? 1.1f : 1});
        break;
    }
    /* his birth: bells out of the dark, fixed pitch (the same in every Pip: his first sounds are not yet his) */
    case VOICE_SPARK: /* one glassy "ting" (E6): a bright strike, then the ring, softer, shimmering on */
        add(p, (syl_t){.f0 = 1318.5f, .f1 = 1321, .dur = .05f, .bright = 2.4f, .open = 1.6f});
        add(p, (syl_t){.f0 = 1320, .f1 = 1316, .dur = .55f, .vib_rate = 6, .vib_depth = .008f, .bright = 1.7f, .open = 1.25f, .amp = .55f});
        break;
    case VOICE_STARDUST: { /* twinkles up a pentatonic scale, C5 to G6, each a quick strike, growing brighter and louder */
        static const float hz[9] = {523.3f, 587.3f, 659.3f, 784.0f, 880.0f, 1046.5f, 1174.7f, 1318.5f, 1568.0f};
        for (int i = 0; i < 9; i++) {
            float k = i / 8.0f;
            add(p, (syl_t){.f0 = hz[i], .f1 = hz[i] * 1.006f, .dur = i == 8 ? .32f : .065f, .gap = i == 8 ? 0 : .075f - .03f * k,
                           .vib_rate = i == 8 ? 7 : 0, .vib_depth = .012f, .bright = 1.8f + .6f * k, .open = 1.4f, .amp = .45f + .55f * k});
        }
        break;
    }
    case VOICE_BLOOM: /* a breathy swell rising into it, then C5-G5 and a held C6 ringing out */
        add(p, (syl_t){.f0 = 260, .f1 = 820, .dur = .32f, .bright = 5, .open = 1.2f, .noise = .9f, .amp = .55f});
        add(p, (syl_t){.f0 = 523.3f, .f1 = 527, .dur = .07f, .gap = .01f, .bright = 2.6f, .open = 1.7f});
        add(p, (syl_t){.f0 = 784, .f1 = 789, .dur = .07f, .gap = .01f, .bright = 2.6f, .open = 1.8f});
        add(p, (syl_t){.f0 = 1046.5f, .f1 = 1050, .dur = .62f, .vib_rate = 6.5f, .vib_depth = .014f, .bright = 2.2f, .open = 1.7f});
        break;
    /* the newer answers' sounds: the same soft palette */
    case VOICE_MMPH: /* "mmf-mmf!": muffled, the mouth shut, a little rough, up then down */
        add(p, (syl_t){.f0 = base * .95f, .f1 = base * 1.15f, .dur = .12f, .gap = .05f, .rough = .2f, .bright = 1.35f, .open = 1.08f, .noise = .05f});
        add(p, (syl_t){.f0 = base * 1.2f, .f1 = base * .9f, .dur = .16f, .rough = .2f, .bright = 1.35f, .open = 1.08f, .noise = .05f});
        break;
    case VOICE_BOING: /* "b-oi-oi-oing": a soft onset, then one glide up with a wide, quick wobble, like a spring let go */
        add(p, (syl_t){.f0 = base * .7f, .f1 = base * .75f, .dur = .03f, .bright = 3, .open = 1.2f, .noise = .3f, .amp = .6f});
        add(p, (syl_t){.f0 = base * .8f, .f1 = base * 1.7f, .dur = .46f, .vib_rate = 17, .vib_depth = .11f, .bright = 2.4f, .open = 1.8f});
        break;
    case VOICE_EEK: /* "eek-eek-eek!": three high squeaks, each a little higher */
        for (int i = 0; i < 3; i++) {
            float f = base * (2.1f + .18f * i);
            add(p, (syl_t){.f0 = f, .f1 = f * 1.12f, .dur = .07f, .gap = .045f, .bright = 2.8f, .open = 1.9f, .noise = .04f});
        }
        break;
    case VOICE_PINCH: /* "nnngh-ip!": squeezed - a strained hum with the mouth shut, rising and trembling, then a squeak popping out */
        add(p, (syl_t){.f0 = base * .9f, .f1 = base * 1.35f, .dur = .3f, .rough = .18f, .vib_rate = 14, .vib_depth = .03f, .bright = 1.4f, .open = 1.1f, .noise = .04f});
        add(p, (syl_t){.f0 = base * 2.1f, .f1 = base * 2.6f, .dur = .07f, .bright = 3, .open = 2.2f});
        break;
    case VOICE_YAY: /* "ya-aay!": a pick-up, then a big bright leap held with a glad vibrato */
        add(p, (syl_t){.f0 = base * 1.2f, .f1 = base * 1.35f, .dur = .1f, .gap = .03f, .bright = 3, .open = 2});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 1.95f, .dur = .32f, .vib_rate = 7, .vib_depth = .03f, .bright = 3, .open = 2.2f});
        break;
    case VOICE_NUZZLE: /* "nnn~": low, the mouth shut, a purr in it, rising and settling */
        add(p, (syl_t){.f0 = base * .78f, .f1 = base * .95f, .dur = .42f, .gap = .02f, .rough = .15f, .vib_rate = 5, .vib_depth = .02f, .bright = 1.5f, .open = 1.2f});
        add(p, (syl_t){.f0 = base * .95f, .f1 = base * .8f, .dur = .34f, .rough = .12f, .vib_rate = 4.5f, .vib_depth = .025f, .bright = 1.5f, .open = 1.2f, .noise = .02f});
        break;
    case VOICE_OOHOOH: /* "oo-oo-oo!": quick breathy pants stepping up */
        for (int i = 0; i < 3 + (int)rnd(0, 2); i++) {
            float f = base * (1.1f + .14f * i);
            add(p, (syl_t){.f0 = f, .f1 = f * 1.1f, .dur = .08f, .gap = .055f, .bright = 2.4f, .open = 1.9f, .noise = .14f});
        }
        break;
    case VOICE_YUM: /* "mnyam-mnyam": a shut "m", then the mouth opening on "nyam", twice */
        for (int i = 0; i < 2; i++) {
            float f = base * (1 - .06f * i);
            add(p, (syl_t){.f0 = f * .9f, .f1 = f * .95f, .dur = .06f, .bright = 1.4f, .open = 1.05f});
            add(p, (syl_t){.f0 = f * 1.08f, .f1 = f * .94f, .dur = .12f, .gap = .07f, .bright = 2.6f, .open = 2});
        }
        break;
    case VOICE_FANFARE: /* "da-da-DAA!": two quick notes up a major third, then the fifth held, opening wide */
        add(p, (syl_t){.f0 = base, .f1 = base, .dur = .07f, .gap = .035f, .bright = 3, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 1.26f, .f1 = base * 1.26f, .dur = .07f, .gap = .035f, .bright = 3, .open = 1.7f});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 1.52f, .dur = .4f, .vib_rate = 6.5f, .vib_depth = .03f, .bright = 3, .open = 2.2f});
        break;
    case VOICE_DEFLATE: /* "pfff-iuuu": a hiss of air, then a long whistle sinking away */
        add(p, (syl_t){.f0 = base, .f1 = base, .dur = .12f, .bright = 5, .open = 1, .noise = 1.3f, .amp = .5f});
        add(p, (syl_t){.f0 = base * 1.7f, .f1 = base * .5f, .dur = .62f, .vib_rate = 3.5f, .vib_depth = .02f, .bright = 2, .open = 1.4f, .noise = .2f});
        break;
    case VOICE_SQUEAK: /* "squee-squee" squeaky clean, and a little ting */
        for (int i = 0; i < 2; i++)
            add(p, (syl_t){.f0 = base * 2.3f, .f1 = base * 2.9f, .dur = .065f, .gap = .06f, .bright = 2.2f, .open = 1.5f});
        add(p, (syl_t){.f0 = base * 3.2f, .f1 = base * 3.22f, .dur = .16f, .vib_rate = 7, .vib_depth = .01f, .bright = 1.6f, .open = 1.1f, .amp = .55f});
        break;
    /* ---- the thirty-eight, two sounds each ---- */
    case VOICE_HEEHEE: /* squeezed high "hi-hi-hi-hi" that cannot stop, a gasp, and again */
        add(p, (syl_t){.f0 = base * 1.55f, .f1 = base * 1.8f, .dur = 0.06f, .gap = 0.035f, .bright = 2.3f, .open = 1.5f, .amp = 1.0f});
        add(p, (syl_t){.f0 = base * 1.6f, .f1 = base * 1.85f, .dur = 0.06f, .gap = 0.035f, .bright = 2.3f, .open = 1.5f, .amp = 0.97f});
        add(p, (syl_t){.f0 = base * 1.65f, .f1 = base * 1.9f, .dur = 0.06f, .gap = 0.035f, .bright = 2.3f, .open = 1.5f, .amp = 0.94f});
        add(p, (syl_t){.f0 = base * 1.7f, .f1 = base * 1.95f, .dur = 0.06f, .gap = 0.035f, .bright = 2.3f, .open = 1.5f, .amp = 0.91f});
        add(p, (syl_t){.f0 = base * 1.75f, .f1 = base * 2.0f, .dur = 0.06f, .gap = 0.035f, .bright = 2.3f, .open = 1.5f, .amp = 0.88f});
        add(p, (syl_t){.f0 = base * 0.9f, .f1 = base * 1.5f, .dur = 0.16f, .gap = 0.05f, .bright = 1.8f, .open = 1.6f, .noise = 0.06f});
        add(p, (syl_t){.f0 = base * 1.75f, .f1 = base * 2.0f, .dur = 0.06f, .gap = 0.035f, .bright = 2.3f, .open = 1.5f, .amp = 1.0f});
        add(p, (syl_t){.f0 = base * 1.69f, .f1 = base * 1.94f, .dur = 0.06f, .gap = 0.035f, .bright = 2.3f, .open = 1.5f, .amp = 0.97f});
        add(p, (syl_t){.f0 = base * 1.63f, .f1 = base * 1.88f, .dur = 0.06f, .gap = 0.035f, .bright = 2.3f, .open = 1.5f, .amp = 0.94f});
        add(p, (syl_t){.f0 = base * 1.57f, .f1 = base * 1.82f, .dur = 0.06f, .gap = 0.035f, .bright = 2.3f, .open = 1.5f, .amp = 0.91f});
        add(p, (syl_t){.f0 = base * 1.51f, .f1 = base * 1.76f, .dur = 0.06f, .gap = 0.035f, .bright = 2.3f, .open = 1.5f, .amp = 0.88f});
        break;
    case VOICE_WHEEZE: /* out of breath with laughing: "ha-ha... haa... hii" */
        add(p, (syl_t){.f0 = base * 1.4f, .f1 = base * 1.2f, .dur = 0.085f, .gap = 0.045f, .bright = 2.1f, .open = 1.7f, .amp = 1.0f});
        add(p, (syl_t){.f0 = base * 1.34f, .f1 = base * 1.14f, .dur = 0.085f, .gap = 0.045f, .bright = 2.1f, .open = 1.7f, .amp = 0.97f});
        add(p, (syl_t){.f0 = base * 1.28f, .f1 = base * 1.08f, .dur = 0.085f, .gap = 0.045f, .bright = 2.1f, .open = 1.7f, .amp = 0.94f});
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 1.5f, .dur = 0.2f, .gap = 0.06f, .bright = 1.8f, .open = 1.7f, .noise = 0.08f});
        add(p, (syl_t){.f0 = base * 1.55f, .f1 = base * 1.15f, .dur = 0.26f, .gap = 0.05f, .vib_rate = 9.0f, .vib_depth = 0.03f, .bright = 2.0f, .open = 1.8f});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 1.85f, .dur = 0.18f, .vib_rate = 11.0f, .vib_depth = 0.03f, .bright = 2.0f, .open = 1.4f});
        break;
    case VOICE_ROLLING: /* a laugh going round: down one side and up the other, twice */
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 1.35f, .dur = 0.085f, .gap = 0.045f, .bright = 2.0f, .open = 1.8f});
        add(p, (syl_t){.f0 = base * 1.37f, .f1 = base * 1.22f, .dur = 0.085f, .gap = 0.045f, .bright = 2.0f, .open = 1.8f});
        add(p, (syl_t){.f0 = base * 1.24f, .f1 = base * 1.09f, .dur = 0.085f, .gap = 0.045f, .bright = 2.0f, .open = 1.8f});
        add(p, (syl_t){.f0 = base * 1.11f, .f1 = base * 0.96f, .dur = 0.085f, .gap = 0.045f, .bright = 2.0f, .open = 1.8f});
        add(p, (syl_t){.f0 = base * 1.11f, .f1 = base * 1.22f, .dur = 0.085f, .gap = 0.045f, .bright = 2.0f, .open = 1.8f});
        add(p, (syl_t){.f0 = base * 1.26f, .f1 = base * 1.37f, .dur = 0.085f, .gap = 0.045f, .bright = 2.0f, .open = 1.8f});
        add(p, (syl_t){.f0 = base * 1.41f, .f1 = base * 1.52f, .dur = 0.085f, .gap = 0.045f, .bright = 2.0f, .open = 1.8f});
        add(p, (syl_t){.f0 = base * 1.56f, .f1 = base * 1.67f, .dur = 0.085f, .gap = 0.045f, .bright = 2.0f, .open = 1.8f});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 1.7f, .dur = 0.1f, .gap = 0.04f, .bright = 2.2f, .open = 1.7f});
        add(p, (syl_t){.f0 = base * 1.65f, .f1 = base * 1.1f, .dur = 0.3f, .vib_rate = 7.0f, .vib_depth = 0.03f, .bright = 2.2f, .open = 1.9f});
        break;
    case VOICE_HOHO: /* "o-ho-ho-ho": round and low, from the belly */
        add(p, (syl_t){.f0 = base * 0.85f, .f1 = base * 1.0f, .dur = 0.16f, .gap = 0.05f, .bright = 1.8f, .open = 1.9f});
        add(p, (syl_t){.f0 = base * 0.95f, .f1 = base * 0.78f, .dur = 0.13f, .gap = 0.06f, .bright = 1.8f, .open = 1.9f, .amp = 1.0f});
        add(p, (syl_t){.f0 = base * 0.93f, .f1 = base * 0.76f, .dur = 0.138f, .gap = 0.068f, .bright = 1.8f, .open = 1.9f, .amp = 0.97f});
        add(p, (syl_t){.f0 = base * 0.91f, .f1 = base * 0.74f, .dur = 0.14600000000000002f, .gap = 0.076f, .bright = 1.8f, .open = 1.9f, .amp = 0.94f});
        add(p, (syl_t){.f0 = base * 0.89f, .f1 = base * 0.72f, .dur = 0.154f, .gap = 0.08399999999999999f, .bright = 1.8f, .open = 1.9f, .amp = 0.91f});
        add(p, (syl_t){.f0 = base * 0.9f, .f1 = base * 0.62f, .dur = 0.32f, .vib_rate = 6.0f, .vib_depth = 0.03f, .bright = 1.7f, .open = 1.6f});
        break;
    case VOICE_BOINGS: /* "bo-ing, bo-ing, bo-ing": three springs, each higher */
        add(p, (syl_t){.f0 = base * 0.7f, .f1 = base * 1.5f, .dur = 0.1f, .bright = 1.9f, .open = 1.5f});
        add(p, (syl_t){.f0 = base * 1.4f, .f1 = base * 0.95f, .dur = 0.16f, .gap = 0.05f, .vib_rate = 14.0f, .vib_depth = 0.05f, .bright = 1.8f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 0.85f, .f1 = base * 1.7f, .dur = 0.1f, .bright = 1.9f, .open = 1.5f});
        add(p, (syl_t){.f0 = base * 1.5999999999999999f, .f1 = base * 1.0999999999999999f, .dur = 0.16f, .gap = 0.05f, .vib_rate = 14.0f, .vib_depth = 0.05f, .bright = 1.8f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.0f, .f1 = base * 1.9f, .dur = 0.1f, .bright = 1.9f, .open = 1.5f});
        add(p, (syl_t){.f0 = base * 1.7999999999999998f, .f1 = base * 1.25f, .dur = 0.16f, .gap = 0.05f, .vib_rate = 14.0f, .vib_depth = 0.05f, .bright = 1.8f, .open = 1.4f});
        break;
    case VOICE_YIPPEE: /* "yip-piii!" */
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 1.6f, .dur = 0.09f, .gap = 0.03f, .bright = 2.4f, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 2.3f, .dur = 0.13f, .bright = 2.2f, .open = 1.8f});
        add(p, (syl_t){.f0 = base * 2.3f, .f1 = base * 1.75f, .dur = 0.38f, .vib_rate = 8.0f, .vib_depth = 0.035f, .bright = 2.2f, .open = 1.7f});
        break;
    case VOICE_PETALS: /* six flowers opening: a soft bell each, round about */
        add(p, (syl_t){.f0 = 643.9f, .f1 = 659.0f, .dur = 0.12f, .gap = 0.03f, .bright = 1.7f, .open = 1.35f, .amp = 0.8f});
        add(p, (syl_t){.f0 = 811.3f, .f1 = 830.3f, .dur = 0.12f, .gap = 0.03f, .bright = 1.7f, .open = 1.35f, .amp = 0.85f});
        add(p, (syl_t){.f0 = 964.8f, .f1 = 987.4f, .dur = 0.12f, .gap = 0.03f, .bright = 1.7f, .open = 1.35f, .amp = 0.9f});
        add(p, (syl_t){.f0 = 1287.9f, .f1 = 1318.0f, .dur = 0.12f, .gap = 0.03f, .bright = 1.7f, .open = 1.35f, .amp = 0.9f});
        add(p, (syl_t){.f0 = 1083.0f, .f1 = 1108.3f, .dur = 0.12f, .gap = 0.03f, .bright = 1.7f, .open = 1.35f, .amp = 0.95f});
        add(p, (syl_t){.f0 = 1622.6f, .f1 = 1660.6f, .dur = 0.42f, .vib_rate = 6.0f, .vib_depth = 0.01f, .bright = 1.7f, .open = 1.35f, .amp = 0.9f});
        break;
    case VOICE_SPRINGSONG: /* a little hummed skip: "tra-la-la" */
        add(p, (syl_t){.f0 = base * 0.977f, .f1 = base * 1.0f, .dur = 0.12f, .gap = 0.03f, .bright = 2.0f, .open = 1.8f});
        add(p, (syl_t){.f0 = base * 1.231f, .f1 = base * 1.26f, .dur = 0.09f, .gap = 0.03f, .bright = 2.0f, .open = 1.8f});
        add(p, (syl_t){.f0 = base * 1.464f, .f1 = base * 1.498f, .dur = 0.16f, .gap = 0.03f, .bright = 2.0f, .open = 1.8f});
        add(p, (syl_t){.f0 = base * 1.231f, .f1 = base * 1.26f, .dur = 0.09f, .gap = 0.03f, .bright = 2.0f, .open = 1.8f});
        add(p, (syl_t){.f0 = base * 1.643f, .f1 = base * 1.682f, .dur = 0.16f, .gap = 0.03f, .bright = 2.0f, .open = 1.8f});
        add(p, (syl_t){.f0 = base * 1.464f, .f1 = base * 1.498f, .dur = 0.3f, .vib_rate = 6.0f, .vib_depth = 0.02f, .bright = 2.0f, .open = 1.8f});
        break;
    case VOICE_LUBDUB: /* a heart: "bu-bum... bu-bum", warmer each time, and a sigh */
        add(p, (syl_t){.f0 = base * 0.62f, .f1 = base * 0.5f, .dur = 0.09f, .gap = 0.05f, .bright = 1.8f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 0.75f, .f1 = base * 0.55f, .dur = 0.13f, .gap = 0.18f, .bright = 1.8f, .open = 1.5f});
        add(p, (syl_t){.f0 = base * 0.66f, .f1 = base * 0.5f, .dur = 0.09f, .gap = 0.05f, .bright = 1.8f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 0.8f, .f1 = base * 0.55f, .dur = 0.13f, .gap = 0.18f, .bright = 1.8f, .open = 1.5f});
        add(p, (syl_t){.f0 = base * 0.7f, .f1 = base * 0.5f, .dur = 0.09f, .gap = 0.05f, .bright = 1.8f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 0.85f, .f1 = base * 0.55f, .dur = 0.13f, .gap = 0.18f, .bright = 1.8f, .open = 1.5f});
        add(p, (syl_t){.f0 = base * 1.2f, .f1 = base * 0.85f, .dur = 0.4f, .vib_rate = 6.0f, .vib_depth = 0.03f, .bright = 2.0f, .open = 1.9f, .noise = 0.03f});
        break;
    case VOICE_SWOON: /* "aaah": up, held, melting down */
        add(p, (syl_t){.f0 = base * 1.0f, .f1 = base * 1.7f, .dur = 0.26f, .bright = 2.2f, .open = 2.0f});
        add(p, (syl_t){.f0 = base * 1.7f, .f1 = base * 1.65f, .dur = 0.28f, .vib_rate = 6.0f, .vib_depth = 0.03f, .bright = 2.2f, .open = 2.0f});
        add(p, (syl_t){.f0 = base * 1.65f, .f1 = base * 0.9f, .dur = 0.42f, .vib_rate = 5.0f, .vib_depth = 0.035f, .bright = 1.9f, .open = 1.5f});
        break;
    case VOICE_ROCKETUP: /* a whistle up, a hush, and a shower of soft bells */
        add(p, (syl_t){.f0 = base * 0.8f, .f1 = base * 2.6f, .dur = 0.5f, .gap = 0.1f, .bright = 1.5f, .open = 1.15f, .amp = 0.75f});
        add(p, (syl_t){.f0 = 2044.2f, .f1 = 2092.0f, .dur = 0.07f, .gap = 0.02f, .bright = 1.6f, .open = 1.3f, .amp = 1.0f});
        add(p, (syl_t){.f0 = 1531.4f, .f1 = 1567.2f, .dur = 0.07f, .gap = 0.02f, .bright = 1.6f, .open = 1.3f, .amp = 0.95f});
        add(p, (syl_t){.f0 = 1719.0f, .f1 = 1759.2f, .dur = 0.07f, .gap = 0.02f, .bright = 1.6f, .open = 1.3f, .amp = 0.9f});
        add(p, (syl_t){.f0 = 1287.8f, .f1 = 1317.9f, .dur = 0.07f, .gap = 0.02f, .bright = 1.6f, .open = 1.3f, .amp = 0.85f});
        add(p, (syl_t){.f0 = 1531.4f, .f1 = 1567.2f, .dur = 0.07f, .gap = 0.02f, .bright = 1.6f, .open = 1.3f, .amp = 0.8f});
        add(p, (syl_t){.f0 = 1022.1f, .f1 = 1046.0f, .dur = 0.07f, .gap = 0.02f, .bright = 1.6f, .open = 1.3f, .amp = 0.75f});
        add(p, (syl_t){.f0 = 1287.8f, .f1 = 1317.9f, .dur = 0.09f, .gap = 0.02f, .bright = 1.6f, .open = 1.3f, .amp = 0.7f});
        add(p, (syl_t){.f0 = 765.7f, .f1 = 783.6f, .dur = 0.26f, .vib_rate = 6.0f, .vib_depth = 0.01f, .bright = 1.6f, .open = 1.3f, .amp = 0.65f});
        break;
    case VOICE_OOHAAH: /* the crowd: "ooo-ooh... aa-aah!" */
        add(p, (syl_t){.f0 = base * 0.9f, .f1 = base * 1.25f, .dur = 0.34f, .gap = 0.08f, .vib_rate = 5.0f, .vib_depth = 0.02f, .bright = 1.6f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.2f, .f1 = base * 1.8f, .dur = 0.2f, .bright = 2.2f, .open = 2.1f});
        add(p, (syl_t){.f0 = base * 1.8f, .f1 = base * 1.4f, .dur = 0.34f, .vib_rate = 6.0f, .vib_depth = 0.03f, .bright = 2.2f, .open = 2.0f});
        break;
    case VOICE_MEDALIST: /* a short march home: "ta-ta-ta taa, ta-taa" */
        add(p, (syl_t){.f0 = 511.1f, .f1 = 523.0f, .dur = 0.08f, .gap = 0.025f, .bright = 2.0f, .open = 1.5f});
        add(p, (syl_t){.f0 = 511.1f, .f1 = 523.0f, .dur = 0.08f, .gap = 0.025f, .bright = 2.0f, .open = 1.5f});
        add(p, (syl_t){.f0 = 511.1f, .f1 = 523.0f, .dur = 0.08f, .gap = 0.025f, .bright = 2.0f, .open = 1.5f});
        add(p, (syl_t){.f0 = 765.7f, .f1 = 783.6f, .dur = 0.2f, .gap = 0.025f, .vib_rate = 6.0f, .vib_depth = 0.015f, .bright = 2.0f, .open = 1.5f});
        add(p, (syl_t){.f0 = 643.9f, .f1 = 658.9f, .dur = 0.1f, .gap = 0.025f, .bright = 2.0f, .open = 1.5f});
        add(p, (syl_t){.f0 = 1022.1f, .f1 = 1046.0f, .dur = 0.46f, .vib_rate = 6.0f, .vib_depth = 0.015f, .bright = 2.0f, .open = 1.5f});
        break;
    case VOICE_MHM: /* pleased with himself: "mm-hm. Mhmm!" */
        add(p, (syl_t){.f0 = base * 0.8f, .f1 = base * 0.95f, .dur = 0.14f, .gap = 0.06f, .bright = 1.4f, .open = 1.15f});
        add(p, (syl_t){.f0 = base * 1.0f, .f1 = base * 0.85f, .dur = 0.16f, .gap = 0.12f, .bright = 1.4f, .open = 1.15f});
        add(p, (syl_t){.f0 = base * 0.85f, .f1 = base * 1.3f, .dur = 0.22f, .gap = 0.02f, .bright = 1.5f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 1.3f, .f1 = base * 1.1f, .dur = 0.2f, .vib_rate = 7.0f, .vib_depth = 0.02f, .bright = 1.7f, .open = 1.5f});
        break;
    case VOICE_DISCOBEAT: /* "oon-tsa" of the mouth: a low "doom", a light "ti", and a hook */
        add(p, (syl_t){.f0 = base * 0.6f, .f1 = base * 0.45f, .dur = 0.12f, .gap = 0.09f, .bright = 1.7f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 1.45f, .dur = 0.05f, .gap = 0.1f, .bright = 1.8f, .open = 1.2f, .amp = 0.7f});
        add(p, (syl_t){.f0 = base * 0.6f, .f1 = base * 0.45f, .dur = 0.12f, .gap = 0.09f, .bright = 1.7f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 1.45f, .dur = 0.05f, .gap = 0.06f, .bright = 1.8f, .open = 1.2f, .amp = 0.7f});
        add(p, (syl_t){.f0 = base * 0.977f, .f1 = base * 1.0f, .dur = 0.09f, .gap = 0.025f, .bright = 2.0f, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 1.162f, .f1 = base * 1.189f, .dur = 0.09f, .gap = 0.025f, .bright = 2.0f, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 1.304f, .f1 = base * 1.335f, .dur = 0.09f, .gap = 0.025f, .bright = 2.0f, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 1.464f, .f1 = base * 1.498f, .dur = 0.09f, .gap = 0.025f, .bright = 2.0f, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 1.741f, .f1 = base * 1.782f, .dur = 0.28f, .vib_rate = 6.0f, .vib_depth = 0.02f, .bright = 2.0f, .open = 1.6f});
        break;
    case VOICE_GROOVY: /* "ua-ua, ua-ua-uu": a wah-wah */
        add(p, (syl_t){.f0 = base * 0.95f, .f1 = base * 1.0f, .dur = 0.13f, .gap = 0.03f, .bright = 1.2f, .open = 2.6f});
        add(p, (syl_t){.f0 = base * 1.04f, .f1 = base * 1.09f, .dur = 0.13f, .gap = 0.03f, .bright = 1.2f, .open = 2.6f});
        add(p, (syl_t){.f0 = base * 1.13f, .f1 = base * 1.18f, .dur = 0.13f, .gap = 0.03f, .bright = 1.2f, .open = 2.6f});
        add(p, (syl_t){.f0 = base * 0.95f, .f1 = base * 1.0f, .dur = 0.13f, .gap = 0.03f, .bright = 1.2f, .open = 2.6f});
        add(p, (syl_t){.f0 = base * 1.04f, .f1 = base * 1.09f, .dur = 0.13f, .gap = 0.03f, .bright = 1.2f, .open = 2.6f});
        add(p, (syl_t){.f0 = base * 1.25f, .f1 = base * 0.8f, .dur = 0.34f, .vib_rate = 6.0f, .vib_depth = 0.03f, .bright = 1.3f, .open = 2.4f});
        break;
    case VOICE_EYEPOP: /* "hm?.. bwo-OOP!": two eyes out on springs */
        add(p, (syl_t){.f0 = base * 0.85f, .f1 = base * 1.0f, .dur = 0.12f, .gap = 0.22f, .bright = 1.5f, .open = 1.2f});
        add(p, (syl_t){.f0 = base * 0.8f, .f1 = base * 2.1f, .dur = 0.16f, .bright = 1.9f, .open = 1.7f});
        add(p, (syl_t){.f0 = base * 2.1f, .f1 = base * 1.6f, .dur = 0.22f, .vib_rate = 16.0f, .vib_depth = 0.07f, .bright = 2.0f, .open = 1.5f});
        break;
    case VOICE_WHOANELLY: /* "wo-o-o-ow": down and up again, not believing it */
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 0.9f, .dur = 0.22f, .bright = 2.0f, .open = 1.9f});
        add(p, (syl_t){.f0 = base * 0.9f, .f1 = base * 1.9f, .dur = 0.34f, .vib_rate = 6.0f, .vib_depth = 0.03f, .bright = 2.2f, .open = 2.2f});
        add(p, (syl_t){.f0 = base * 1.9f, .f1 = base * 1.5f, .dur = 0.22f, .bright = 2.1f, .open = 1.7f});
        break;
    case VOICE_IDEAPING: /* three drops of thought... and "ting!" */
        add(p, (syl_t){.f0 = base * 0.9f, .f1 = base * 0.95f, .dur = 0.06f, .gap = 0.3f, .bright = 1.6f, .open = 1.2f, .amp = 0.75f});
        add(p, (syl_t){.f0 = base * 0.95f, .f1 = base * 1.0f, .dur = 0.06f, .gap = 0.3f, .bright = 1.6f, .open = 1.2f, .amp = 0.8f});
        add(p, (syl_t){.f0 = base * 1.0f, .f1 = base * 1.05f, .dur = 0.06f, .gap = 0.28f, .bright = 1.6f, .open = 1.2f, .amp = 0.85f});
        add(p, (syl_t){.f0 = 1318, .f1 = 1320, .dur = 0.06f, .gap = 0.02f, .bright = 1.9f, .open = 1.4f});
        add(p, (syl_t){.f0 = 1976, .f1 = 1972, .dur = 0.5f, .vib_rate = 7.0f, .vib_depth = 0.01f, .bright = 1.5f, .open = 1.2f});
        break;
    case VOICE_GOTIT: /* "hmm... a! a-ha-a!" */
        add(p, (syl_t){.f0 = base * 0.75f, .f1 = base * 0.85f, .dur = 0.3f, .gap = 0.08f, .vib_rate = 5.0f, .vib_depth = 0.02f, .bright = 1.4f, .open = 1.2f});
        add(p, (syl_t){.f0 = base * 1.4f, .f1 = base * 1.7f, .dur = 0.09f, .gap = 0.06f, .bright = 2.2f, .open = 1.8f});
        add(p, (syl_t){.f0 = base * 1.2f, .f1 = base * 1.35f, .dur = 0.08f, .gap = 0.02f, .bright = 2.1f, .open = 1.7f});
        add(p, (syl_t){.f0 = base * 1.4f, .f1 = base * 1.95f, .dur = 0.28f, .vib_rate = 7.0f, .vib_depth = 0.025f, .bright = 2.2f, .open = 2.0f});
        break;
    case VOICE_PEEKSHY: /* from behind paws: "mi?.. mm!.. mi-hi" */
        add(p, (syl_t){.f0 = base * 1.35f, .f1 = base * 1.65f, .dur = 0.1f, .gap = 0.26f, .bright = 1.6f, .open = 1.3f, .amp = 0.8f});
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 0.9f, .dur = 0.12f, .gap = 0.26f, .bright = 1.4f, .open = 1.1f, .amp = 0.8f});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 1.75f, .dur = 0.08f, .gap = 0.04f, .bright = 1.9f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.6f, .f1 = base * 1.85f, .dur = 0.08f, .bright = 1.9f, .open = 1.4f});
        break;
    case VOICE_BASHFUL: /* "u-u-uh" sinking into his collar */
        add(p, (syl_t){.f0 = base * 1.3f, .f1 = base * 1.45f, .dur = 0.1f, .gap = 0.03f, .vib_rate = 13.0f, .vib_depth = 0.03f, .bright = 1.8f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.35f, .f1 = base * 1.2f, .dur = 0.12f, .gap = 0.04f, .vib_rate = 13.0f, .vib_depth = 0.03f, .bright = 1.8f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.25f, .f1 = base * 0.8f, .dur = 0.4f, .vib_rate = 8.0f, .vib_depth = 0.04f, .bright = 1.6f, .open = 1.3f});
        break;
    case VOICE_HELLOO: /* "pri-vie-et!": up, up and wide open */
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 1.3f, .dur = 0.1f, .gap = 0.02f, .bright = 2.2f, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 1.3f, .f1 = base * 1.85f, .dur = 0.16f, .gap = 0.02f, .bright = 2.3f, .open = 2.0f});
        add(p, (syl_t){.f0 = base * 1.85f, .f1 = base * 1.5f, .dur = 0.3f, .vib_rate = 6.0f, .vib_depth = 0.025f, .bright = 2.2f, .open = 2.0f});
        break;
    case VOICE_YOOHOO: /* "yu-huu!" */
        add(p, (syl_t){.f0 = base * 1.2f, .f1 = base * 1.8f, .dur = 0.18f, .gap = 0.04f, .bright = 1.9f, .open = 1.5f});
        add(p, (syl_t){.f0 = base * 1.45f, .f1 = base * 1.4f, .dur = 0.36f, .vib_rate = 6.0f, .vib_depth = 0.025f, .bright = 1.8f, .open = 1.6f});
        break;
    case VOICE_ROYAL: /* trumpets: "pa-pa-ra-paa" */
        add(p, (syl_t){.f0 = 434.9f, .f1 = 440.0f, .dur = 0.13f, .gap = 0.03f, .bright = 2.5f, .open = 1.3f});
        add(p, (syl_t){.f0 = 434.9f, .f1 = 440.0f, .dur = 0.07f, .gap = 0.03f, .bright = 2.5f, .open = 1.3f});
        add(p, (syl_t){.f0 = 580.6f, .f1 = 587.3f, .dur = 0.07f, .gap = 0.03f, .bright = 2.5f, .open = 1.3f});
        add(p, (syl_t){.f0 = 731.5f, .f1 = 740.0f, .dur = 0.16f, .gap = 0.03f, .bright = 2.5f, .open = 1.3f});
        add(p, (syl_t){.f0 = 869.9f, .f1 = 880.0f, .dur = 0.5f, .vib_rate = 6.0f, .vib_depth = 0.014f, .bright = 2.5f, .open = 1.3f});
        break;
    case VOICE_HARRUMPH: /* a king clears his throat: "ahem... mmm" */
        add(p, (syl_t){.f0 = base * 0.7f, .f1 = base * 0.8f, .dur = 0.14f, .gap = 0.05f, .rough = 0.25f, .bright = 1.6f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 0.62f, .f1 = base * 0.56f, .dur = 0.18f, .gap = 0.14f, .rough = 0.3f, .bright = 1.6f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 0.75f, .f1 = base * 0.95f, .dur = 0.24f, .vib_rate = 5.0f, .vib_depth = 0.025f, .bright = 1.5f, .open = 1.3f});
        break;
    case VOICE_HALOSONG: /* a choir of one: "aa-aaa-aa", high and clear */
        add(p, (syl_t){.f0 = 757.3f, .f1 = 784.0f, .dur = 0.34f, .gap = 0.01f, .vib_rate = 6.0f, .vib_depth = 0.015f, .bright = 1.6f, .open = 1.4f, .amp = 0.85f});
        add(p, (syl_t){.f0 = 954.1f, .f1 = 987.8f, .dur = 0.34f, .gap = 0.01f, .vib_rate = 6.0f, .vib_depth = 0.015f, .bright = 1.6f, .open = 1.4f, .amp = 0.9f});
        add(p, (syl_t){.f0 = 1134.7f, .f1 = 1174.7f, .dur = 0.6f, .vib_rate = 6.0f, .vib_depth = 0.015f, .bright = 1.6f, .open = 1.4f, .amp = 0.95f});
        break;
    case VOICE_WHISTLING: /* an idle whistle: not me */
        add(p, (syl_t){.f0 = 921.8f, .f1 = 988.0f, .dur = 0.13f, .gap = 0.03f, .bright = 1.3f, .open = 1.1f});
        add(p, (syl_t){.f0 = 1034.7f, .f1 = 1109.0f, .dur = 0.1f, .gap = 0.03f, .bright = 1.3f, .open = 1.1f});
        add(p, (syl_t){.f0 = 1161.4f, .f1 = 1244.8f, .dur = 0.13f, .gap = 0.03f, .bright = 1.3f, .open = 1.1f});
        add(p, (syl_t){.f0 = 921.8f, .f1 = 988.0f, .dur = 0.1f, .gap = 0.03f, .bright = 1.3f, .open = 1.1f});
        add(p, (syl_t){.f0 = 1381.2f, .f1 = 1480.3f, .dur = 0.22f, .gap = 0.03f, .vib_rate = 6.0f, .vib_depth = 0.02f, .bright = 1.3f, .open = 1.1f});
        add(p, (syl_t){.f0 = 1161.4f, .f1 = 1244.8f, .dur = 0.3f, .vib_rate = 6.0f, .vib_depth = 0.02f, .bright = 1.3f, .open = 1.1f});
        break;
    case VOICE_MWAHAHA: /* a small villain: "mwa-ha-ha-ha", tiny and pleased */
        add(p, (syl_t){.f0 = base * 0.8f, .f1 = base * 1.2f, .dur = 0.18f, .gap = 0.05f, .bright = 1.8f, .open = 2.0f});
        add(p, (syl_t){.f0 = base * 1.15f, .f1 = base * 1.0f, .dur = 0.085f, .gap = 0.05f, .bright = 2.0f, .open = 1.8f, .amp = 1.0f});
        add(p, (syl_t){.f0 = base * 1.22f, .f1 = base * 1.07f, .dur = 0.085f, .gap = 0.05f, .bright = 2.0f, .open = 1.8f, .amp = 0.97f});
        add(p, (syl_t){.f0 = base * 1.29f, .f1 = base * 1.14f, .dur = 0.085f, .gap = 0.05f, .bright = 2.0f, .open = 1.8f, .amp = 0.94f});
        add(p, (syl_t){.f0 = base * 1.36f, .f1 = base * 1.21f, .dur = 0.085f, .gap = 0.05f, .bright = 2.0f, .open = 1.8f, .amp = 0.91f});
        add(p, (syl_t){.f0 = base * 1.45f, .f1 = base * 1.1f, .dur = 0.22f, .vib_rate = 8.0f, .vib_depth = 0.03f, .bright = 2.0f, .open = 1.7f});
        break;
    case VOICE_TEEHEE: /* a whispered plot: "hi... hi-hi" */
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 1.7f, .dur = 0.07f, .gap = 0.2f, .bright = 2.0f, .open = 1.4f, .amp = 0.8f});
        add(p, (syl_t){.f0 = base * 1.45f, .f1 = base * 1.65f, .dur = 0.07f, .gap = 0.05f, .bright = 2.0f, .open = 1.4f, .amp = 0.85f});
        add(p, (syl_t){.f0 = base * 1.6f, .f1 = base * 1.8f, .dur = 0.08f, .gap = 0.1f, .bright = 2.0f, .open = 1.4f, .amp = 0.9f});
        add(p, (syl_t){.f0 = base * 0.9f, .f1 = base * 0.8f, .dur = 0.26f, .vib_rate = 7.0f, .vib_depth = 0.03f, .bright = 1.4f, .open = 1.2f});
        break;
    case VOICE_HOPLA: /* three throws: "hop, hop, hop-la!" */
        add(p, (syl_t){.f0 = base * 1.0f, .f1 = base * 1.5f, .dur = 0.09f, .gap = 0.11f, .bright = 2.0f, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 1.15f, .f1 = base * 1.65f, .dur = 0.09f, .gap = 0.11f, .bright = 2.0f, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 1.3f, .f1 = base * 1.8f, .dur = 0.09f, .gap = 0.05f, .bright = 2.0f, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 1.9f, .f1 = base * 1.5f, .dur = 0.24f, .vib_rate = 7.0f, .vib_depth = 0.025f, .bright = 2.2f, .open = 1.9f});
        break;
    case VOICE_CIRCUS: /* the ring: a jolly run, oompah */
        add(p, (syl_t){.f0 = base * 1.464f, .f1 = base * 1.498f, .dur = 0.08f, .gap = 0.02f, .bright = 2.0f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.382f, .f1 = base * 1.414f, .dur = 0.08f, .gap = 0.02f, .bright = 2.0f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.464f, .f1 = base * 1.498f, .dur = 0.08f, .gap = 0.02f, .bright = 2.0f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.382f, .f1 = base * 1.414f, .dur = 0.08f, .gap = 0.02f, .bright = 2.0f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.464f, .f1 = base * 1.498f, .dur = 0.1f, .gap = 0.02f, .bright = 2.0f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.231f, .f1 = base * 1.26f, .dur = 0.1f, .gap = 0.02f, .bright = 2.0f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 0.977f, .f1 = base * 1.0f, .dur = 0.1f, .gap = 0.02f, .bright = 2.0f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.231f, .f1 = base * 1.26f, .dur = 0.1f, .gap = 0.02f, .bright = 2.0f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.464f, .f1 = base * 1.498f, .dur = 0.1f, .gap = 0.02f, .bright = 2.0f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.954f, .f1 = base * 2.0f, .dur = 0.28f, .vib_rate = 6.0f, .vib_depth = 0.02f, .bright = 2.0f, .open = 1.4f});
        break;
    case VOICE_LIFTOFF: /* floating up: a long glide, swaying */
        add(p, (syl_t){.f0 = base * 0.7f, .f1 = base * 1.1f, .dur = 0.4f, .vib_rate = 3.0f, .vib_depth = 0.03f, .bright = 1.7f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 1.7f, .dur = 0.5f, .vib_rate = 3.5f, .vib_depth = 0.035f, .bright = 1.8f, .open = 1.5f});
        add(p, (syl_t){.f0 = base * 1.7f, .f1 = base * 1.75f, .dur = 0.3f, .vib_rate = 5.0f, .vib_depth = 0.025f, .bright = 1.6f, .open = 1.3f, .amp = 0.8f});
        break;
    case VOICE_POPFALL: /* "pok!", a slide down, "bump", and a giggle */
        add(p, (syl_t){.f0 = base * 1.0f, .f1 = base * 2.3f, .dur = 0.04f, .gap = 0.08f, .bright = 2.0f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 2.0f, .f1 = base * 0.7f, .dur = 0.4f, .gap = 0.04f, .vib_rate = 8.0f, .vib_depth = 0.03f, .bright = 1.7f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 0.6f, .f1 = base * 0.45f, .dur = 0.1f, .gap = 0.12f, .bright = 1.7f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 1.7f, .dur = 0.06f, .gap = 0.04f, .bright = 2.2f, .open = 1.5f, .amp = 1.0f});
        add(p, (syl_t){.f0 = base * 1.56f, .f1 = base * 1.76f, .dur = 0.06f, .gap = 0.04f, .bright = 2.2f, .open = 1.5f, .amp = 0.97f});
        add(p, (syl_t){.f0 = base * 1.62f, .f1 = base * 1.82f, .dur = 0.06f, .gap = 0.04f, .bright = 2.2f, .open = 1.5f, .amp = 0.94f});
        break;
    case VOICE_LICK: /* "lyap... mm... lyap... mmm!" */
        add(p, (syl_t){.f0 = base * 0.9f, .f1 = base * 1.6f, .dur = 0.08f, .gap = 0.06f, .bright = 1.7f, .open = 1.7f});
        add(p, (syl_t){.f0 = base * 1.0f, .f1 = base * 0.9f, .dur = 0.16f, .gap = 0.16f, .bright = 1.4f, .open = 1.15f});
        add(p, (syl_t){.f0 = base * 0.95f, .f1 = base * 1.7f, .dur = 0.08f, .gap = 0.06f, .bright = 1.7f, .open = 1.7f});
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 1.3f, .dur = 0.14f, .bright = 1.5f, .open = 1.2f});
        add(p, (syl_t){.f0 = base * 1.3f, .f1 = base * 0.95f, .dur = 0.26f, .vib_rate = 6.0f, .vib_depth = 0.03f, .bright = 1.6f, .open = 1.4f});
        break;
    case VOICE_FREEZE: /* "a-y-y-y!" teeth together, shivering, then "fu-uh" */
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 1.9f, .dur = 0.16f, .gap = 0.03f, .vib_rate = 17.0f, .vib_depth = 0.06f, .bright = 2.0f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.9f, .f1 = base * 1.7f, .dur = 0.3f, .gap = 0.1f, .vib_rate = 17.0f, .vib_depth = 0.08f, .bright = 1.9f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 1.2f, .f1 = base * 0.7f, .dur = 0.4f, .bright = 1.8f, .open = 1.7f, .noise = 0.05f});
        break;
    case VOICE_PITTER: /* rain on the umbrella: soft drops of every size */
        add(p, (syl_t){.f0 = base * 1.3f, .f1 = base * 1.15f, .dur = 0.05f, .gap = 0.07f, .bright = 1.6f, .open = 1.25f, .amp = 0.75f});
        add(p, (syl_t){.f0 = base * 1.75f, .f1 = base * 1.6f, .dur = 0.05f, .gap = 0.04f, .bright = 1.6f, .open = 1.25f, .amp = 0.77f});
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 0.95f, .dur = 0.05f, .gap = 0.12f, .bright = 1.6f, .open = 1.25f, .amp = 0.79f});
        add(p, (syl_t){.f0 = base * 1.55f, .f1 = base * 1.4f, .dur = 0.05f, .gap = 0.05f, .bright = 1.6f, .open = 1.25f, .amp = 0.81f});
        add(p, (syl_t){.f0 = base * 1.9f, .f1 = base * 1.75f, .dur = 0.05f, .gap = 0.09f, .bright = 1.6f, .open = 1.25f, .amp = 0.83f});
        add(p, (syl_t){.f0 = base * 1.2f, .f1 = base * 1.05f, .dur = 0.05f, .gap = 0.04f, .bright = 1.6f, .open = 1.25f, .amp = 0.85f});
        add(p, (syl_t){.f0 = base * 1.65f, .f1 = base * 1.5f, .dur = 0.05f, .gap = 0.14f, .bright = 1.6f, .open = 1.25f, .amp = 0.87f});
        add(p, (syl_t){.f0 = base * 1.0f, .f1 = base * 0.85f, .dur = 0.05f, .gap = 0.06f, .bright = 1.6f, .open = 1.25f, .amp = 0.89f});
        add(p, (syl_t){.f0 = base * 1.8f, .f1 = base * 1.65f, .dur = 0.05f, .gap = 0.05f, .bright = 1.6f, .open = 1.25f, .amp = 0.91f});
        add(p, (syl_t){.f0 = base * 1.35f, .f1 = base * 1.2f, .dur = 0.05f, .gap = 0.1f, .bright = 1.6f, .open = 1.25f, .amp = 0.9299999999999999f});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 1.35f, .dur = 0.05f, .bright = 1.6f, .open = 1.25f, .amp = 0.95f});
        break;
    case VOICE_DRYHUM: /* dry and pleased: a humming stroll */
        add(p, (syl_t){.f0 = base * 0.831f, .f1 = base * 0.85f, .dur = 0.2f, .gap = 0.03f, .vib_rate = 6.0f, .vib_depth = 0.02f, .bright = 1.4f, .open = 1.2f});
        add(p, (syl_t){.f0 = base * 0.932f, .f1 = base * 0.954f, .dur = 0.14f, .gap = 0.03f, .bright = 1.4f, .open = 1.2f});
        add(p, (syl_t){.f0 = base * 1.046f, .f1 = base * 1.071f, .dur = 0.2f, .gap = 0.03f, .vib_rate = 6.0f, .vib_depth = 0.02f, .bright = 1.4f, .open = 1.2f});
        add(p, (syl_t){.f0 = base * 1.244f, .f1 = base * 1.274f, .dur = 0.28f, .gap = 0.03f, .vib_rate = 6.0f, .vib_depth = 0.02f, .bright = 1.4f, .open = 1.2f});
        add(p, (syl_t){.f0 = base * 1.046f, .f1 = base * 1.071f, .dur = 0.14f, .gap = 0.03f, .bright = 1.4f, .open = 1.2f});
        add(p, (syl_t){.f0 = base * 0.932f, .f1 = base * 0.954f, .dur = 0.36f, .vib_rate = 6.0f, .vib_depth = 0.02f, .bright = 1.4f, .open = 1.2f});
        break;
    case VOICE_HEAVE: /* "nnn... nnn... HUP!" */
        add(p, (syl_t){.f0 = base * 0.6f, .f1 = base * 0.72f, .dur = 0.34f, .gap = 0.08f, .vib_rate = 15.0f, .vib_depth = 0.04f, .bright = 1.5f, .open = 1.2f});
        add(p, (syl_t){.f0 = base * 0.66f, .f1 = base * 0.85f, .dur = 0.36f, .gap = 0.05f, .vib_rate = 16.0f, .vib_depth = 0.05f, .bright = 1.5f, .open = 1.25f});
        add(p, (syl_t){.f0 = base * 0.9f, .f1 = base * 1.8f, .dur = 0.16f, .bright = 2.2f, .open = 1.9f});
        add(p, (syl_t){.f0 = base * 1.8f, .f1 = base * 1.6f, .dur = 0.16f, .vib_rate = 9.0f, .vib_depth = 0.03f, .bright = 2.2f, .open = 1.7f});
        break;
    case VOICE_STRONG: /* puffed up: "u-ff... ta-daa" */
        add(p, (syl_t){.f0 = base * 0.9f, .f1 = base * 0.65f, .dur = 0.24f, .gap = 0.12f, .bright = 1.8f, .open = 1.6f, .noise = 0.06f});
        add(p, (syl_t){.f0 = base * 1.0f, .f1 = base * 1.15f, .dur = 0.1f, .gap = 0.04f, .bright = 2.2f, .open = 1.7f});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 1.45f, .dur = 0.4f, .vib_rate = 6.0f, .vib_depth = 0.025f, .bright = 2.2f, .open = 2.0f});
        break;
    case VOICE_ABRACADABRA: /* a spell mumbled, winding up - and "puff" */
        add(p, (syl_t){.f0 = base * 0.85f, .f1 = base * 0.95f, .dur = 0.075f, .gap = 0.025f, .bright = 1.9f, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 1.04f, .f1 = base * 1.02f, .dur = 0.075f, .gap = 0.025f, .bright = 1.9f, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 0.99f, .f1 = base * 1.09f, .dur = 0.075f, .gap = 0.025f, .bright = 1.9f, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 1.18f, .f1 = base * 1.16f, .dur = 0.075f, .gap = 0.025f, .bright = 1.9f, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 1.13f, .f1 = base * 1.23f, .dur = 0.075f, .gap = 0.025f, .bright = 1.9f, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 1.32f, .f1 = base * 1.3f, .dur = 0.075f, .gap = 0.025f, .bright = 1.9f, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 1.27f, .f1 = base * 1.37f, .dur = 0.075f, .gap = 0.025f, .bright = 1.9f, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 1.0f, .f1 = base * 2.0f, .dur = 0.2f, .gap = 0.05f, .bright = 1.8f, .open = 1.5f});
        add(p, (syl_t){.f0 = base * 0.75f, .f1 = base * 0.5f, .dur = 0.16f, .bright = 1.6f, .open = 1.5f, .noise = 0.08f});
        break;
    case VOICE_RABBIT: /* sparkles up and a little "a-voila!" */
        add(p, (syl_t){.f0 = 859.9f, .f1 = 880.0f, .dur = 0.06f, .gap = 0.02f, .bright = 1.6f, .open = 1.3f, .amp = 0.75f});
        add(p, (syl_t){.f0 = 1083.4f, .f1 = 1108.7f, .dur = 0.06f, .gap = 0.02f, .bright = 1.6f, .open = 1.3f, .amp = 0.8f});
        add(p, (syl_t){.f0 = 1288.4f, .f1 = 1318.5f, .dur = 0.06f, .gap = 0.02f, .bright = 1.6f, .open = 1.3f, .amp = 0.85f});
        add(p, (syl_t){.f0 = 1719.8f, .f1 = 1760.0f, .dur = 0.06f, .gap = 0.02f, .bright = 1.6f, .open = 1.3f, .amp = 0.9f});
        add(p, (syl_t){.f0 = 2166.8f, .f1 = 2217.5f, .dur = 0.06f, .bright = 1.6f, .open = 1.3f, .amp = 0.9f});
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 1.2f, .dur = 0.08f, .gap = 0.03f, .bright = 2.1f, .open = 1.7f});
        add(p, (syl_t){.f0 = base * 1.4f, .f1 = base * 1.9f, .dur = 0.28f, .vib_rate = 7.0f, .vib_depth = 0.025f, .bright = 2.2f, .open = 1.9f});
        break;
    case VOICE_DAWN: /* light rising: four soft notes stacking up */
        add(p, (syl_t){.f0 = 374.3f, .f1 = 392.0f, .dur = 0.3f, .vib_rate = 6.0f, .vib_depth = 0.012f, .bright = 1.7f, .open = 1.35f, .amp = 0.75f});
        add(p, (syl_t){.f0 = 560.8f, .f1 = 587.3f, .dur = 0.3f, .vib_rate = 6.0f, .vib_depth = 0.012f, .bright = 1.7f, .open = 1.35f, .amp = 0.85f});
        add(p, (syl_t){.f0 = 748.6f, .f1 = 784.0f, .dur = 0.3f, .vib_rate = 6.0f, .vib_depth = 0.012f, .bright = 1.7f, .open = 1.35f, .amp = 0.92f});
        add(p, (syl_t){.f0 = 943.2f, .f1 = 987.8f, .dur = 0.7f, .vib_rate = 6.0f, .vib_depth = 0.012f, .bright = 1.7f, .open = 1.35f, .amp = 1.0f});
        break;
    case VOICE_STRETCH: /* a good stretch: "mmm-aaa-ahh" and a smack */
        add(p, (syl_t){.f0 = base * 0.7f, .f1 = base * 0.8f, .dur = 0.3f, .bright = 1.4f, .open = 1.2f});
        add(p, (syl_t){.f0 = base * 0.8f, .f1 = base * 1.7f, .dur = 0.5f, .vib_rate = 5.0f, .vib_depth = 0.025f, .bright = 1.9f, .open = 2.2f});
        add(p, (syl_t){.f0 = base * 1.6f, .f1 = base * 0.85f, .dur = 0.3f, .gap = 0.06f, .bright = 2.0f, .open = 1.7f});
        add(p, (syl_t){.f0 = base * 0.9f, .f1 = base * 1.2f, .dur = 0.07f, .bright = 1.7f, .open = 1.4f});
        break;
    case VOICE_BAA: /* a small soft sheep: "be-e-e" */
        add(p, (syl_t){.f0 = base * 0.85f, .f1 = base * 0.95f, .dur = 0.1f, .vib_rate = 11.0f, .vib_depth = 0.05f, .bright = 1.8f, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 0.95f, .f1 = base * 0.88f, .dur = 0.5f, .vib_rate = 11.0f, .vib_depth = 0.06f, .bright = 1.9f, .open = 1.7f});
        break;
    case VOICE_COUNTING: /* one... two... thr...: each lower and slower, a yawn */
        add(p, (syl_t){.f0 = base * 1.15f, .f1 = base * 1.2f, .dur = 0.12f, .gap = 0.2f, .bright = 1.8f, .open = 1.5f});
        add(p, (syl_t){.f0 = base * 1.05f, .f1 = base * 1.1f, .dur = 0.15f, .gap = 0.22f, .bright = 1.7f, .open = 1.5f});
        add(p, (syl_t){.f0 = base * 0.95f, .f1 = base * 0.9f, .dur = 0.2f, .gap = 0.16f, .bright = 1.6f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 0.8f, .f1 = base * 1.1f, .dur = 0.3f, .bright = 1.7f, .open = 2.0f, .noise = 0.04f});
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 0.55f, .dur = 0.55f, .bright = 1.6f, .open = 1.5f, .noise = 0.04f});
        break;
    case VOICE_HIYA: /* "ki-yaa!" short and neat */
        add(p, (syl_t){.f0 = base * 1.6f, .f1 = base * 1.9f, .dur = 0.07f, .gap = 0.02f, .bright = 2.1f, .open = 1.5f});
        add(p, (syl_t){.f0 = base * 1.3f, .f1 = base * 1.7f, .dur = 0.1f, .bright = 2.2f, .open = 2.0f});
        add(p, (syl_t){.f0 = base * 1.7f, .f1 = base * 1.2f, .dur = 0.2f, .bright = 2.2f, .open = 1.7f});
        break;
    case VOICE_SHADOW: /* sneaking: five toes, and a soft gong */
        add(p, (syl_t){.f0 = base * 0.782f, .f1 = base * 0.8f, .dur = 0.06f, .gap = 0.1f, .bright = 1.6f, .open = 1.25f, .amp = 0.75f});
        add(p, (syl_t){.f0 = base * 0.93f, .f1 = base * 0.951f, .dur = 0.06f, .gap = 0.1f, .bright = 1.6f, .open = 1.25f, .amp = 0.8f});
        add(p, (syl_t){.f0 = base * 0.782f, .f1 = base * 0.8f, .dur = 0.06f, .gap = 0.1f, .bright = 1.6f, .open = 1.25f, .amp = 0.75f});
        add(p, (syl_t){.f0 = base * 1.043f, .f1 = base * 1.068f, .dur = 0.06f, .gap = 0.1f, .bright = 1.6f, .open = 1.25f, .amp = 0.85f});
        add(p, (syl_t){.f0 = base * 0.93f, .f1 = base * 0.951f, .dur = 0.06f, .bright = 1.6f, .open = 1.25f, .amp = 0.8f});
        add(p, (syl_t){.f0 = 330, .f1 = 328, .dur = 0.6f, .vib_rate = 5.0f, .vib_depth = 0.008f, .bright = 1.6f, .open = 1.25f, .amp = 0.9f});
        break;
    case VOICE_GROWING: /* growing: a creeping glide with three little leaves */
        add(p, (syl_t){.f0 = base * 0.6f, .f1 = base * 0.9f, .dur = 0.34f, .gap = 0.02f, .vib_rate = 9.0f, .vib_depth = 0.02f, .bright = 1.5f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 1.2f, .f1 = base * 1.4f, .dur = 0.06f, .gap = 0.04f, .bright = 1.8f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 0.9f, .f1 = base * 1.2f, .dur = 0.24f, .gap = 0.02f, .vib_rate = 9.0f, .vib_depth = 0.02f, .bright = 1.6f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 1.75f, .dur = 0.06f, .gap = 0.04f, .bright = 1.8f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.2f, .f1 = base * 1.6f, .dur = 0.2f, .gap = 0.02f, .bright = 1.7f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.9f, .f1 = base * 2.2f, .dur = 0.07f, .bright = 1.8f, .open = 1.4f});
        break;
    case VOICE_BLOSSOM: /* "pu-uh!" it opens, and bells */
        add(p, (syl_t){.f0 = base * 0.8f, .f1 = base * 1.5f, .dur = 0.18f, .gap = 0.03f, .bright = 1.8f, .open = 1.9f});
        add(p, (syl_t){.f0 = 766.1f, .f1 = 784.0f, .dur = 0.09f, .gap = 0.02f, .bright = 1.6f, .open = 1.3f, .amp = 0.8f});
        add(p, (syl_t){.f0 = 1147.8f, .f1 = 1174.7f, .dur = 0.09f, .gap = 0.02f, .bright = 1.6f, .open = 1.3f, .amp = 0.85f});
        add(p, (syl_t){.f0 = 965.2f, .f1 = 987.8f, .dur = 0.09f, .gap = 0.02f, .bright = 1.6f, .open = 1.3f, .amp = 0.85f});
        add(p, (syl_t){.f0 = 1532.2f, .f1 = 1568.0f, .dur = 0.44f, .vib_rate = 6.0f, .vib_depth = 0.01f, .bright = 1.6f, .open = 1.3f, .amp = 0.9f});
        break;
    case VOICE_GREYSIGH: /* a long grey sigh: "haah... mm" */
        add(p, (syl_t){.f0 = base * 1.0f, .f1 = base * 0.62f, .dur = 0.62f, .gap = 0.12f, .vib_rate = 4.0f, .vib_depth = 0.02f, .bright = 1.8f, .open = 1.6f, .noise = 0.05f});
        add(p, (syl_t){.f0 = base * 0.6f, .f1 = base * 0.48f, .dur = 0.34f, .vib_rate = 4.0f, .vib_depth = 0.025f, .bright = 1.4f, .open = 1.15f});
        break;
    case VOICE_DRIZZLE: /* drops falling in minor: one, two... three */
        add(p, (syl_t){.f0 = 643.9f, .f1 = 659.0f, .dur = 0.16f, .gap = 0.12f, .bright = 1.6f, .open = 1.25f, .amp = 0.85f});
        add(p, (syl_t){.f0 = 573.7f, .f1 = 587.1f, .dur = 0.16f, .gap = 0.12f, .bright = 1.6f, .open = 1.25f, .amp = 0.85f});
        add(p, (syl_t){.f0 = 482.4f, .f1 = 493.7f, .dur = 0.16f, .gap = 0.12f, .bright = 1.6f, .open = 1.25f, .amp = 0.9f});
        add(p, (syl_t){.f0 = 382.9f, .f1 = 391.8f, .dur = 0.56f, .vib_rate = 6.0f, .vib_depth = 0.012f, .bright = 1.6f, .open = 1.25f, .amp = 0.85f});
        break;
    case VOICE_BAWL: /* "u-aaa... u-aaa-aa": crying out loud */
        add(p, (syl_t){.f0 = base * 0.9f, .f1 = base * 1.4f, .dur = 0.16f, .vib_rate = 7.0f, .vib_depth = 0.05f, .bright = 1.9f, .open = 1.9f});
        add(p, (syl_t){.f0 = base * 1.4f, .f1 = base * 1.0f, .dur = 0.46f, .gap = 0.1f, .vib_rate = 7.0f, .vib_depth = 0.06f, .bright = 2.0f, .open = 1.8f});
        add(p, (syl_t){.f0 = base * 0.95f, .f1 = base * 1.5f, .dur = 0.16f, .vib_rate = 7.0f, .vib_depth = 0.05f, .bright = 1.9f, .open = 1.9f});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 0.85f, .dur = 0.6f, .vib_rate = 7.0f, .vib_depth = 0.07f, .bright = 2.0f, .open = 1.7f});
        break;
    case VOICE_SOBS: /* catching his breath: "hu-hu-huu... hik" */
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 0.95f, .dur = 0.12f, .gap = 0.06f, .vib_rate = 9.0f, .vib_depth = 0.05f, .bright = 1.8f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 0.92f, .dur = 0.12f, .gap = 0.06f, .vib_rate = 9.0f, .vib_depth = 0.05f, .bright = 1.8f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.15f, .f1 = base * 0.75f, .dur = 0.34f, .gap = 0.12f, .vib_rate = 8.0f, .vib_depth = 0.06f, .bright = 1.8f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.0f, .f1 = base * 1.5f, .dur = 0.05f, .gap = 0.1f, .bright = 1.7f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 0.85f, .f1 = base * 0.6f, .dur = 0.3f, .vib_rate = 7.0f, .vib_depth = 0.05f, .bright = 1.6f, .open = 1.2f});
        break;
    case VOICE_CRACKED: /* "mm?.. krr-ak" and a voice going down the stairs */
        add(p, (syl_t){.f0 = base * 1.2f, .f1 = base * 1.35f, .dur = 0.2f, .gap = 0.1f, .bright = 1.8f, .open = 1.5f});
        add(p, (syl_t){.f0 = base * 0.9f, .f1 = base * 0.6f, .dur = 0.08f, .gap = 0.05f, .rough = 0.4f, .bright = 1.8f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.029f, .f1 = base * 1.0f, .dur = 0.16f, .gap = 0.03f, .bright = 1.7f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 0.917f, .f1 = base * 0.891f, .dur = 0.16f, .gap = 0.03f, .bright = 1.7f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 0.817f, .f1 = base * 0.794f, .dur = 0.16f, .gap = 0.03f, .bright = 1.7f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 0.687f, .f1 = base * 0.667f, .dur = 0.46f, .vib_rate = 6.0f, .vib_depth = 0.04f, .bright = 1.7f, .open = 1.3f});
        break;
    case VOICE_LETGO: /* an unsteady "a-ah..." trailing off */
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 1.25f, .dur = 0.22f, .gap = 0.04f, .vib_rate = 6.0f, .vib_depth = 0.04f, .bright = 1.8f, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 1.2f, .f1 = base * 0.9f, .dur = 0.3f, .gap = 0.06f, .vib_rate = 6.0f, .vib_depth = 0.05f, .bright = 1.7f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 0.85f, .f1 = base * 0.55f, .dur = 0.5f, .vib_rate = 5.0f, .vib_depth = 0.05f, .bright = 1.5f, .open = 1.2f, .amp = 0.85f});
        break;
    case VOICE_KETTLE: /* a kettle coming to the boil: a murmur, then a round whistle up */
        add(p, (syl_t){.f0 = base * 0.5f, .f1 = base * 0.56f, .dur = 0.34f, .gap = 0.02f, .rough = 0.5f, .bright = 1.6f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 0.56f, .f1 = base * 0.7f, .dur = 0.3f, .gap = 0.02f, .rough = 0.5f, .bright = 1.6f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 1.0f, .f1 = base * 1.9f, .dur = 0.34f, .vib_rate = 13.0f, .vib_depth = 0.02f, .bright = 1.6f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 1.9f, .f1 = base * 2.0f, .dur = 0.4f, .vib_rate = 13.0f, .vib_depth = 0.025f, .bright = 1.5f, .open = 1.2f, .amp = 0.85f});
        break;
    case VOICE_GRRMPH: /* "grr-rr... mpf!" */
        add(p, (syl_t){.f0 = base * 0.48f, .f1 = base * 0.42f, .dur = 0.34f, .gap = 0.04f, .rough = 0.6f, .bright = 2.0f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 0.52f, .f1 = base * 0.46f, .dur = 0.26f, .gap = 0.08f, .rough = 0.6f, .bright = 2.0f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 0.8f, .f1 = base * 0.55f, .dur = 0.12f, .bright = 1.6f, .open = 1.3f});
        break;
    case VOICE_SMOULDER: /* low coals: a slow growl that flares */
        add(p, (syl_t){.f0 = base * 0.42f, .f1 = base * 0.5f, .dur = 0.44f, .rough = 0.5f, .bright = 1.9f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 0.5f, .f1 = base * 0.9f, .dur = 0.3f, .rough = 0.5f, .bright = 2.0f, .open = 1.7f});
        add(p, (syl_t){.f0 = base * 0.9f, .f1 = base * 0.5f, .dur = 0.36f, .rough = 0.5f, .bright = 1.9f, .open = 1.4f});
        break;
    case VOICE_ROARLET: /* a small fierce "rrraa!" */
        add(p, (syl_t){.f0 = base * 0.55f, .f1 = base * 0.7f, .dur = 0.16f, .rough = 0.7f, .bright = 2.0f, .open = 1.5f});
        add(p, (syl_t){.f0 = base * 0.8f, .f1 = base * 1.15f, .dur = 0.22f, .rough = 0.5f, .bright = 2.2f, .open = 2.1f});
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 0.6f, .dur = 0.26f, .rough = 0.5f, .bright = 2.0f, .open = 1.6f});
        break;
    case VOICE_HMFF: /* "hmf!" and his nose in the air */
        add(p, (syl_t){.f0 = base * 0.95f, .f1 = base * 0.7f, .dur = 0.13f, .gap = 0.12f, .bright = 1.5f, .open = 1.2f});
        add(p, (syl_t){.f0 = base * 0.9f, .f1 = base * 1.25f, .dur = 0.13f, .bright = 1.6f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 1.25f, .f1 = base * 1.0f, .dur = 0.14f, .bright = 1.6f, .open = 1.3f});
        break;
    case VOICE_NYEH: /* "nye-e-e": not looking at you */
        add(p, (syl_t){.f0 = base * 0.85f, .f1 = base * 1.0f, .dur = 0.12f, .bright = 1.6f, .open = 1.7f});
        add(p, (syl_t){.f0 = base * 1.0f, .f1 = base * 0.7f, .dur = 0.36f, .gap = 0.08f, .vib_rate = 6.0f, .vib_depth = 0.04f, .bright = 1.7f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 0.62f, .f1 = base * 0.5f, .dur = 0.2f, .bright = 1.4f, .open = 1.15f});
        break;
    case VOICE_TEETH: /* teeth chattering softly, and a wavering "u-u" */
        add(p, (syl_t){.f0 = base * 1.3f, .f1 = base * 1.2f, .dur = 0.035f, .gap = 0.04f, .bright = 1.7f, .open = 1.2f, .amp = 0.75f});
        add(p, (syl_t){.f0 = base * 1.3800000000000001f, .f1 = base * 1.28f, .dur = 0.035f, .gap = 0.04f, .bright = 1.7f, .open = 1.2f, .amp = 0.75f});
        add(p, (syl_t){.f0 = base * 1.3f, .f1 = base * 1.2f, .dur = 0.035f, .gap = 0.04f, .bright = 1.7f, .open = 1.2f, .amp = 0.75f});
        add(p, (syl_t){.f0 = base * 1.3800000000000001f, .f1 = base * 1.28f, .dur = 0.035f, .gap = 0.04f, .bright = 1.7f, .open = 1.2f, .amp = 0.75f});
        add(p, (syl_t){.f0 = base * 1.3f, .f1 = base * 1.2f, .dur = 0.035f, .gap = 0.04f, .bright = 1.7f, .open = 1.2f, .amp = 0.75f});
        add(p, (syl_t){.f0 = base * 1.3800000000000001f, .f1 = base * 1.28f, .dur = 0.035f, .gap = 0.04f, .bright = 1.7f, .open = 1.2f, .amp = 0.75f});
        add(p, (syl_t){.f0 = base * 1.3f, .f1 = base * 1.2f, .dur = 0.035f, .gap = 0.04f, .bright = 1.7f, .open = 1.2f, .amp = 0.75f});
        add(p, (syl_t){.f0 = base * 1.3800000000000001f, .f1 = base * 1.28f, .dur = 0.035f, .gap = 0.04f, .bright = 1.7f, .open = 1.2f, .amp = 0.75f});
        add(p, (syl_t){.f0 = base * 1.2f, .f1 = base * 1.4f, .dur = 0.22f, .vib_rate = 10.0f, .vib_depth = 0.06f, .bright = 1.7f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.4f, .f1 = base * 1.1f, .dur = 0.3f, .vib_rate = 10.0f, .vib_depth = 0.07f, .bright = 1.7f, .open = 1.3f});
        break;
    case VOICE_BOOO: /* the ghost: a hollow "u-uuu-uu" */
        add(p, (syl_t){.f0 = base * 0.7f, .f1 = base * 1.05f, .dur = 0.4f, .vib_rate = 5.0f, .vib_depth = 0.05f, .bright = 1.4f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 1.05f, .f1 = base * 1.1f, .dur = 0.34f, .vib_rate = 5.0f, .vib_depth = 0.06f, .bright = 1.4f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 0.6f, .dur = 0.6f, .vib_rate = 4.5f, .vib_depth = 0.06f, .bright = 1.4f, .open = 1.2f, .amp = 0.85f});
        break;
    case VOICE_EMPTYWIND: /* wind over nothing: a slow rise and fall, and a far whistle */
        add(p, (syl_t){.f0 = base * 0.9f, .f1 = base * 1.3f, .dur = 0.6f, .vib_rate = 3.0f, .vib_depth = 0.02f, .bright = 1.3f, .open = 1.15f, .amp = 0.8f, .noise = 0.08f});
        add(p, (syl_t){.f0 = base * 1.3f, .f1 = base * 0.8f, .dur = 0.6f, .gap = 0.08f, .vib_rate = 3.0f, .vib_depth = 0.02f, .bright = 1.3f, .open = 1.15f, .amp = 0.8f, .noise = 0.08f});
        add(p, (syl_t){.f0 = base * 1.7f, .f1 = base * 1.5f, .dur = 0.4f, .vib_rate = 5.0f, .vib_depth = 0.02f, .bright = 1.3f, .open = 1.1f, .amp = 0.7f});
        break;
    case VOICE_MEH: /* "me-e-h" */
        add(p, (syl_t){.f0 = base * 0.85f, .f1 = base * 0.9f, .dur = 0.08f, .bright = 1.5f, .open = 1.2f});
        add(p, (syl_t){.f0 = base * 0.9f, .f1 = base * 0.6f, .dur = 0.4f, .vib_rate = 4.0f, .vib_depth = 0.02f, .bright = 1.8f, .open = 1.4f});
        break;
    case VOICE_POWERDOWN: /* winding down: each note slower and lower */
        add(p, (syl_t){.f0 = base * 0.995f, .f1 = base * 0.95f, .dur = 0.09f, .gap = 0.04f, .bright = 1.8f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 0.837f, .f1 = base * 0.799f, .dur = 0.12f, .gap = 0.04f, .bright = 1.8f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 0.704f, .f1 = base * 0.672f, .dur = 0.16f, .gap = 0.04f, .bright = 1.8f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 0.558f, .f1 = base * 0.533f, .dur = 0.22f, .gap = 0.04f, .vib_rate = 6.0f, .vib_depth = 0.03f, .bright = 1.8f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 0.47f, .f1 = base * 0.448f, .dur = 0.44f, .vib_rate = 6.0f, .vib_depth = 0.03f, .bright = 1.8f, .open = 1.3f});
        break;
    case VOICE_LOWBEEP: /* "bip... bip... bu-up": an empty battery */
        add(p, (syl_t){.f0 = base * 1.3f, .f1 = base * 1.3f, .dur = 0.07f, .gap = 0.24f, .bright = 1.8f, .open = 1.05f});
        add(p, (syl_t){.f0 = base * 1.3f, .f1 = base * 1.3f, .dur = 0.07f, .gap = 0.24f, .bright = 1.8f, .open = 1.05f});
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 0.6f, .dur = 0.4f, .vib_rate = 5.0f, .vib_depth = 0.03f, .bright = 1.7f, .open = 1.2f});
        break;
    case VOICE_RUMBLE: /* a belly: "gru-u-ull", and an embarrassed "u?" */
        add(p, (syl_t){.f0 = base * 0.42f, .f1 = base * 0.6f, .dur = 0.26f, .vib_rate = 7.0f, .vib_depth = 0.12f, .bright = 1.8f, .open = 1.5f});
        add(p, (syl_t){.f0 = base * 0.6f, .f1 = base * 0.38f, .dur = 0.34f, .vib_rate = 6.0f, .vib_depth = 0.14f, .bright = 1.8f, .open = 1.5f});
        add(p, (syl_t){.f0 = base * 0.45f, .f1 = base * 0.55f, .dur = 0.18f, .gap = 0.14f, .vib_rate = 8.0f, .vib_depth = 0.12f, .bright = 1.8f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 0.95f, .f1 = base * 1.2f, .dur = 0.14f, .bright = 1.7f, .open = 1.4f});
        break;
    case VOICE_NOCOOKIE: /* "o-oo!.. oh...": there it is - and it is gone */
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 1.6f, .dur = 0.24f, .gap = 0.08f, .vib_rate = 7.0f, .vib_depth = 0.03f, .bright = 1.9f, .open = 1.7f});
        add(p, (syl_t){.f0 = base * 1.6f, .f1 = base * 1.75f, .dur = 0.14f, .gap = 0.1f, .bright = 1.9f, .open = 1.5f});
        add(p, (syl_t){.f0 = base * 1.2f, .f1 = base * 0.65f, .dur = 0.46f, .vib_rate = 5.0f, .vib_depth = 0.03f, .bright = 1.8f, .open = 1.4f});
        break;
    case VOICE_BIRDIES: /* little birds going round: "tiu-tiu, tiu-tiu" */
        add(p, (syl_t){.f0 = base * 1.9f, .f1 = base * 2.4f, .dur = 0.05f, .bright = 1.6f, .open = 1.25f, .amp = 0.8f});
        add(p, (syl_t){.f0 = base * 2.4f, .f1 = base * 1.7f, .dur = 0.08f, .gap = 0.06f, .bright = 1.6f, .open = 1.25f, .amp = 0.8f});
        add(p, (syl_t){.f0 = base * 2.05f, .f1 = base * 2.55f, .dur = 0.05f, .bright = 1.6f, .open = 1.25f, .amp = 0.8f});
        add(p, (syl_t){.f0 = base * 2.55f, .f1 = base * 1.7f, .dur = 0.08f, .gap = 0.14f, .bright = 1.6f, .open = 1.25f, .amp = 0.8f});
        add(p, (syl_t){.f0 = base * 1.9f, .f1 = base * 2.4f, .dur = 0.05f, .bright = 1.6f, .open = 1.25f, .amp = 0.8f});
        add(p, (syl_t){.f0 = base * 2.4f, .f1 = base * 1.7f, .dur = 0.08f, .gap = 0.06f, .bright = 1.6f, .open = 1.25f, .amp = 0.8f});
        add(p, (syl_t){.f0 = base * 2.05f, .f1 = base * 2.55f, .dur = 0.05f, .bright = 1.6f, .open = 1.25f, .amp = 0.8f});
        add(p, (syl_t){.f0 = base * 2.55f, .f1 = base * 1.7f, .dur = 0.08f, .gap = 0.14f, .bright = 1.6f, .open = 1.25f, .amp = 0.8f});
        add(p, (syl_t){.f0 = base * 1.9f, .f1 = base * 2.4f, .dur = 0.05f, .bright = 1.6f, .open = 1.25f, .amp = 0.8f});
        add(p, (syl_t){.f0 = base * 2.4f, .f1 = base * 1.7f, .dur = 0.08f, .gap = 0.06f, .bright = 1.6f, .open = 1.25f, .amp = 0.8f});
        add(p, (syl_t){.f0 = base * 2.05f, .f1 = base * 2.55f, .dur = 0.05f, .bright = 1.6f, .open = 1.25f, .amp = 0.8f});
        add(p, (syl_t){.f0 = base * 2.55f, .f1 = base * 1.7f, .dur = 0.08f, .gap = 0.14f, .bright = 1.6f, .open = 1.25f, .amp = 0.8f});
        break;
    case VOICE_KAPUT: /* "bo-nk... uuu-ooo": and over he goes */
        add(p, (syl_t){.f0 = base * 0.8f, .f1 = base * 0.55f, .dur = 0.12f, .gap = 0.1f, .bright = 1.9f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.4f, .f1 = base * 0.9f, .dur = 0.34f, .vib_rate = 6.0f, .vib_depth = 0.1f, .bright = 1.8f, .open = 1.5f});
        add(p, (syl_t){.f0 = base * 0.9f, .f1 = base * 0.5f, .dur = 0.44f, .vib_rate = 5.0f, .vib_depth = 0.12f, .bright = 1.7f, .open = 1.4f});
        break;
    case VOICE_LOVESMENOT: /* loves... loves not... loves... not */
        add(p, (syl_t){.f0 = base * 1.2f, .f1 = base * 1.35f, .dur = 0.14f, .gap = 0.14f, .bright = 1.8f, .open = 1.5f});
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 0.95f, .dur = 0.16f, .gap = 0.16f, .bright = 1.7f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.15f, .f1 = base * 1.3f, .dur = 0.14f, .gap = 0.16f, .bright = 1.8f, .open = 1.5f});
        add(p, (syl_t){.f0 = base * 0.95f, .f1 = base * 0.58f, .dur = 0.5f, .vib_rate = 5.0f, .vib_depth = 0.035f, .bright = 1.6f, .open = 1.3f});
        break;
    case VOICE_FADING: /* a music box running down */
        add(p, (syl_t){.f0 = 1147.8f, .f1 = 1174.7f, .dur = 0.14f, .gap = 0.04f, .bright = 1.6f, .open = 1.25f, .amp = 0.95f});
        add(p, (syl_t){.f0 = 965.2f, .f1 = 987.8f, .dur = 0.16f, .gap = 0.04f, .bright = 1.6f, .open = 1.25f, .amp = 0.92f});
        add(p, (syl_t){.f0 = 766.1f, .f1 = 784.0f, .dur = 0.19f, .gap = 0.04f, .vib_rate = 6.0f, .vib_depth = 0.012f, .bright = 1.6f, .open = 1.25f, .amp = 0.88f});
        add(p, (syl_t){.f0 = 965.2f, .f1 = 987.8f, .dur = 0.23f, .gap = 0.04f, .vib_rate = 6.0f, .vib_depth = 0.012f, .bright = 1.6f, .open = 1.25f, .amp = 0.84f});
        add(p, (syl_t){.f0 = 723.1f, .f1 = 740.0f, .dur = 0.28f, .gap = 0.04f, .vib_rate = 6.0f, .vib_depth = 0.012f, .bright = 1.6f, .open = 1.25f, .amp = 0.8f});
        add(p, (syl_t){.f0 = 573.9f, .f1 = 587.3f, .dur = 0.5f, .vib_rate = 6.0f, .vib_depth = 0.012f, .bright = 1.6f, .open = 1.25f, .amp = 0.75f});
        break;
    case VOICE_SNIFFLE: /* "shmyg... shmyg": a stuffy little sniff up, twice, and "mm" */
        add(p, (syl_t){.f0 = base * 0.75f, .f1 = base * 1.15f, .dur = 0.13f, .gap = 0.14f, .bright = 1.4f, .open = 1.2f, .noise = 0.1f});
        add(p, (syl_t){.f0 = base * 0.8f, .f1 = base * 1.2f, .dur = 0.13f, .gap = 0.2f, .bright = 1.4f, .open = 1.2f, .noise = 0.1f});
        add(p, (syl_t){.f0 = base * 0.72f, .f1 = base * 0.6f, .dur = 0.3f, .vib_rate = 5.0f, .vib_depth = 0.03f, .bright = 1.3f, .open = 1.1f});
        break;
    case VOICE_ACHOOLET: /* "ah... aah... pchi!" small and through the nose */
        add(p, (syl_t){.f0 = base * 0.9f, .f1 = base * 1.15f, .dur = 0.18f, .gap = 0.12f, .bright = 1.7f, .open = 1.5f, .noise = 0.04f});
        add(p, (syl_t){.f0 = base * 1.05f, .f1 = base * 1.45f, .dur = 0.24f, .gap = 0.1f, .bright = 1.8f, .open = 1.6f, .noise = 0.04f});
        add(p, (syl_t){.f0 = base * 1.8f, .f1 = base * 1.1f, .dur = 0.12f, .gap = 0.06f, .bright = 1.9f, .open = 1.4f, .noise = 0.1f});
        add(p, (syl_t){.f0 = base * 0.8f, .f1 = base * 0.65f, .dur = 0.24f, .bright = 1.4f, .open = 1.2f});
        break;
    /* the cable pulled out: soft pops (a pitch drop, no hiss), blips for the pulses of charge */
    case VOICE_POPFULL: /* short: "pok! - ah" and a ding */
        add(p, (syl_t){.f0 = base * 2.4f, .f1 = base * .9f, .dur = .04f, .gap = .06f, .bright = 1.6f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * .95f, .f1 = base * 1.25f, .dur = .2f, .gap = .02f, .bright = 1.9f, .open = 2.0f});
        add(p, (syl_t){.f0 = 1568, .f1 = 1570, .dur = .16f, .bright = 1.5f, .open = 1.2f, .amp = .7f});
        break;
    case VOICE_CHARGEDUP: /* short and calm: the plug easing out (a soft slide down), a muffled click, a content "mm" */
        add(p, (syl_t){.f0 = base * 1.8f, .f1 = base * 1.0f, .dur = .14f, .bright = 1.2f, .open = 1.1f, .amp = .7f});
        add(p, (syl_t){.f0 = 1175, .f1 = 1046, .dur = .025f, .gap = .1f, .bright = 1.2f, .open = 1.1f, .amp = .5f});
        add(p, (syl_t){.f0 = base * .85f, .f1 = base * .95f, .dur = .22f, .vib_rate = 4, .vib_depth = .015f, .bright = 1.2f, .open = 1.0f});
        break;
    case VOICE_YANK: /* short: "pok! - eh?!" */
        add(p, (syl_t){.f0 = base * 2.2f, .f1 = base * .8f, .dur = .04f, .gap = .07f, .bright = 1.6f, .open = 1.4f});
        add(p, (syl_t){.f0 = base * .95f, .f1 = base * 1.7f, .dur = .18f, .bright = 2.1f, .open = 1.8f});
        break;
    case VOICE_NOTFULL: /* short: two low blips and a pouty "uh" down */
        add(p, (syl_t){.f0 = 392, .f1 = 390, .dur = .06f, .gap = .05f, .bright = 1.5f, .open = 1.2f, .amp = .7f});
        add(p, (syl_t){.f0 = 330, .f1 = 328, .dur = .07f, .gap = .06f, .bright = 1.5f, .open = 1.2f, .amp = .7f});
        add(p, (syl_t){.f0 = base * 1.0f, .f1 = base * .7f, .dur = .24f, .bright = 1.4f, .open = 1.2f});
        break;
    case VOICE_POWERFAIL: /* short: a soft whine sliding down to nothing */
        add(p, (syl_t){.f0 = base * 1.6f, .f1 = base * .45f, .dur = .4f, .vib_rate = 9, .vib_depth = .02f, .bright = 1.3f, .open = 1.2f});
        break;
    case VOICE_FEEBLE: /* short: weak "m... mm" coming back on */
        add(p, (syl_t){.f0 = base * .8f, .f1 = base * .85f, .dur = .1f, .gap = .08f, .bright = 1.1f, .open = 1.0f, .amp = .7f});
        add(p, (syl_t){.f0 = base * .85f, .f1 = base * .7f, .dur = .26f, .vib_rate = 6, .vib_depth = .03f, .bright = 1.1f, .open = 1.0f});
        break;
    case VOICE_RETCH: /* short: a gulp held in (cheeks puffed), then a rough low "hweh-uhp", no hiss */
        add(p, (syl_t){.f0 = base * .8f, .f1 = base * .7f, .dur = .07f, .gap = .12f, .bright = 1.1f, .open = .9f, .amp = .7f});
        add(p, (syl_t){.f0 = base * .75f, .f1 = base * .55f, .dur = .22f, .gap = .02f, .rough = .3f, .bright = 1.5f, .open = 1.7f});
        add(p, (syl_t){.f0 = base * .6f, .f1 = base * .5f, .dur = .09f, .rough = .2f, .bright = 1.2f, .open = .9f});
        break;
    case VOICE_GROGGY: /* "mwuh?..": slow and low, breathy, sinking, a little rough */
        add(p, (syl_t){.f0 = base * .7f, .f1 = base * .86f, .dur = .32f, .gap = .06f, .bright = 1.8f, .open = 1.3f, .noise = .08f});
        add(p, (syl_t){.f0 = base * .82f, .f1 = base * .62f, .dur = .32f, .rough = .1f, .bright = 1.8f, .open = 1.3f, .noise = .06f});
        break;
    case VOICE_SKYLIT: { /* stars coming out: far bells, slow, the theme's first notes (G C E) lost among them */
        static const float n[9] = {7, 24, 12, 19, 16, 28, 14, 21, 24};
        for (int i = 0; i < 9; i++)
            add(p, (syl_t){.f0 = 392 * semi(n[i] - 7 + 7), .f1 = 392 * semi(n[i] + .04f), .dur = i % 3 == 2 ? .34f : .2f, .gap = .05f + .04f * (i % 2), .bright = 1.6f, .open = 1.3f,
                           .vib_rate = 6, .vib_depth = .006f, .amp = .5f + .05f * i});
        break;
    }
    case VOICE_HEART: /* "tu-dum": a heart, close */
        add(p, (syl_t){.f0 = 330, .f1 = 196, .dur = .13f, .gap = .1f, .bright = 2.2f, .open = 1.4f});
        add(p, (syl_t){.f0 = 280, .f1 = 165, .dur = .2f, .bright = 2.0f, .open = 1.3f, .amp = .85f});
        break;
    case VOICE_RISER: { /* the galaxy winding up: the theme's notes climbing, quicker and quicker, higher and higher */
        static const float n[16] = {0, 4, 7, 12, 4, 7, 12, 16, 7, 12, 16, 19, 12, 16, 19, 24};
        for (int i = 0; i < 16; i++)
            add(p, (syl_t){.f0 = 392 * semi(n[i]), .f1 = 392 * semi(n[i] + .3f), .dur = .17f - .0075f * i, .gap = .035f - .002f * i, .bright = 1.7f + .06f * i, .open = 1.4f,
                           .vib_rate = 8, .vib_depth = .008f, .amp = .55f + .03f * i});
        break;
    }
    case VOICE_GRAND: { /* the theme, whole: sol do mi, re sol, do' - broad, sung with the mouth open */
        static const float n[6] = {-5, 0, 4, 2, 7, 12};
        static const float d[6] = {.26f, .26f, .46f, .26f, .46f, .95f};
        for (int i = 0; i < 6; i++)
            add(p, (syl_t){.f0 = 523.3f * semi(n[i] - .5f), .f1 = 523.3f * semi(n[i]), .dur = d[i], .gap = .02f, .bright = 2.4f, .open = 2.0f, .vib_rate = 5.5f, .vib_depth = d[i] > .4f ? .02f : .008f,
                           .amp = .8f + .04f * i});
        break;
    }
    case VOICE_ANTHEM: { /* ...and once more, for joy: quick, a third higher at the end, a flourish on top */
        static const float n[14] = {-5, 0, 4, 2, 7, 12, 12, 11, 12, 16, 12, 16, 19, 24};
        static const float d[14] = {.12f, .12f, .22f, .12f, .22f, .34f, .1f, .1f, .1f, .34f, .07f, .07f, .07f, .62f};
        for (int i = 0; i < 14; i++)
            add(p, (syl_t){.f0 = 523.3f * semi(n[i] - .4f), .f1 = 523.3f * semi(n[i]), .dur = d[i], .gap = .018f, .bright = 2.3f, .open = 1.8f, .vib_rate = 6, .vib_depth = d[i] > .3f ? .02f : 0});
        break;
    }
    /* the little scenes with pictures: two sounds each, of the same soft palette (no hiss: noise only where a breath is meant) */
    /* ---- the twenty new faces, two sounds each ---- */
    case VOICE_WOW: /* "wooo-aaah": up and open, a slow shimmer on top */
        add(p, (syl_t){.f0 = base * .8f, .f1 = base * 1.75f, .dur = .3f, .bright = 2.2f, .open = 2.4f, .vib_rate = 5, .vib_depth = .02f});
        add(p, (syl_t){.f0 = base * 1.75f, .f1 = base * 1.45f, .dur = .42f, .bright = 2.8f, .open = 2.2f, .vib_rate = 7, .vib_depth = .035f});
        break;
    case VOICE_TWINKLE: { /* little bells tumbling up and down, the last two ringing */
        static const float n[8] = {12, 16, 19, 24, 21, 19, 24, 28};
        for (int i = 0; i < 8; i++)
            add(p, (syl_t){.f0 = 392 * semi(n[i]), .f1 = 392 * semi(n[i] + .1f), .dur = i > 5 ? .26f : .06f, .gap = i > 5 ? 0 : .03f, .bright = 1.8f, .open = 1.4f,
                           .vib_rate = i > 5 ? 8 : 0, .vib_depth = .012f, .amp = .7f + .04f * i});
        break;
    }
    case VOICE_LALA: { /* "la-la-la, la-laa": a waltz he made up, never in quite the same key */
        static const float n[7] = {7, 4, 7, 12, 9, 7, 4};
        static const float d[7] = {.22f, .13f, .13f, .26f, .13f, .13f, .42f};
        for (int i = 0; i < 7; i++)
            add(p, (syl_t){.f0 = base * .95f * semi(n[i] - .8f), .f1 = base * .95f * semi(n[i]), .dur = d[i], .gap = .03f, .bright = 2.2f, .open = 2.0f,
                           .vib_rate = d[i] > .2f ? 6 : 0, .vib_depth = .022f});
        break;
    }
    case VOICE_ARIA: /* a tenor's solo: a scoop up, a long held note swelling in vibrato, a tumble down the scale */
        add(p, (syl_t){.f0 = base * .75f, .f1 = base * 1.5f, .dur = .22f, .bright = 2.2f, .open = 1.8f});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 1.5f, .dur = .7f, .vib_rate = 6, .vib_depth = .045f, .bright = 2.6f, .open = 2.3f});
        for (int i = 0; i < 4; i++) {
            static const float n[4] = {5, 4, 2, 0};
            add(p, (syl_t){.f0 = base * semi(n[i]), .f1 = base * semi(n[i] - .2f), .dur = i == 3 ? .3f : .09f, .gap = .015f, .bright = 2.3f, .open = 1.7f, .vib_rate = 6, .vib_depth = i == 3 ? .03f : 0});
        }
        break;
    case VOICE_MWAH: /* "mmm... CHMOK! aah": lips shut and humming up, the pop, a melting breath */
        add(p, (syl_t){.f0 = base * .8f, .f1 = base * 1.25f, .dur = .26f, .bright = 1.4f, .open = 1.1f, .vib_rate = 7, .vib_depth = .015f, .amp = .8f});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 3.2f, .dur = .045f, .gap = .03f, .bright = 5, .open = 1.6f, .noise = .9f});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 1.05f, .dur = .3f, .bright = 2.4f, .open = 2.3f, .vib_rate = 6, .vib_depth = .025f, .noise = .03f});
        break;
    case VOICE_SMOOCH: /* two quick pecks and a shy little "hi" */
        for (int i = 0; i < 2; i++) {
            add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 1.35f, .dur = .06f, .bright = 1.4f, .open = 1.1f, .amp = .7f});
            add(p, (syl_t){.f0 = base * 1.7f, .f1 = base * 3.4f, .dur = .04f, .gap = .1f, .bright = 5, .open = 1.6f, .noise = .8f});
        }
        add(p, (syl_t){.f0 = base * 1.6f, .f1 = base * 2.0f, .dur = .12f, .bright = 2.4f, .open = 1.7f, .vib_rate = 12, .vib_depth = .02f});
        break;
    case VOICE_HEYYOU: /* "he-ey...": low and sidling up, a lazy curl at the end */
        add(p, (syl_t){.f0 = base * .72f, .f1 = base * .85f, .dur = .12f, .gap = .02f, .bright = 2, .open = 1.6f, .noise = .05f});
        add(p, (syl_t){.f0 = base * .85f, .f1 = base * 1.35f, .dur = .24f, .bright = 2.2f, .open = 2.0f, .vib_rate = 6, .vib_depth = .02f});
        add(p, (syl_t){.f0 = base * 1.35f, .f1 = base * .95f, .dur = .26f, .bright = 2, .open = 1.7f, .vib_rate = 7, .vib_depth = .03f});
        break;
    case VOICE_WINK: /* a tiny "pip" and a glint: ting! */
        add(p, (syl_t){.f0 = base * 1.2f, .f1 = base * 1.6f, .dur = .07f, .gap = .04f, .bright = 2.6f, .open = 1.6f});
        add(p, (syl_t){.f0 = 1568, .f1 = 1570, .dur = .04f, .bright = 2.2f, .open = 1.5f});
        add(p, (syl_t){.f0 = 2093, .f1 = 2088, .dur = .36f, .bright = 1.6f, .open = 1.25f, .vib_rate = 9, .vib_depth = .008f, .amp = .7f});
        break;
    case VOICE_XYLO: { /* seven colours: a xylophone run up the scale, and the top note again */
        static const float n[9] = {0, 2, 4, 5, 7, 9, 11, 12, 12};
        for (int i = 0; i < 9; i++)
            add(p, (syl_t){.f0 = 587 * semi(n[i]), .f1 = 587 * semi(n[i] - .15f), .dur = i == 8 ? .3f : .075f, .gap = i == 7 ? .07f : .02f, .bright = 2.0f, .open = 1.3f,
                           .vib_rate = i == 8 ? 7 : 0, .vib_depth = .012f, .amp = .75f + .03f * i});
        break;
    }
    case VOICE_PRISM: /* a shimmering "aaa-ooo-iii": one breath gliding over three vowels and back */
        add(p, (syl_t){.f0 = base, .f1 = base * 1.5f, .dur = .34f, .vib_rate = 11, .vib_depth = .025f, .bright = 1.8f, .open = 2.6f});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 2.0f, .dur = .34f, .vib_rate = 12, .vib_depth = .025f, .bright = 3.0f, .open = 1.2f});
        add(p, (syl_t){.f0 = base * 2.0f, .f1 = base * 1.5f, .dur = .4f, .vib_rate = 10, .vib_depth = .03f, .bright = 1.8f, .open = 2.0f, .amp = .85f});
        break;
    case VOICE_KNOCK: /* hiding: two soft knocks, and a careful "u-u?" */
        for (int i = 0; i < 2; i++)
            add(p, (syl_t){.f0 = base * .72f, .f1 = base * .6f, .dur = .075f, .gap = i ? .2f : .11f, .bright = 1.6f, .open = 1.1f, .amp = .85f});
        add(p, (syl_t){.f0 = base * .95f, .f1 = base * 1.05f, .dur = .13f, .gap = .04f, .bright = 1.5f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 1.05f, .f1 = base * 1.5f, .dur = .2f, .bright = 1.8f, .open = 1.5f, .vib_rate = 7, .vib_depth = .02f});
        break;
    case VOICE_POPUP: /* a slide whistle up out of hiding, and "ba!" */
        add(p, (syl_t){.f0 = base * .7f, .f1 = base * 2.4f, .dur = .3f, .gap = .02f, .bright = 1.6f, .open = 1.2f, .amp = .85f});
        add(p, (syl_t){.f0 = base * 1.6f, .f1 = base * 1.35f, .dur = .17f, .bright = 3, .open = 2.4f});
        add(p, (syl_t){.f0 = base * 2.0f, .f1 = base * 2.3f, .dur = .08f, .bright = 2.6f, .open = 1.6f, .vib_rate = 14, .vib_depth = .02f});
        break;
    case VOICE_OM: /* "ooommm": the mouth slowly closing on a long low note */
        add(p, (syl_t){.f0 = base * .62f, .f1 = base * .62f, .dur = .75f, .bright = 2.6f, .open = 1.7f, .vib_rate = 4, .vib_depth = .012f});
        add(p, (syl_t){.f0 = base * .62f, .f1 = base * .6f, .dur = .95f, .bright = 1.4f, .open = 1.05f, .vib_rate = 4, .vib_depth = .014f, .amp = .9f});
        break;
    case VOICE_BOWL: /* a singing bowl struck: a soft knock, then the ring circling round and fading */
        add(p, (syl_t){.f0 = 660, .f1 = 655, .dur = .05f, .bright = 3, .open = 1.6f, .noise = .1f});
        for (int i = 0; i < 5; i++)
            add(p, (syl_t){.f0 = 659 * (i & 1 ? 1.004f : 1), .f1 = 659, .dur = .34f, .vib_rate = 5.5f, .vib_depth = .006f, .bright = 1.6f, .open = 1.2f, .amp = 1 - .16f * i});
        break;
    case VOICE_SNORE: /* "hrrr... fiu": in through the nose, out in a little whistle */
        add(p, (syl_t){.f0 = base * .42f, .f1 = base * .52f, .dur = .6f, .gap = .12f, .rough = .9f, .bright = 3.2f, .open = 1.3f, .noise = .3f, .amp = .9f});
        add(p, (syl_t){.f0 = base * 2.1f, .f1 = base * 1.3f, .dur = .62f, .bright = 1.5f, .open = 1.15f, .noise = .2f, .vib_rate = 6, .vib_depth = .015f, .amp = .75f});
        break;
    case VOICE_DREAMSONG: { /* talking in his sleep, sweetly: "nya... mm-myam... fuuu" */
        add(p, (syl_t){.f0 = base * .85f, .f1 = base * 1.1f, .dur = .2f, .gap = .16f, .bright = 1.8f, .open = 1.6f, .vib_rate = 5, .vib_depth = .02f});
        add(p, (syl_t){.f0 = base * .8f, .f1 = base * .75f, .dur = .13f, .gap = .05f, .bright = 1.4f, .open = 1.1f});
        add(p, (syl_t){.f0 = base * .9f, .f1 = base * .8f, .dur = .18f, .gap = .18f, .bright = 1.7f, .open = 1.7f});
        add(p, (syl_t){.f0 = base * 1.0f, .f1 = base * .6f, .dur = .6f, .bright = 1.6f, .open = 1.2f, .noise = .12f, .vib_rate = 4, .vib_depth = .02f, .amp = .8f});
        break;
    }
    case VOICE_MELT: /* "aaawwwuuu": sliding down like syrup, wobbling on the way */
        add(p, (syl_t){.f0 = base * 1.6f, .f1 = base * 1.3f, .dur = .28f, .bright = 2.6f, .open = 2.2f, .vib_rate = 6, .vib_depth = .03f});
        add(p, (syl_t){.f0 = base * 1.3f, .f1 = base * .6f, .dur = .75f, .bright = 1.9f, .open = 1.4f, .vib_rate = 4.5f, .vib_depth = .06f});
        break;
    case VOICE_COO: /* a dove's "gu-guu-u" */
        add(p, (syl_t){.f0 = base * .85f, .f1 = base * 1.0f, .dur = .12f, .gap = .04f, .bright = 1.6f, .open = 1.3f, .vib_rate = 22, .vib_depth = .04f});
        add(p, (syl_t){.f0 = base * 1.0f, .f1 = base * 1.18f, .dur = .3f, .gap = .04f, .bright = 1.6f, .open = 1.5f, .vib_rate = 22, .vib_depth = .04f});
        add(p, (syl_t){.f0 = base * 1.05f, .f1 = base * .82f, .dur = .3f, .bright = 1.5f, .open = 1.3f, .vib_rate = 5, .vib_depth = .02f});
        break;
    case VOICE_CLAPS: /* little palms: round "pat-pat-pat", two pitches by turns, quickening; no hiss */
        for (int i = 0; i < 8; i++) {
            float f = base * (i & 1 ? .98f : .84f);
            add(p, (syl_t){.f0 = f, .f1 = f * .74f, .dur = .065f, .gap = .12f - .011f * i, .bright = 1.8f, .open = 1.25f, .amp = .8f + .025f * i});
        }
        break;
    case VOICE_BRAVO: /* "bra-VOO! u-hu!" */
        add(p, (syl_t){.f0 = base * 1.0f, .f1 = base * 1.1f, .dur = .12f, .gap = .03f, .bright = 2.8f, .open = 1.8f});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 1.7f, .dur = .34f, .gap = .09f, .bright = 2.6f, .open = 2.0f, .vib_rate = 7, .vib_depth = .03f});
        add(p, (syl_t){.f0 = base * 1.4f, .f1 = base * 1.5f, .dur = .08f, .gap = .03f, .bright = 2.6f, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 2.1f, .f1 = base * 1.8f, .dur = .18f, .bright = 2.6f, .open = 1.8f});
        break;
    case VOICE_BASSLINE: { /* "doo, doo, da-doom": a walking bass hummed softly, each note eased into, the first the gentlest */
        static const float n[6] = {0, 0, 3, 5, 3, 0};
        static const float d[6] = {.22f, .13f, .17f, .13f, .13f, .34f};
        static const float a[6] = {.62f, .75f, .9f, .95f, .95f, 1};
        for (int i = 0; i < 6; i++)
            add(p, (syl_t){.f0 = base * .68f * semi(n[i] - .5f), .f1 = base * .68f * semi(n[i]), .dur = d[i], .gap = i == 1 ? .09f : .03f, .bright = 1.5f, .open = 1.2f,
                           .vib_rate = i == 5 ? 5 : 0, .vib_depth = .018f, .amp = a[i]});
        break;
    }
    case VOICE_YEAH: /* an unhurried "mm... yeea-ah" */
        add(p, (syl_t){.f0 = base * .8f, .f1 = base * .86f, .dur = .16f, .gap = .07f, .bright = 1.4f, .open = 1.1f, .amp = .8f});
        add(p, (syl_t){.f0 = base * .95f, .f1 = base * 1.15f, .dur = .18f, .bright = 2.4f, .open = 1.7f});
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * .68f, .dur = .4f, .bright = 2.1f, .open = 1.7f, .vib_rate = 4.5f, .vib_depth = .03f});
        break;
    case VOICE_WHIMPER: /* "u-uu-uuu?": a puppy asking, each a little higher and more hopeful */
        for (int i = 0; i < 3; i++)
            add(p, (syl_t){.f0 = base * (1.25f + .12f * i), .f1 = base * (1.6f + .2f * i), .dur = .13f + .07f * i, .gap = .07f, .bright = 1.8f, .open = 1.3f, .vib_rate = 9, .vib_depth = .035f, .noise = .02f});
        break;
    case VOICE_YIP: /* "yip! yip-yip!" */
        for (int i = 0; i < 3; i++) {
            add(p, (syl_t){.f0 = base * 1.2f, .f1 = base * 2.2f, .dur = .05f, .bright = 2.8f, .open = 1.8f});
            add(p, (syl_t){.f0 = base * 2.0f, .f1 = base * 1.1f, .dur = .07f, .gap = i ? .04f : .14f, .bright = 2.8f, .open = 1.6f});
        }
        break;
    case VOICE_FLUTTER: /* wings: a soft trill of two notes drifting up, then settling */
        for (int i = 0; i < 12; i++) {
            float u = i / 11.0f, k = 1.25f + .55f * sin01(u * .5f);
            add(p, (syl_t){.f0 = base * k * (i & 1 ? 1.19f : 1), .f1 = base * k * (i & 1 ? 1.16f : 1.03f), .dur = .05f, .gap = .022f, .bright = 1.7f, .open = 1.3f, .amp = .75f + .25f * sin01(u * .5f)});
        }
        break;
    case VOICE_HUSHED: /* breath held: a whispered "ooh...", barely there, and a tiny glad "mi" */
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 1.35f, .dur = .38f, .gap = .1f, .bright = 1.8f, .open = 1.8f, .noise = .3f, .vib_rate = 4, .vib_depth = .015f, .amp = .8f});
        add(p, (syl_t){.f0 = base * 1.7f, .f1 = base * 1.95f, .dur = .14f, .bright = 2, .open = 1.4f, .vib_rate = 10, .vib_depth = .02f});
        break;
    case VOICE_BLUB: /* "blub... blub-blub": bubbles let go under water, each a quick scoop up */
        for (int i = 0, n = 5 + (int)rnd(0, 2); i < n; i++) {
            float f = base * rnd(.7f, 1.0f);
            add(p, (syl_t){.f0 = f, .f1 = f * 2.1f, .dur = rnd(.06f, .09f), .gap = i % 3 == 0 ? .13f : .035f, .bright = 1.5f, .open = 1.5f});
        }
        break;
    case VOICE_BUBBLEPOP: /* "pok!" and a surprised giggle */
        add(p, (syl_t){.f0 = base * 1.0f, .f1 = base * 3.0f, .dur = .035f, .gap = .14f, .bright = 3, .open = 1.4f, .noise = .3f});
        for (int i = 0; i < 4; i++)
            add(p, (syl_t){.f0 = base * (1.6f + .1f * (i & 1)), .f1 = base * (1.85f + .1f * (i & 1)), .dur = .055f, .gap = .045f, .bright = 2.6f, .open = 1.7f, .vib_rate = 16, .vib_depth = .02f, .noise = .03f});
        break;
    case VOICE_BEEPBOOP: { /* "beep-boop-bip, bleep": steady tones, no glide at all */
        static const float n[6] = {12, 5, 14, 7, 17, 12};
        for (int i = 0, m = 4 + (int)rnd(0, 2.99f); i < m; i++) {
            float f = base * .8f * semi(n[i]);
            add(p, (syl_t){.f0 = f, .f1 = f, .dur = i & 1 ? .13f : .075f, .gap = .04f, .bright = 3.0f, .open = 1.0f});
        }
        break;
    }
    case VOICE_SERVO: /* joints: a motor whirring up, a step, whirring back, and a beep "done" */
        add(p, (syl_t){.f0 = base * .6f, .f1 = base * 1.1f, .dur = .28f, .gap = .04f, .rough = .6f, .bright = 3, .open = 1.1f, .vib_rate = 30, .vib_depth = .02f});
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * .75f, .dur = .2f, .gap = .06f, .rough = .6f, .bright = 3, .open = 1.1f, .vib_rate = 30, .vib_depth = .02f});
        add(p, (syl_t){.f0 = base * 2.0f, .f1 = base * 2.0f, .dur = .07f, .gap = .03f, .bright = 2.6f, .open = 1.0f, .amp = .85f});
        add(p, (syl_t){.f0 = base * 2.0f, .f1 = base * 2.0f, .dur = .07f, .bright = 2.6f, .open = 1.0f, .amp = .85f});
        break;
    case VOICE_PURR: /* "mrrr-mrrr": in and out */
        for (int i = 0; i < 3; i++)
            add(p, (syl_t){.f0 = base * (i & 1 ? .56f : .64f), .f1 = base * (i & 1 ? .6f : .58f), .dur = .42f, .gap = .05f, .rough = 1, .bright = 2.6f, .open = 1.3f, .noise = .05f});
        break;
    case VOICE_MEOW: /* "mi-aa-ow": mouth shut, wide open, shut */
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 1.5f, .dur = .12f, .bright = 1.5f, .open = 1.2f});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 1.7f, .dur = .16f, .bright = 2.6f, .open = 2.4f, .vib_rate = 6, .vib_depth = .02f});
        add(p, (syl_t){.f0 = base * 1.65f, .f1 = base * .95f, .dur = .32f, .bright = 2.2f, .open = 1.5f, .vib_rate = 6, .vib_depth = .02f});
        break;
    case VOICE_BLBLBL: /* a finger over the lips: "blblblbl-brr!" */
        add(p, (syl_t){.f0 = base * .9f, .f1 = base * 1.5f, .dur = .4f, .rough = 1, .bright = 2.6f, .open = 1.5f, .vib_rate = 19, .vib_depth = .14f});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * .8f, .dur = .3f, .gap = .03f, .rough = 1, .bright = 2.6f, .open = 1.5f, .vib_rate = 19, .vib_depth = .14f});
        add(p, (syl_t){.f0 = base * 1.9f, .f1 = base * 2.2f, .dur = .08f, .bright = 2.6f, .open = 1.6f});
        break;
    case VOICE_YOYO: /* "ua-ua-uaa": a voice on a yo-yo */
        for (int i = 0; i < 3; i++) {
            add(p, (syl_t){.f0 = base * .7f, .f1 = base * (1.5f + .25f * i), .dur = .12f, .bright = 2.2f, .open = 2.0f});
            add(p, (syl_t){.f0 = base * (1.5f + .25f * i), .f1 = base * .75f, .dur = .14f, .gap = .02f, .bright = 2.2f, .open = 1.4f});
        }
        break;
    case VOICE_PFFT: /* a laugh that will not stay in, behind shut lips: "mf... mf-mf... mmm-hi!" */
        add(p, (syl_t){.f0 = base * .9f, .f1 = base * .82f, .dur = .09f, .gap = .16f, .bright = 1.4f, .open = 1.1f, .amp = .75f});
        for (int i = 0; i < 2; i++)
            add(p, (syl_t){.f0 = base * (.95f + .08f * i), .f1 = base * (.85f + .08f * i), .dur = .08f, .gap = i ? .13f : .04f, .bright = 1.4f, .open = 1.1f, .amp = .85f});
        add(p, (syl_t){.f0 = base * .95f, .f1 = base * 1.3f, .dur = .28f, .gap = .02f, .bright = 1.4f, .open = 1.15f, .vib_rate = 13, .vib_depth = .05f});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 1.7f, .dur = .07f, .gap = .04f, .bright = 2.2f, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 1.45f, .f1 = base * 1.6f, .dur = .08f, .bright = 2.2f, .open = 1.6f, .amp = .9f});
        break;
    case VOICE_CRACKUP: /* "mm-HA! ha-ha-ha-ha, ooh": lower and rounder, the run slides down and slows, a sigh after */
        add(p, (syl_t){.f0 = base * .85f, .f1 = base * 1.05f, .dur = .12f, .gap = .02f, .bright = 1.4f, .open = 1.15f, .amp = .75f});
        for (int i = 0; i < 6; i++)
            add(p, (syl_t){.f0 = base * (1.35f - .07f * i), .f1 = base * (1.15f - .07f * i), .dur = i ? .095f : .14f, .gap = .045f + .006f * i, .bright = 2.0f, .open = 1.7f, .amp = 1 - .04f * i});
        add(p, (syl_t){.f0 = base * 1.15f, .f1 = base * .72f, .dur = .36f, .bright = 1.7f, .open = 1.4f, .vib_rate = 6, .vib_depth = .03f, .amp = .8f});
        break;
    case VOICE_CRACKLE: /* the fire, heard from under a blanket: soft round pops of all sizes over now and then a low "whuff" */
        for (int i = 0; i < 12; i++) {
            float f = base * rnd(.6f, 1.25f);
            if (i % 5 == 2) add(p, (syl_t){.f0 = base * .55f, .f1 = base * .62f, .dur = .2f, .gap = .04f, .bright = 1.4f, .open = 1.2f, .vib_rate = 9, .vib_depth = .05f, .amp = .7f});
            else add(p, (syl_t){.f0 = f, .f1 = f * rnd(.6f, .8f), .dur = rnd(.035f, .06f), .gap = rnd(.03f, .13f), .bright = 1.8f, .open = 1.2f, .amp = rnd(.68f, .85f)});
        }
        break;
    case VOICE_COZY: /* warm through: "mmm... ha-aah" */
        add(p, (syl_t){.f0 = base * .72f, .f1 = base * .82f, .dur = .42f, .gap = .06f, .bright = 1.4f, .open = 1.15f, .vib_rate = 4.5f, .vib_depth = .02f});
        add(p, (syl_t){.f0 = base * .9f, .f1 = base * .58f, .dur = .62f, .bright = 2.0f, .open = 1.8f, .noise = .1f, .vib_rate = 4, .vib_depth = .02f, .amp = .85f});
        break;
    case VOICE_SHOOTING: /* a star falling: a glassy run sliding down the sky, its tail fading */
        for (int i = 0; i < 12; i++) {
            float f = 2093 * semi(-2 * i + (i & 1 ? 1 : 0));
            add(p, (syl_t){.f0 = f, .f1 = f * .97f, .dur = .065f, .gap = .006f, .bright = 1.7f, .open = 1.3f, .amp = 1 - .045f * i});
        }
        add(p, (syl_t){.f0 = 587, .f1 = 560, .dur = .3f, .bright = 1.6f, .open = 1.2f, .vib_rate = 6, .vib_depth = .012f, .amp = .55f});
        break;
    case VOICE_WISH: /* eyes shut: a whispered wish, and two far-off bells answer */
        for (int i = 0; i < 4; i++)
            add(p, (syl_t){.f0 = base * rnd(.95f, 1.2f), .f1 = base * rnd(.9f, 1.2f), .dur = rnd(.08f, .13f), .gap = .04f, .bright = 2, .open = 1.5f, .noise = .5f, .amp = .7f});
        add(p, (syl_t){.f0 = 1174.7f, .f1 = 1177, .dur = .14f, .gap = .04f, .bright = 1.8f, .open = 1.4f, .amp = .8f});
        add(p, (syl_t){.f0 = 1760, .f1 = 1756, .dur = .5f, .bright = 1.6f, .open = 1.25f, .vib_rate = 6, .vib_depth = .01f, .amp = .75f});
        break;
    case VOICE_ZAP: /* a spark that does not bite: three bubbly "bi-bi-bi" and a wobbling "bzuu-ip!" up */
        for (int i = 0; i < 3; i++)
            add(p, (syl_t){.f0 = base * (.95f + .12f * i), .f1 = base * (1.1f + .12f * i), .dur = .055f, .gap = .035f, .bright = 1.8f, .open = 1.3f, .amp = .75f + .08f * i});
        add(p, (syl_t){.f0 = base * .8f, .f1 = base * 1.7f, .dur = .34f, .gap = .02f, .bright = 1.9f, .open = 1.4f, .vib_rate = 20, .vib_depth = .05f});
        add(p, (syl_t){.f0 = base * 1.7f, .f1 = base * 1.85f, .dur = .13f, .bright = 1.8f, .open = 1.3f});
        break;
    case VOICE_POWERUP: { /* a game's power-up: quick triads climbing, the top one held and shaking with energy */
        static const float n[10] = {0, 4, 7, 5, 9, 12, 7, 11, 14, 19};
        for (int i = 0; i < 10; i++)
            add(p, (syl_t){.f0 = base * .9f * semi(n[i]), .f1 = base * .9f * semi(n[i]), .dur = i == 9 ? .34f : .055f, .gap = .008f, .bright = 2.6f, .open = 1.2f,
                           .vib_rate = i == 9 ? 13 : 0, .vib_depth = .03f});
        break;
    }
    case VOICE_GULP: /* "glup, glup, glup... aaah!": drinking it in, each swallow a little higher */
        for (int i = 0; i < 4; i++) {
            float f = base * (.62f + .06f * i);
            add(p, (syl_t){.f0 = f, .f1 = f * .78f, .dur = .05f, .bright = 1.5f, .open = 1.2f});
            add(p, (syl_t){.f0 = f * .85f, .f1 = f * 1.9f, .dur = .075f, .gap = .085f, .bright = 1.6f, .open = 1.5f});
        }
        add(p, (syl_t){.f0 = base * 1.4f, .f1 = base * .85f, .dur = .5f, .bright = 2.2f, .open = 2.2f, .noise = .1f, .vib_rate = 5, .vib_depth = .02f});
        break;
    case VOICE_FILLUP: /* filling up: a soft hum rising like a glass under the tap, and a ding at the brim */
        add(p, (syl_t){.f0 = base * .6f, .f1 = base * 1.0f, .dur = .45f, .vib_rate = 8, .vib_depth = .02f, .bright = 1.5f, .open = 1.3f});
        add(p, (syl_t){.f0 = base * 1.0f, .f1 = base * 1.7f, .dur = .35f, .gap = .05f, .vib_rate = 11, .vib_depth = .022f, .bright = 1.8f, .open = 1.5f});
        add(p, (syl_t){.f0 = 1318.5f, .f1 = 1320, .dur = .06f, .bright = 2.2f, .open = 1.5f});
        add(p, (syl_t){.f0 = 1760, .f1 = 1757, .dur = .42f, .bright = 1.6f, .open = 1.25f, .vib_rate = 6, .vib_depth = .01f, .amp = .8f});
        break;
    /* New Year: bells at a fixed pitch and pace - pip.c's scenes are timed to them */
    case VOICE_JINGLE: { /* sleigh bells shaken (quick warbling high pairs), a little tune of its own, then a ding held as the
                            star on the tree lights (at ~1.7 s) */
        static const float tune[5] = {784.0f, 1318.5f, 1174.7f, 1046.5f, 1568.0f}; /* G5 E6 D6 C6 G6 */
        for (int i = 0; i < 4; i++)
            add(p, (syl_t){.f0 = i & 1 ? 2093 : 1976, .f1 = i & 1 ? 2150 : 2030, .dur = .05f, .gap = .04f, .vib_rate = 32, .vib_depth = .04f,
                           .bright = 1.6f, .open = 1.2f, .amp = .7f});
        for (int i = 0; i < 5; i++)
            add(p, (syl_t){.f0 = tune[i], .f1 = tune[i] * 1.004f, .dur = i == 4 ? .2f : .12f, .gap = i == 4 ? .46f : .05f, .bright = 1.9f, .open = 1.4f,
                           .amp = .85f});
        add(p, (syl_t){.f0 = 2093, .f1 = 2097, .dur = .55f, .vib_rate = 6, .vib_depth = .01f, .bright = 1.6f, .open = 1.25f, .amp = .75f}); /* the star */
        break;
    }
    case VOICE_OHTREE: /* "mm? mm-mm..." looking the tree over, then "o-oh!" up as the star lights, and a bell */
        add(p, (syl_t){.f0 = base * .8f, .f1 = base * .95f, .dur = .22f, .gap = .12f, .vib_rate = 5, .vib_depth = .02f, .bright = 1.5f, .open = 1.2f});
        add(p, (syl_t){.f0 = base * .9f, .f1 = base * .85f, .dur = .16f, .gap = .06f, .bright = 1.5f, .open = 1.2f});
        add(p, (syl_t){.f0 = base * .95f, .f1 = base * .82f, .dur = .3f, .gap = .64f, .vib_rate = 5, .vib_depth = .02f, .bright = 1.5f, .open = 1.2f});
        add(p, (syl_t){.f0 = base * 1.0f, .f1 = base * 1.25f, .dur = .12f, .gap = .03f, .bright = 2.6f, .open = 1.9f});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 1.75f, .dur = .42f, .gap = .02f, .vib_rate = 7, .vib_depth = .035f, .bright = 2.6f, .open = 2.1f});
        add(p, (syl_t){.f0 = 1568, .f1 = 1571, .dur = .4f, .vib_rate = 6, .vib_depth = .01f, .bright = 1.6f, .open = 1.25f, .amp = .6f});
        break;
    case VOICE_HANGUP: { /* the wreath: a soft whistle sliding down as it comes (0..0.55 s), a wooden "tok" as it lands (0.85 s), and
                            bells running up as its lights come on (2.0 s), the top one held */
        static const float bell[4] = {1318.5f, 1568.0f, 2093.0f, 2637.0f}; /* E6 G6 C7 E7 */
        add(p, (syl_t){.f0 = 1500, .f1 = 700, .dur = .55f, .gap = .3f, .vib_rate = 6, .vib_depth = .01f, .bright = 1.6f, .open = 1.3f, .amp = .7f});
        add(p, (syl_t){.f0 = 620, .f1 = 430, .dur = .06f, .gap = 1.09f, .bright = 1.5f, .open = 1, .noise = .1f});
        for (int i = 0; i < 4; i++)
            add(p, (syl_t){.f0 = bell[i], .f1 = bell[i] * 1.004f, .dur = i == 3 ? .5f : .09f, .gap = .03f, .vib_rate = i == 3 ? 6 : 0, .vib_depth = .01f,
                           .bright = 1.6f, .open = 1.25f, .amp = .75f});
        break;
    }
    case VOICE_AAH: /* "o?" up at it, "oo~" following its swing, then an admiring "a-aah~" as it lights up (2.0 s), and a ding */
        add(p, (syl_t){.f0 = base * 1.0f, .f1 = base * 1.3f, .dur = .15f, .gap = .25f, .bright = 2.4f, .open = 1.6f});
        add(p, (syl_t){.f0 = base * 1.2f, .f1 = base * 1.0f, .dur = .35f, .gap = 1.25f, .vib_rate = 3, .vib_depth = .05f, .bright = 2, .open = 1.4f});
        add(p, (syl_t){.f0 = base * 1.1f, .f1 = base * 1.25f, .dur = .1f, .gap = .03f, .bright = 2.6f, .open = 2});
        add(p, (syl_t){.f0 = base * 1.5f, .f1 = base * 1.2f, .dur = .52f, .gap = .02f, .vib_rate = 6, .vib_depth = .035f, .bright = 2.6f, .open = 2.2f, .noise = .04f});
        add(p, (syl_t){.f0 = 1568, .f1 = 1571, .dur = .26f, .vib_rate = 6, .vib_depth = .01f, .bright = 1.6f, .open = 1.25f, .amp = .6f});
        break;    case VOICE_BABBLE: babble(p, base, v.bb_kind, v.bb_v, v.bb_a); break;
    }
    /* how he sounds just now (voice_tone): his own sounds only - the interface's, the tunes and the drums keep theirs */
    bool fixed = sound == VOICE_TICK || sound == VOICE_TOCK || sound == VOICE_ALARM || sound == VOICE_RING || sound == VOICE_BIRTHDAY ||
                 sound == VOICE_FLAP || sound == VOICE_POINT || sound == VOICE_SPARK || sound == VOICE_STARDUST || sound == VOICE_BLOOM || (sound >= VOICE_SKYLIT && sound <= VOICE_ANTHEM) ||
                 (sound >= VOICE_NOTE0 && sound <= VOICE_NOTE3) || sound == VOICE_JINGLE || sound == VOICE_HANGUP;
    float sh = v.tone_semis, tp = v.tone_tempo;
    if (!fixed && (sh != 0 || tp != 1)) {
        float k = sound == VOICE_BURP || sound == VOICE_TOOT ? 1 : semi(sh); /* a burp is a throat at fixed vowels: its pace, not its pitch (raised,
                                                          its pulses drove the soft clip into a rasp) */
        for (int i = 0; i < p->n; i++) {
            syl_t *s = &p->s[i];
            s->f0 *= k, s->f1 *= k, s->dur *= tp, s->gap *= tp;
            if (s->formant > 0) s->formant *= 1 + (k - 1) * 0.5f; /* a vowel moves less than the pitch */
        }
    }
}

void voice_init(int rate)
{
    v.rate = rate, v.i = v.cur.n = 0, v.rng = 0x1234567u, v.noise = 99;
    v.warm_k = 1 - expf(-TWO_PI * 3000.0f / (float)rate); /* ~3 kHz */
}
static int pip_lvl = VOICE_STEPS, ui_lvl = VOICE_UI_DEFAULT;
static int lvl_clamp(int l) { return l < 0 ? 0 : l > VOICE_STEPS ? VOICE_STEPS : l; }
void voice_levels(int pip, int ui) { pip_lvl = lvl_clamp(pip), ui_lvl = lvl_clamp(ui); }
int voice_pip_level(void) { return pip_lvl; }
int voice_ui_level(void) { return ui_lvl; }
/* the steps about 4-5 dB apart, so each sounds a step quieter on the small speaker, the first still heard */
static const float gains[VOICE_STEPS + 1] = {0, 0.2f, 0.33f, 0.5f, 0.72f, 1};
static void (*say_hook)(void);
void voice_on_say(void (*cb)(void)) { say_hook = cb; }
void voice_say(int sound)
{
    bool ui = sound == VOICE_TICK || sound == VOICE_ALARM || sound == VOICE_RING || sound == VOICE_FLAP || sound == VOICE_POINT; /* the
                                                                       interface's (a game's too); the rest is Pip talking */
    int l = sound == VOICE_ALARM ? VOICE_STEPS : ui ? ui_lvl : pip_lvl; /* the timer's song: always as loud as it goes, so it is
                                                                            never missed; the ring follows the interface's level */
    if (sound < 0 || sound >= VOICE_COUNT || l == 0) return;
    v.pending_gain = gains[l];
    v.pending = sound;
    if (say_hook) say_hook();
}
void voice_babble(int kind, float valence, float arousal)
{
    v.bb_kind = kind < 0 || kind >= BABBLE_KINDS ? BABBLE_SAY : kind, v.bb_v = valence == valence ? valence : 0, v.bb_a = arousal == arousal ? arousal : 0; /* a NaN would poison the filters for good */
    voice_say(VOICE_BABBLE);
}
void voice_tone(float semis, float tempo)
{
    if (semis != semis) semis = 0;
    if (tempo != tempo) tempo = 1;
    v.tone_semis = semis < -3 ? -3 : semis > 3 ? 3 : semis;
    v.tone_tempo = tempo < 0.8f ? 0.8f : tempo > 1.25f ? 1.25f : tempo;
}
void voice_set_volume(float vol) { v.volume = vol < 0 ? 0 : vol > 1 ? 1 : vol; }
float voice_volume(void) { return v.volume; }
/* ---- the pad: up to ten soft sines easing in and out, two a chord, so one chord flows into the next ---- */
static struct { float f, ph, amp, to, k; } pad[10];
static bool pad_on(void)
{
    for (int i = 0; i < 10; i++) if (pad[i].amp > 0.0004f || pad[i].to > 0) return true;
    return false;
}
void voice_pad(const float *hz, int n, float level, float secs)
{
    level *= gains[pip_lvl]; /* as loud as his voice is set */
    if (level <= 0) n = 0;
    float k = 1 / (fmaxf(secs, 0.02f) * 0.35f * v.rate);
    for (int i = 0; i < 10; i++) pad[i].to = 0, pad[i].k = k;
    for (int j = 0; j < n && j < 5; j++) {
        int best = -1;
        for (int i = 0; i < 10; i++) if (fabsf(pad[i].f - hz[j]) < 0.5f && pad[i].amp > 0) best = i; /* sounding already: kept */
        for (int i = 0; i < 10 && best < 0; i++) if (pad[i].to == 0 && pad[i].amp < 0.0005f) best = i;
        for (int i = 0; i < 10 && best < 0; i++) if (pad[i].to == 0) best = i;
        if (best < 0) break;
        pad[best].f = hz[j], pad[best].to = level * (1 - 0.1f * j) / (n > 0 ? n : 1), pad[best].k = k;
    }
    if (n && say_hook) say_hook();
}
static float pad_sample(float dt)
{
    static float lfo;
    float y = 0;
    if ((lfo += dt * 0.31f) > 1) lfo -= 1;
    float sh = 0.85f + 0.15f * sin01(lfo);
    for (int i = 0; i < 10; i++) {
        if (pad[i].amp < 0.0003f && pad[i].to <= 0) { pad[i].amp = 0; continue; }
        pad[i].amp += (pad[i].to - pad[i].amp) * pad[i].k;
        pad[i].ph += pad[i].f * dt;
        pad[i].ph -= (float)(int)pad[i].ph;
        y += pad[i].amp * (sin01(pad[i].ph) + 0.22f * sin01(2 * pad[i].ph) + 0.08f * sin01(3 * pad[i].ph));
    }
    return y * sh * v.volume;
}
bool voice_busy(void) { return v.pending >= 0 || v.i < v.cur.n || pad_on(); }
void voice_hush(void) { v.pending = -1, v.hush = true, voice_pad(NULL, 0, 0, 0.1f); }
static int started;
int voice_started(void) { return started; }

const char *voice_name(int sound)
{
    static const char *const names[VOICE_COUNT] = {"poke", "happy", "sad", "growl", "oh", "love", "giggle", "nom", "burp", "yawn",
                                                   "dizzy", "derp", "hm", "censor", "alarm", "hmph", "tick", "tock", "sneeze", "ring",
                                                   "whee", "hum", "pout", "tada", "think", "aww", "peek", "phew", "laugh", "thanks",
                                                   "loop", "birthday", "rant", "snort", "brr", "lullaby", "chat", "mutter", "mumble", "tease",
                                                   "note0", "note1", "note2", "note3", "flap", "point",
                                                   "taptap", "sigh", "mmm", "ooh", "snicker", "ehe", "hup", "aha", "ugh", "kya", "hic", "toot",
                                                   "spark", "stardust", "bloom", "mmph", "boing", "eek", "pinch", "yay", "nuzzle", "oohooh", "yum",
                                                   "fanfare", "deflate", "squeak", "groggy",
                                                   "wow", "twinkle", "lala", "aria", "mwah", "smooch", "heyyou", "wink", "xylo",
                                                   "prism", "knock", "popup", "om", "bowl", "snore", "dreamsong", "melt", "coo",
                                                   "claps", "bravo", "bassline", "yeah", "whimper", "yip", "flutter", "hushed", "blub",
                                                   "bubblepop", "beepboop", "servo", "purr", "meow", "blblbl", "yoyo", "pfft", "crackup",
                                                   "crackle", "cozy", "shooting", "wish", "zap", "powerup", "gulp", "fillup", "skylit", "heart", "riser", "grand", "anthem",
                                                   "heehee", "wheeze", "rolling", "hoho", "boings", "yippee", "petals", "springsong", "lubdub",
                                                   "swoon", "rocketup", "oohaah", "medalist", "mhm", "discobeat", "groovy", "eyepop", "whoanelly",
                                                   "ideaping", "gotit", "peekshy", "bashful", "helloo", "yoohoo", "royal", "harrumph", "halosong",
                                                   "whistling", "mwahaha", "teehee", "hopla", "circus", "liftoff", "popfall", "lick", "freeze",
                                                   "pitter", "dryhum", "heave", "strong", "abracadabra", "rabbit", "dawn", "stretch", "baa",
                                                   "counting", "hiya", "shadow", "growing", "blossom", "greysigh", "drizzle", "bawl", "sobs",
                                                   "cracked", "letgo", "kettle", "grrmph", "smoulder", "roarlet", "hmff", "nyeh", "teeth",
                                                   "booo", "emptywind", "meh", "powerdown", "lowbeep", "rumble", "nocookie", "birdies", "kaput",
                                                   "lovesmenot", "fading", "sniffle", "achoolet", "popfull", "chargedup", "yank", "notfull", "powerfail", "feeble", "retch",
                                                   "jingle", "ohtree", "hangup", "aah", "babble"};
    return sound >= 0 && sound < VOICE_COUNT ? names[sound] : "";
}

static void syl_start(void) /* the next syllable: its running values, so the loop below is plain arithmetic */
{
    if (v.i >= v.cur.n) return;
    const syl_t *s = &v.cur.s[v.i];
    float rate = (float)v.rate;
    v.k = 0, v.len = (int)(s->dur * rate), v.gap = (int)(s->gap * rate);
    if (v.len < 1) v.len = 1;
    v.inv_len = 1.0f / v.len;
    v.f = s->f0, v.glide = powf(s->f1 / s->f0, v.inv_len); /* the glide: a ratio a sample */
    v.vib_ph = 0, v.vib_inc = s->vib_rate / rate, v.rough_ph = 0, v.rough_inc = 38 / rate;
    v.period_k = v.pulse_amp = 1, v.hp = v.hp2 = v.prev_src = v.prev_hp = 0;
    v.periods = 0; /* the vowel filters' state runs on from the last syllable: no click where two creaks join */
}

bool voice_render(int16_t *out, int n)
{
    int sound = __atomic_exchange_n(&v.pending, -1, __ATOMIC_ACQ_REL); /* asked for meanwhile: never lost between the two */
    if (sound >= 0) { v.gain = v.pending_gain; compose(&v.cur, sound); v.i = 0; syl_start(); started++; v.hush = false, v.fade = 1; }
    float fade_step = 0;
    bool hushed = v.hush; /* read once: a hush set after this is left for the next block, never cleared unseen */
    if (hushed && v.i < v.cur.n) fade_step = 1.0f / (HUSH_S * v.rate);
    else {
        if (hushed) v.hush = false;
        v.fade = 1;
    }
    bool loud = pad_on();
    const float dt = 1.0f / v.rate, nyq = 3.14159f / v.rate, out_gain = v.volume * v.gain * 0.6f;
    for (int k = 0; k < n; k++) {
        float y = 0;
        if (v.i < v.cur.n) {
            const syl_t *s = &v.cur.s[v.i];
            if (v.k < v.len) {
                float x = v.k * v.inv_len, open_k = sin01(x * 0.5f); /* 0 -> 1 -> 0 across the syllable */
                float f = v.f * (1 + s->vib_depth * sin01(v.vib_ph));
                v.f *= v.glide, v.vib_ph += v.vib_inc;
                v.phase += f * v.period_k * dt;
                float src;
                if (s->wave == 1) { /* creak: a rounded glottal pulse a period, each period a little off in length and strength */
                    if (v.phase >= 1) {
                        v.phase -= (float)(int)v.phase;
                        v.period_k = 1 + s->jitter * ((int32_t)next(&v.noise) / 2147483648.0f);
                        v.pulse_amp = 1 + s->shimmer * ((int32_t)next(&v.noise) / 2147483648.0f);
                        if (s->gurgle > 0) { /* the wet rattle: every other pulse weaker, and now and then one swallowed */
                            if (v.periods & 1) v.pulse_amp *= 1 - 0.55f * s->gurgle;
                            if (next(&v.noise) % 1000 < (uint32_t)(s->gurgle * 260)) v.pulse_amp *= 0.12f;
                        }
                        v.periods++;
                    }
                    float pulse = (v.phase < 0.42f ? sin01(v.phase * (0.5f / 0.42f)) : 0) * v.pulse_amp;
                    /* what the throat sends is the flow's change (the click as the folds shut), not the flow: the pulse
                       high-passed twice at ~400 Hz, so the fundamental and its first harmonics - what a tiny speaker
                       cannot play - neither eat the headroom nor drive the soft clip into a rattle */
                    v.hp = 0.864f * (v.hp + pulse - v.prev_src), v.prev_src = pulse;
                    v.hp2 = 0.864f * (v.hp2 + v.hp - v.prev_hp), v.prev_hp = v.hp;
                    src = v.hp2 * 4;
                } else {
                    v.phase -= (float)(int)v.phase;
                    src = sin01(v.phase) + 0.3f * sin01(2 * v.phase);
                }
                src += s->noise * ((int32_t)next(&v.noise) / 2147483648.0f);
                /* the mouth: a resonant low-pass whose cutoff opens mid-syllable (Chamberlin SVF); g = 2 sin(pi fc / rate)
                   by its series, exact enough under the 0.16 x rate cap */
                float fc = fminf((s->formant > 0 ? s->formant : f * s->bright) * (1 + (s->open - 1) * open_k), v.rate * 0.16f);
                float w = fc * nyq, g = 2 * w * (1 - w * w * (1.0f / 6));
                v.low += g * v.band;
                v.band += g * (src - v.low - (s->damp > 0 ? s->damp : 1.25f) * v.band);
                /* a soft envelope: eased in over 10 ms, eased out over the last third (no clicks at either end) */
                float in = fminf(v.k * dt * 100, 1), o = x > 0.66f ? (1 - x) * (1 / 0.34f) : 1;
                float env = in * in * (3 - 2 * in) * o * o * (3 - 2 * o);
                if (s->rough > 0) env *= 1 - s->rough * 0.45f * (1 + sin01(v.rough_ph)), v.rough_ph += v.rough_inc;
                float voiced = s->wave == 1 ? v.band : v.low; /* the creak through a vowel: a band, not a low-pass */
                if (s->formant2 > 0) { /* and a second resonance: an "o" or an "a", not a tube */
                    float fc2 = fminf(s->formant2 * (1 + (s->open - 1) * open_k * 0.5f), v.rate * 0.16f);
                    float w2 = fc2 * nyq, g2 = 2 * w2 * (1 - w2 * w2 * (1.0f / 6));
                    v.low2 += g2 * v.band2;
                    v.band2 += g2 * (src - v.low2 - 0.7f * v.band2);
                    voiced += 0.55f * v.band2;
                }
                y = voiced * env * s->amp * v.cur.gain;
                v.k++;
            } else if (v.k++ >= v.len + v.gap) {
                v.i++;
                syl_start();
            }
        }
        if (fade_step > 0) { /* hushed: down to nothing, then the sound is over */
            y *= v.fade;
            if ((v.fade -= fade_step) <= 0) v.i = v.cur.n, v.hush = false, v.fade = 1, fade_step = 0;
        }
        y *= out_gain;
        y += pad_sample(dt);
        v.warm += (y - v.warm) * v.warm_k; /* warmth: the fizz over ~3 kHz rolled off, the tiny speaker never shrills */
        y = v.warm;
        y = y / (1 + fabsf(y)); /* soft clip */
        out[k] = (int16_t)(y * 32000);
        if (fabsf(y) > 0.01f) loud = true;
    }
    return loud;
}
