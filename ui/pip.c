#include "pip.h"
#include "cosmos.h"
#include "face.h"
#include "grime.h"
#include "imu.h"
#include "mic.h"
#include "phone.h"
#include "sprite.h"
#include "theme.h"
#include <stddef.h>
#include <stdio.h>
#include "pet.h"

LV_FONT_DECLARE(font_pip_48) /* the "!" and "?" over his eyes, the grawlix: nine glyphs (tools/fonts.ps1) */
#include "voice.h"
#include <math.h>
#include <string.h>

#define SCR 466
#define TAP_MS 300
#define SPREAD 0.55f  /* angle of each eye from the front of the (virtual, round) head, rad */
#define REST_SPACING 237.0f /* px between the eyes when looking straight */
#define HEAD_R (REST_SPACING / (2 * 0.5227f)) /* sin(SPREAD); turning the head slides the far eye behind the near one */
#define EYE_SCALE 1.45f /* mood table sizes -> screen */

typedef struct { float w, h, roll, taper, lid, tilt, smile, pw, ph, square, heart, glint, spiral, veins, wet; } shape_t;
#define SHAPE_N (int)(sizeof(shape_t) / sizeof(float))

/* One mood = eye shape + how Pip behaves in it. Anything left out is "neutral":
   w/h/pw/ph of 0 mean the neutral size, look_rate/look_amp/blink of 0 mean 1.
   roll/tilt are for the left eye (+ = inner corner down), mirrored for the right one. */
typedef struct {
    const char *name;
    shape_t s;
    float closed;              /* 0 open .. 1 shut */
    float gx, gy;              /* where it tends to look, -1..1 */
    float head_roll;
    float lid_asym, size_asym; /* right eye: extra lid, extra size */
    float cross;               /* pupils turned inward (+) */
    uint32_t tint;             /* white of the eye, 0 = white */
    float blush;
    float look_rate, look_amp; /* glances: how often, how far */
    float slow;                /* 0..1 sluggish */
    float tremble;             /* px */
    float sway_x, sway_y;      /* slow head circles / nods, rad */
    float nod, wag;            /* answers: the head nods yes / shakes no, rad */
    float blink;               /* blink frequency multiplier */
    float wink;                /* seconds between winks of the right eye, 0 = never */
    uint8_t fx;                /* scene effect around the face */
    const char *text;          /* drawn above the eyes */
    uint32_t text_color;
} mood_def_t;

enum { FX_NONE, FX_SWEAR, FX_SNOW, FX_SWEAT, FX_RAGE };

#define S(...) .s = {__VA_ARGS__}
static const mood_def_t moods[PIP_MOOD_COUNT] = {
    [PIP_NEUTRAL]     = {.name = "neutral"},
    [PIP_HAPPY]       = {"happy", S(.w = 118, .h = 124, .roll = -.06f, .taper = -.1f, .smile = .42f, .pw = .36f, .ph = .38f), .gy = -.8f, .head_roll = .12f},
    [PIP_SAD]         = {"sad", S(.w = 112, .h = 126, .roll = -.14f, .taper = .15f, .lid = .22f, .tilt = -.35f, .pw = .52f, .ph = .56f), .gy = .55f, .head_roll = -.1f, .slow = .3f},
    [PIP_ANGRY]       = {"angry", S(.w = 114, .h = 114, .roll = .16f, .taper = .3f, .lid = .3f, .tilt = .45f, .pw = .2f, .ph = .4f), .gy = .1f},
    [PIP_SURPRISED]   = {"surprised", S(.w = 124, .h = 160, .taper = -.05f, .pw = .24f, .ph = .26f), .gy = -.1f, .look_rate = 2},
    [PIP_SLEEPY]      = {"sleepy", S(.w = 122, .h = 124, .lid = .5f, .tilt = -.05f, .pw = .44f, .ph = .44f), .gy = .45f, .head_roll = .08f, .slow = .5f, .blink = 1.6f},
    [PIP_ASLEEP]      = {"asleep", S(.w = 122, .h = 124, .pw = .44f, .ph = .44f), .closed = 1, .gy = .4f, .head_roll = .1f},
    [PIP_SKEPTICAL]   = {"skeptical", S(.w = 128, .h = 76, .roll = .04f, .lid = .08f, .pw = .52f, .ph = .3f), .lid_asym = .28f, .head_roll = -.06f},
    [PIP_CURIOUS]     = {"curious", S(.w = 124, .h = 148, .taper = -.05f, .pw = .5f, .ph = .54f), .lid_asym = -.1f, .gy = -.1f, .head_roll = .22f},
    [PIP_LOVE]        = {"love", S(.w = 120, .h = 128, .taper = -.08f, .smile = .2f, .pw = .38f, .ph = .38f, .heart = 1), /* a heart's edge is dear: kept middling */ .gy = -.15f, .head_roll = .1f, .blush = .6f},
    [PIP_DIZZY]       = {"dizzy", S(.w = 120, .h = 128, .lid = .12f, .pw = .34f, .ph = .34f), .lid_asym = .1f},
    [PIP_OUCH]        = {"ouch", S(.w = 126, .h = 26, .roll = .3f, .pw = .2f, .ph = .2f)},

    [PIP_STONED]      = {"stoned", S(.w = 124, .h = 122, .roll = -.04f, .lid = .42f, .tilt = -.04f, .smile = .24f, .pw = .46f, .ph = .48f, .veins = 1),
                         .gy = .1f, .head_roll = .1f, .tint = 0xFFE4E0, .look_amp = .5f, .slow = .75f, .sway_x = .06f, .sway_y = .04f, .blink = .6f},
    [PIP_PINKEYE]     = {"pinkeye", S(.w = 118, .h = 128, .lid = .25f, .tilt = -.1f), .gy = .2f, .lid_asym = .4f, .size_asym = .08f, .tint = 0xFF8C88, .slow = .2f, .blink = 2.5f},
    [PIP_NAUSEOUS]    = {"nauseous", S(.w = 116, .h = 124, .roll = -.06f, .lid = .3f, .tilt = -.2f, .pw = .38f, .ph = .4f), .gy = .2f, .lid_asym = .12f,
                         .tint = 0xD2F2B4, .slow = .4f, .sway_x = .12f, .sway_y = .08f},
    [PIP_SICK]        = {"sick", S(.w = 112, .h = 118, .lid = .38f, .tilt = -.15f, .pw = .36f, .ph = .38f), .gy = .35f, .tint = 0xC6D0DA, .slow = .5f, .tremble = 1, .blink = 1.5f},
    [PIP_TIRED]       = {"tired", S(.w = 120, .h = 120, .lid = .45f, .tilt = -.12f, .pw = .42f, .ph = .44f), .gy = .3f, .look_amp = .6f, .slow = .45f, .blink = 1.8f},
    [PIP_EXHAUSTED]   = {"exhausted", S(.w = 118, .h = 112, .roll = -.06f, .lid = .62f, .tilt = -.15f), .gy = .45f, .head_roll = .12f, .slow = .7f, .sway_y = .05f, .blink = 2},

    [PIP_ANNOYED]     = {"annoyed", S(.w = 116, .h = 124, .roll = .06f, .lid = .2f, .tilt = .22f, .pw = .36f, .ph = .4f), .gx = .5f, .look_rate = .5f, .look_amp = .3f},
    [PIP_FURIOUS]     = {"furious", S(.w = 112, .h = 110, .roll = .2f, .taper = .35f, .lid = .36f, .tilt = .6f, .pw = .16f, .ph = .34f), .tint = 0xFFD6CC, .look_amp = .3f, .tremble = 1.2f},
    [PIP_RAGE]        = {"rage", S(.w = 110, .h = 106, .roll = .24f, .taper = .4f, .lid = .38f, .tilt = .7f, .pw = .14f, .ph = .3f), .tint = 0xFF3A28,
                         .look_rate = .3f, .look_amp = .2f, .tremble = 3},
    [PIP_SWEARING]    = {"swearing", S(.w = 118, .h = 118, .roll = .16f, .taper = .28f, .lid = .3f, .tilt = .5f, .pw = .2f, .ph = .38f), .look_rate = .5f,
                         .tremble = 1.5f, .fx = FX_SWEAR},
    /* berserk: the same red as rage, but the pupils pinpoints of two sizes on a slow spiral, darting all over, veins,
       a hard tremble, the head jolted about (update) and a hail of grawlix (FX_RAGE) */
    [PIP_BERSERK]     = {"berserk", S(.w = 120, .h = 134, .roll = .22f, .taper = .3f, .lid = .14f, .tilt = .62f, .pw = .16f, .ph = .18f,
                                      .spiral = .35f), /* no veins: they cost every pixel of the eye (~4 fps on the board) */
                         .size_asym = .24f, .lid_asym = -.16f, .tint = 0xFF2A1A, .look_rate = 4, .look_amp = .7f, .tremble = 4,
                         .sway_x = .06f, .fx = FX_RAGE},
    [PIP_INDIGNANT]   = {"indignant", S(.w = 122, .h = 146, .roll = .08f, .lid = .12f, .tilt = .35f, .pw = .26f, .ph = .3f), .gy = -.2f, .head_roll = -.08f, .look_rate = 1.5f},
    [PIP_BELLIGERENT] = {"belligerent", S(.w = 116, .h = 112, .roll = .18f, .taper = .25f, .lid = .34f, .tilt = .5f, .pw = .3f, .ph = .42f), .gy = -.5f,
                         .look_rate = .4f, .look_amp = .2f},
    [PIP_HURT]        = {"hurt", S(.w = 114, .h = 124, .roll = -.12f, .lid = .2f, .tilt = -.32f, .pw = .5f, .ph = .54f, .glint = 1, .wet = 1),
                         .gx = -.3f, .gy = .3f, .head_roll = -.12f, .look_amp = .3f, .slow = .3f, .tremble = .4f},

    [PIP_DISGUSTED]   = {"disgusted", S(.w = 118, .h = 104, .roll = -.1f, .lid = .3f, .tilt = -.1f, .smile = .25f, .pw = .32f, .ph = .34f), .gx = -.8f, .gy = .35f,
                         .head_roll = -.15f, .lid_asym = .2f, .tint = 0xE8F4D4},
    [PIP_SUSPICIOUS]  = {"suspicious", S(.w = 124, .h = 86, .lid = .44f, .tilt = .08f, .pw = .32f, .ph = .3f), .head_roll = .08f, .lid_asym = .08f,
                         .look_rate = .25f, .look_amp = .12f, .slow = .3f}, /* stares right at you */
    [PIP_CYNICAL]     = {"cynical", S(.w = 120, .h = 108, .lid = .42f, .tilt = -.08f, .pw = .34f, .ph = .36f), .gx = .3f, .gy = -.85f, .head_roll = .1f, .lid_asym = .1f,
                         .look_rate = .3f},
    [PIP_AFRAID]      = {"afraid", S(.w = 126, .h = 160, .roll = -.1f, .taper = -.1f, .lid = .08f, .tilt = -.3f, .pw = .18f, .ph = .2f), .gy = .1f,
                         .look_rate = 2.5f, .look_amp = .5f, .tremble = 2.2f},
    [PIP_NERVOUS]     = {"nervous", S(.w = 118, .h = 140, .lid = .06f, .tilt = -.15f, .pw = .3f, .ph = .32f), .look_rate = 4, .look_amp = .7f, .tremble = .8f, .blink = 2.2f},
    [PIP_DESPONDENT]  = {"despondent", S(.w = 108, .h = 114, .roll = -.12f, .lid = .34f, .tilt = -.4f, .pw = .42f, .ph = .46f), .gy = .7f, .head_roll = -.1f,
                         .look_rate = .4f, .look_amp = .4f, .slow = .6f},
    [PIP_LONELY]      = {"lonely", S(.w = 100, .h = 112, .roll = -.08f, .lid = .18f, .tilt = -.25f, .pw = .46f, .ph = .5f), .gx = -.6f, .gy = .5f, .tint = 0xD4DCE8,
                         .look_rate = .4f, .slow = .5f},
    [PIP_GLOOMY]      = {"gloomy", S(.w = 118, .h = 104, .lid = .5f, .tilt = .02f, .pw = .36f, .ph = .38f), .gy = .45f, .tint = 0x9EA3AA, .look_rate = .3f, .slow = .5f},
    [PIP_DULL]        = {"dull", S(.w = 122, .h = 126, .lid = .35f, .pw = .3f, .ph = .32f), .head_roll = .15f, .lid_asym = .1f, .cross = .7f, .look_rate = .3f,
                         .slow = .6f, .blink = .5f},
    [PIP_SHY]         = {"shy", S(.w = 112, .h = 124, .roll = -.06f, .lid = .12f, .tilt = -.15f, .smile = .15f, .pw = .44f, .ph = .46f), .gx = -.7f, .gy = .55f,
                         .head_roll = -.15f, .blush = .9f, .look_rate = .6f, .look_amp = .4f},
    [PIP_SMUG]        = {"smug", S(.w = 124, .h = 104, .lid = .38f, .tilt = -.06f, .smile = .3f, .pw = .36f, .ph = .38f), .gx = .35f, .gy = -.1f, .head_roll = .14f,
                         .lid_asym = .08f, .look_rate = .4f, .slow = .2f},
    [PIP_BORED]       = {"bored", S(.w = 122, .h = 114, .lid = .45f, .tilt = -.04f, .pw = .34f, .ph = .36f), .gx = .5f, .gy = -.5f, .look_rate = .3f, .slow = .5f, .blink = .7f},
    [PIP_SHOCKED]     = {"shocked", S(.w = 138, .h = 172, .taper = -.08f, .pw = .14f, .ph = .15f), .look_rate = .02f, .look_amp = .05f, .tremble = 1, .blink = .05f,
                         .text = "!", .text_color = 0xFFFFFF},
    [PIP_DISMAYED]    = {"dismayed", S(.w = 118, .h = 146, .roll = -.12f, .lid = .12f, .tilt = -.4f, .pw = .3f, .ph = .32f), .gy = .1f, .look_rate = 2.5f,
                         .look_amp = .6f, .tremble = .6f},
    [PIP_PUZZLED]     = {"puzzled", S(.w = 120, .h = 132, .pw = .36f, .ph = .38f), .gx = .3f, .gy = -.3f, .head_roll = -.22f, .lid_asym = .3f, .size_asym = -.15f,
                         .look_rate = .5f, .text = "?", .text_color = 0xFFFFFF},

    [PIP_AMAZED]      = {"amazed", S(.w = 132, .h = 160, .taper = -.06f, .pw = .48f, .ph = .5f, .glint = 1), .gy = -.25f, .look_rate = .5f, .look_amp = .4f},
    /* one brow down, the other eye wide, chin up, looking right at you, and a wink now and then */
    [PIP_CHEEKY]      = {"cheeky", S(.w = 120, .h = 126, .lid = .22f, .tilt = .35f, .smile = .12f, .pw = .36f, .ph = .4f), .gy = -.25f, .head_roll = -.18f,
                         .lid_asym = -.3f, .size_asym = .1f, .look_rate = 1.2f, .look_amp = .3f, .wink = 2.8f},
    [PIP_CONTENT]     = {"content", S(.w = 124, .h = 124, .roll = -.06f, .taper = -.1f, .smile = .72f, .pw = .36f, .ph = .38f), .gy = -.3f, .head_roll = .1f, .look_rate = .3f, .slow = .4f, .blink = .6f},
    [PIP_COMFY]       = {"comfy", S(.w = 124, .h = 118, .lid = .38f, .smile = .2f, .pw = .4f, .ph = .42f), .gy = .1f, .look_rate = .3f, .slow = .6f, .blink = .5f},

    /* looking right at you, the head nods / shakes */
    [PIP_YES]         = {"yes", S(.w = 120, .h = 130, .taper = -.05f, .smile = .25f, .pw = .42f, .ph = .44f, .glint = .4f), .head_roll = .06f,
                         .look_rate = .1f, .look_amp = .1f, .nod = .13f},
    [PIP_NO]          = {"no", S(.w = 118, .h = 120, .roll = .04f, .lid = .2f, .tilt = .2f, .pw = .36f, .ph = .4f), .look_rate = .1f, .look_amp = .1f,
                         .wag = .2f},

    /* rare cards: only the springs, tints and the renderer's own features (no new per-pixel work); he sneezes
       (pip.c sneeze), snowflakes and a sweat drop are a handful of small plain objects (effects()) */
    [PIP_ALLERGIC]    = {"allergic", S(.w = 120, .h = 112, .roll = -.04f, .lid = .3f, .tilt = -.12f, .pw = .4f, .ph = .42f, .wet = 1),
                         .gy = .15f, .tint = 0xFFD9D2, .blush = .35f, .slow = .2f, .blink = 3},
    [PIP_FROZEN]      = {"frozen", S(.w = 112, .h = 118, .lid = .26f, .tilt = -.1f, .pw = .3f, .ph = .32f, .glint = .5f), .gy = .1f,
                         .tint = 0xC8E8FF, .look_rate = .4f, .look_amp = .5f, .slow = .3f, .tremble = 1.6f, .fx = FX_SNOW},
    [PIP_OVERHEATED]  = {"overheated", S(.w = 122, .h = 112, .roll = -.06f, .lid = .44f, .tilt = -.16f, .pw = .38f, .ph = .4f),
                         .gy = .3f, .head_roll = .1f, .tint = 0xFFCBA8, .blush = .8f, .slow = .6f, .sway_x = .05f, .sway_y = .04f,
                         .blink = 1.4f, .fx = FX_SWEAT},
    [PIP_SMITTEN]     = {"smitten", S(.w = 126, .h = 136, .taper = -.08f, .smile = .15f, .pw = .46f, .ph = .46f, .heart = 1),
                         .gy = -.25f, .head_roll = .14f, .tint = 0xFFF0F5, .blush = 1, .look_rate = .3f, .slow = .35f, .sway_x = .06f},
    [PIP_HYPNOTIZED]  = {"hypnotized", S(.w = 120, .h = 132, .pw = .42f, .ph = .42f, .spiral = 1), .look_rate = .02f, .look_amp = .05f,
                         .slow = .5f, .sway_x = .08f, .sway_y = .08f, .blink = .1f},
    [PIP_HYPER]       = {"hyper", S(.w = 130, .h = 156, .taper = -.06f, .pw = .16f, .ph = .17f, .glint = .6f), .gy = -.1f,
                         .look_rate = 5, .look_amp = 1, .tremble = 1.2f, .blink = .3f},
    [PIP_ZOMBIE]      = {"zombie", S(.w = 118, .h = 116, .roll = -.06f, .lid = .4f, .tilt = -.06f, .pw = .34f, .ph = .36f, .veins = .7f),
                         .gy = .15f, .head_roll = .12f, .lid_asym = .1f, .size_asym = -.08f, .cross = .25f, .tint = 0xC9D6C2,
                         .look_amp = .4f, .slow = .85f, .sway_x = .05f, .sway_y = .06f, .blink = .5f},

    /* feelings brought out by something you do (the interactions in update(), pip_imu and touch_cb): the same springs,
       tints, blush and renderer features as the rest, nothing new per pixel; hops and shakes come from the squash spring */
    [PIP_EXCITED]     = {"excited", S(.w = 128, .h = 152, .taper = -.08f, .smile = .22f, .pw = .4f, .ph = .44f, .glint = 1), .gy = -.35f,
                         .head_roll = .1f, .look_rate = 3, .look_amp = .6f, .tremble = .5f, .blink = .4f},     /* + a hop every third of a second */
    [PIP_DREAMY]      = {"dreamy", S(.w = 124, .h = 118, .roll = -.04f, .lid = .4f, .tilt = -.08f, .smile = .22f, .pw = .44f, .ph = .46f, .glint = .5f),
                         .gx = .55f, .gy = -.6f, .head_roll = .16f, .blush = .3f, .look_rate = .2f, .look_amp = .3f, .slow = .7f,
                         .sway_x = .05f, .sway_y = .03f, .blink = .5f},                                       /* eyes up and away, swaying */
    [PIP_POUTY]       = {"pouty", S(.w = 112, .h = 112, .roll = .06f, .lid = .26f, .tilt = .1f, .pw = .38f, .ph = .42f), .gx = -.8f, .gy = .15f,
                         .head_roll = -.2f, .lid_asym = .12f, .size_asym = -.06f, .blush = .45f, .look_rate = .3f, .look_amp = .2f}, /* cheeks puffed, turned away */
    [PIP_PROUD]       = {"proud", S(.w = 124, .h = 108, .lid = .3f, .tilt = -.04f, .smile = .35f, .pw = .38f, .ph = .4f, .glint = .8f), .gy = -.7f,
                         .head_roll = -.06f, .look_rate = .3f, .slow = .2f},                                   /* chin up, pleased with himself */
    [PIP_FOCUSED]     = {"focused", S(.w = 126, .h = 100, .lid = .3f, .tilt = .03f, .pw = .28f, .ph = .32f, .glint = .4f), .look_amp = .3f, .blink = .3f}, /* narrowed, on it */
    [PIP_TOUCHED]     = {"touched", S(.w = 122, .h = 132, .roll = -.08f, .taper = -.06f, .tilt = -.2f, .smile = .18f, .pw = .5f, .ph = .54f, .glint = 1, .wet = 1),
                         .gy = -.15f, .head_roll = .14f, .blush = .8f, .look_rate = .3f, .slow = .3f, .tremble = .3f}, /* moved to tears */
    [PIP_PLAYFUL]     = {"playful", S(.w = 122, .h = 134, .taper = -.06f, .smile = .2f, .pw = .42f, .ph = .46f, .glint = .6f), .gy = -.2f,
                         .head_roll = -.2f, .lid_asym = -.15f, .size_asym = .08f, .look_rate = 2, .look_amp = .8f, .wink = 1.6f},
    [PIP_RELIEVED]    = {"relieved", S(.w = 122, .h = 118, .roll = -.04f, .lid = .32f, .tilt = -.08f, .smile = .48f, .pw = .4f, .ph = .42f),
                         .gy = .25f, .head_roll = .14f, .slow = .5f, .sway_y = .03f, .blink = .5f, .fx = FX_SWEAT}, /* a drop wiped off */
    [PIP_LAUGHING]    = {"laughing", S(.w = 126, .h = 112, .taper = -.12f, .smile = .95f, .pw = .36f, .ph = .38f), .gy = -.4f, .head_roll = .1f,
                         .blush = .4f, .look_rate = .3f},                                                       /* ^ ^, shaking with it */
    /* the beat itself is in update(): a hop, a nod and the eyes to the other side on each one */
    [PIP_DANCING]     = {"dancing", S(.w = 124, .h = 128, .taper = -.08f, .smile = .38f, .pw = .4f, .ph = .42f, .glint = .7f), .gy = -.2f,
                         .blush = .2f, .look_rate = .1f, .look_amp = .2f, .blink = .5f},
    /* turned over and over: giddy, the pupils half a spiral, a big grin, the head going round; a hop on every turn (pip_imu) */
    [PIP_TUMBLING]    = {"tumbling", S(.w = 124, .h = 132, .taper = -.06f, .smile = .55f, .pw = .44f, .ph = .46f, .spiral = .55f, .glint = .5f),
                         .gy = -.25f, .head_roll = .18f, .cross = .1f, .blush = .35f, .look_rate = .2f, .look_amp = .2f,
                         .sway_x = .1f, .sway_y = .07f, .blink = .6f},
    [PIP_GRATEFUL]    = {"grateful", S(.w = 120, .h = 126, .taper = -.08f, .smile = .55f, .pw = .44f, .ph = .46f, .glint = .6f), .gy = -.1f,
                         .head_roll = .08f, .blush = .5f, .look_rate = .2f, .look_amp = .1f, .nod = .08f},     /* a little bow of thanks */
    /* the spiral's own two (pip.c spiral_*): the gaze is set there, the shapes are plain ones */
    [PIP_WHIRLED]     = {"whirled", S(.w = 130, .h = 146, .taper = -.05f, .smile = .18f, .pw = .3f, .ph = .32f, .glint = .8f),
                         .look_rate = .2f, .look_amp = .1f, .blink = .15f},                                /* round eyes riding the loop */
    [PIP_WOOZY]       = {"woozy", S(.w = 120, .h = 126, .roll = -.03f, .lid = .24f, .tilt = -.05f, .smile = .08f, .pw = .32f, .ph = .34f),
                         .head_roll = .06f, .lid_asym = .1f, .size_asym = .12f, .look_rate = .2f, .look_amp = .1f, .slow = .5f,
                         .sway_x = .1f, .sway_y = .07f, .blink = .5f},                                     /* unwinding, out of step */

    /* everyday faces (moods.h): the day's palettes, his own moments, answers to touch */
    [PIP_IMPATIENT]   = {"impatient", S(.w = 120, .h = 134, .lid = .04f, .tilt = .1f, .pw = .36f, .ph = .38f), .gy = -.15f,
                         .look_rate = 2.6f, .look_amp = .4f, .nod = .045f, .blink = 1.4f},                  /* a tapping foot */
    [PIP_WISTFUL]     = {"wistful", S(.w = 118, .h = 126, .roll = -.1f, .taper = .08f, .lid = .2f, .tilt = -.34f, .pw = .44f, .ph = .48f, .glint = .7f),
                         .gx = -.55f, .gy = -.55f, .head_roll = -.1f, .look_rate = .2f, .look_amp = .2f, .slow = .6f, .sway_x = .03f, .blink = .7f},
    [PIP_BLISSFUL]    = {"blissful", S(.w = 128, .h = 118, .roll = -.06f, .taper = -.1f, .smile = .9f, .pw = .36f, .ph = .38f), .gy = -.25f,
                         .head_roll = .14f, .blush = .45f, .look_rate = .15f, .slow = .8f, .sway_y = .045f, .blink = .3f},
    /* adoring: shining pupils, kept middling and no blush - a glint on a big pupil and a blush are dear on the board (at .52
       with a blush ~13 fps, as amazed; so ~20, as love) */
    [PIP_ADORING]     = {"adoring", S(.w = 120, .h = 132, .taper = -.08f, .smile = .12f, .pw = .42f, .ph = .46f, .glint = .7f), .gy = -.05f,
                         .head_roll = .12f, .look_rate = .15f, .look_amp = .1f, .slow = .3f, .blink = .6f},
    [PIP_SLY]         = {"sly", S(.w = 126, .h = 88, .roll = .06f, .lid = .36f, .tilt = .18f, .smile = .3f, .pw = .34f, .ph = .36f), .gx = .75f, .gy = .05f,
                         .head_roll = -.1f, .lid_asym = .12f, .look_rate = .5f, .look_amp = .25f},
    [PIP_SHEEPISH]    = {"sheepish", S(.w = 108, .h = 116, .roll = -.1f, .lid = .1f, .tilt = -.32f, .smile = .1f, .pw = .44f, .ph = .46f), .gx = .55f,
                         .gy = .6f, .head_roll = -.14f, .blush = .55f, .look_rate = .9f, .look_amp = .3f, .slow = .2f, .blink = 1.6f},
    [PIP_DETERMINED]  = {"determined", S(.w = 118, .h = 108, .roll = .08f, .taper = .15f, .lid = .34f, .tilt = .22f, .pw = .3f, .ph = .36f),
                         .look_rate = .2f, .look_amp = .12f, .tremble = .35f},
    [PIP_EUREKA]      = {"eureka", S(.w = 124, .h = 150, .taper = -.08f, .pw = .3f, .ph = .32f), .gy = -.6f, /* no glint: a flash, the "!" says it */ .look_rate = .05f,
                         .look_amp = .05f, .blink = .05f, .text = "!", .text_color = 0xFFD60A},
    [PIP_EYEROLL]     = {"eyeroll", S(.w = 120, .h = 112, .lid = .32f, .tilt = -.04f, .pw = .34f, .ph = .36f), .head_roll = .08f,
                         .look_rate = .01f, .look_amp = .01f, .slow = .2f, .blink = .2f},                   /* the gaze: update() */
    [PIP_FLUSTERED]   = {"flustered", S(.w = 114, .h = 128, .roll = -.06f, .lid = .06f, .tilt = -.18f, .smile = .18f, .pw = .4f, .ph = .42f),
                         .gx = -.2f, .gy = .35f, .head_roll = -.1f, .blush = 1, .look_rate = 3, .look_amp = .45f, .tremble = .3f, .blink = 3.5f},

    /* newer answers to what you do (moods.h): springs, tints and the renderer's features only; pupils middling and a glint
       or a blush, never both on a big pupil (what costs on the board) */
    [PIP_SQUISHED]    = {"squished", S(.w = 140, .h = 54, .taper = -.04f, .pw = .26f, .ph = .5f), .cross = .15f, .look_rate = .2f, .look_amp = .2f,
                         .tremble = .9f, .blink = .3f},                                                    /* flat under a palm, squirming */
    [PIP_STRETCHED]   = {"stretched", S(.w = 132, .h = 96, .taper = -.06f, .smile = .1f, .pw = .3f, .ph = .36f), .cross = -.7f,
                         .look_rate = .05f, .look_amp = .05f, .tremble = .4f, .blink = .2f},                  /* pulled wide, pupils out */
    [PIP_OVERWHELMED] = {"overwhelmed", S(.w = 124, .h = 150, .roll = -.08f, .taper = -.08f, .lid = .04f, .tilt = -.25f, .pw = .18f, .ph = .2f),
                         .lid_asym = -.1f, .size_asym = .14f, .look_rate = 6, .look_amp = .9f, .tremble = 1, .blink = 2, .fx = FX_SWEAT},
    [PIP_PINCHED]     = {"pinched", S(.w = 106, .h = 124, .roll = .1f, .taper = .06f, .lid = .14f, .tilt = .18f, .pw = .32f, .ph = .34f),
                         .cross = .65f, .look_rate = .05f, .look_amp = .05f, .tremble = .6f, .blink = .2f},  /* squeezed to his nose, crossed */
    [PIP_BEAMING]     = {"beaming", S(.w = 126, .h = 132, .taper = -.1f, .smile = .6f, .pw = .34f, .ph = .36f, .glint = .3f), .gy = -.4f,
                         .head_roll = .08f, .look_rate = .4f, .look_amp = .3f, .blink = .5f},               /* + a bounce (react_fx) */
    [PIP_CUDDLY]      = {"cuddly", S(.w = 120, .h = 110, .roll = -.1f, .taper = -.06f, .lid = .3f, .tilt = -.1f, .smile = .35f, .pw = .4f, .ph = .42f),
                         .gx = -.3f, .gy = .2f, .head_roll = .28f, .blush = .5f, .look_rate = .15f, .look_amp = .2f, .slow = .7f,
                         .sway_x = .04f, .blink = .5f},                                                      /* leaning into the hand */
    [PIP_EAGER]       = {"eager", S(.w = 118, .h = 134, .taper = -.08f, .pw = .32f, .ph = .36f), .gy = -.9f,
                         .look_rate = .1f, .look_amp = .1f, .nod = .07f, .tremble = .4f, .blink = .4f},     /* where is it? */
    [PIP_YUMMY]       = {"yummy", S(.w = 124, .h = 112, .taper = -.1f, .smile = .8f, .pw = .36f, .ph = .38f), .gy = -.2f, .head_roll = .1f,
                         .blush = .3f, .look_rate = .15f, .slow = .3f, .nod = .05f, .blink = .3f},          /* munching */
    [PIP_TRIUMPHANT]  = {"triumphant", S(.w = 126, .h = 140, .roll = -.04f, .taper = -.08f, .smile = .3f, .pw = .34f, .ph = .38f, .glint = .6f),
                         .gy = -.85f, .head_roll = -.12f, .look_rate = .2f, .look_amp = .2f, .tremble = .3f, .sway_x = .05f},
    [PIP_DEFLATED]    = {"deflated", S(.w = 104, .h = 84, .roll = -.12f, .taper = .1f, .lid = .3f, .tilt = -.3f, .pw = .4f, .ph = .44f),
                         .gy = .85f, .head_roll = -.18f, .look_rate = .1f, .look_amp = .2f, .slow = .8f, .blink = .6f},
    [PIP_SPARKLING]   = {"sparkling", S(.w = 124, .h = 134, .taper = -.08f, .smile = .3f, .pw = .32f, .ph = .34f, .glint = .45f), .gy = -.3f,
                         .head_roll = .12f, .tint = 0xF2FAFF, .look_rate = .6f, .look_amp = .4f, .blink = .4f},
    [PIP_GROGGY]      = {"groggy", S(.w = 120, .h = 118, .roll = -.04f, .lid = .55f, .tilt = -.08f, .pw = .42f, .ph = .44f), .gy = .3f,
                         .head_roll = .08f, .lid_asym = .4f, .size_asym = -.1f, .look_rate = .2f, .look_amp = .4f, .slow = .85f,
                         .sway_x = .03f, .sway_y = .05f, .blink = 1.2f},                                     /* one eye still asleep */

    /* the little scenes with pictures (moods.h): what moves is show_face's; middling pupils, small glints - none of these eyes
       costs more than a neutral one by much (pixels counted in pipsim: 0.1..1.15 of it) */
    [PIP_STARSTRUCK]  = {"starstruck", S(.w = 124, .h = 142, .taper = -.08f, .smile = .16f, .pw = .05f, .ph = .05f),
                         .gy = -.1f, .head_roll = .06f, .look_rate = .3f, .look_amp = .25f, .blink = .3f},
    [PIP_SINGING]     = {"singing", S(.w = 124, .h = 114, .roll = -.04f, .taper = -.1f, .smile = .86f, .pw = .34f, .ph = .36f),
                         .gy = -.3f, .blush = .25f, .look_rate = .1f, .look_amp = .1f, .blink = .2f},
    [PIP_KISS]        = {"kiss", S(.w = 118, .h = 124, .roll = -.04f, .taper = -.08f, .lid = .24f, .tilt = -.08f, .smile = .25f, .pw = .36f, .ph = .4f),
                         .gy = -.05f, .head_roll = .12f, .blush = .6f, .look_rate = .1f, .look_amp = .1f, .blink = .3f},
    [PIP_FLIRTY]      = {"flirty", S(.w = 120, .h = 120, .roll = -.02f, .taper = -.06f, .lid = .3f, .tilt = -.12f, .smile = .24f, .pw = .36f, .ph = .4f, .glint = .4f),
                         .head_roll = .2f, .look_rate = .1f, .look_amp = .1f, .blink = .2f},
    [PIP_RAINBOW]     = {"rainbow", S(.w = 110, .h = 120, .taper = -.08f, .smile = .4f, .pw = .34f, .ph = .37f, .glint = .3f),
                         .gy = -.25f, .look_rate = .2f, .look_amp = .2f, .blink = .5f},
    [PIP_PEEKING]     = {"peeking", S(.w = 120, .h = 130, .taper = -.06f, .pw = .34f, .ph = .37f), .look_rate = .1f, .look_amp = .1f, .blink = .4f},
    [PIP_ZEN]         = {"zen", S(.w = 122, .h = 96, .roll = -.02f, .lid = .8f, .pw = .2f, .ph = .2f),
                         .gy = -1, .tint = 0xE9E0FF, .look_rate = .02f, .look_amp = .01f, .slow = .6f, .blink = .01f},
    [PIP_SWEETDREAM]  = {"sweetdream", S(.w = 118, .h = 104, .roll = -.08f, .lid = .84f, .pw = .2f, .ph = .2f),
                         .gy = -1, .head_roll = -.14f, .tint = 0xDCE8FF, .blush = .35f, .look_rate = .02f, .look_amp = .01f, .slow = .7f, .blink = .01f},
    [PIP_MELTING]     = {"melting", S(.w = 120, .h = 130, .taper = -.08f, .smile = .15f, .pw = .38f, .ph = .42f),
                         .gy = .05f, .blush = .3f, .look_rate = .1f, .look_amp = .1f, .slow = .4f, .blink = .3f},
    [PIP_APPLAUSE]    = {"applause", S(.w = 108, .h = 126, .taper = -.06f, .smile = .48f, .pw = .34f, .ph = .38f),
                         .gy = -.3f, .look_rate = .1f, .look_amp = .1f, .blink = .2f},
    [PIP_COOL]        = {"cool", S(.w = 134, .h = 132, .roll = -.1f, .taper = .06f, .lid = .4f, .tilt = -.1f, .pw = .1f, .ph = .2f, .square = .6f),
                         .gx = -.35f, .gy = .1f, .tint = 0x2B3F9E, .look_rate = .02f, .look_amp = .01f, .blink = .01f},
    [PIP_PUPPY]       = {"puppy", S(.w = 116, .h = 130, .roll = -.14f, .lid = .12f, .tilt = -.3f, .smile = .08f, .pw = .44f, .ph = .48f, .glint = .7f),
                         .gy = -.3f, .look_rate = .1f, .look_amp = .1f, .slow = .1f, .blink = .6f},
    [PIP_BUTTERFLY]   = {"butterfly", S(.w = 116, .h = 130, .taper = -.06f, .pw = .32f, .ph = .35f, .glint = .3f),
                         .look_rate = .1f, .look_amp = .1f, .blink = .4f},
    [PIP_BUBBLES]     = {"bubbles", S(.w = 118, .h = 134, .taper = -.06f, .smile = .16f, .pw = .34f, .ph = .37f, .glint = .3f),
                         .look_rate = .1f, .look_amp = .1f, .blink = .4f},
    [PIP_ROBOT]       = {"robot", S(.w = 116, .h = 118, .lid = .1f, .smile = .1f, .pw = .34f, .ph = .34f, .square = 1),
                         .tint = 0x8FEFE2, .look_rate = .1f, .look_amp = .1f, .blink = .01f},
    [PIP_KITTY]       = {"kitty", S(.w = 120, .h = 116, .roll = .12f, .taper = .08f, .lid = .14f, .tilt = .14f, .smile = .12f, .pw = .13f, .ph = .56f),
                         .tint = 0xE9F59A, .look_rate = .3f, .look_amp = .3f, .slow = .2f, .tremble = .5f, .blink = .05f},
    [PIP_SILLY]       = {"silly", S(.w = 118, .h = 132, .taper = -.06f, .smile = .34f, .pw = .3f, .ph = .32f),
                         .gy = -.1f, .look_rate = .1f, .look_amp = .1f, .blink = .3f},
    [PIP_SNICKER]     = {"snicker", S(.w = 122, .h = 118, .taper = -.08f, .lid = .2f, .tilt = -.04f, .smile = .3f, .pw = .34f, .ph = .36f),
                         .gx = .5f, .gy = -.1f, .head_roll = .08f, .look_rate = .2f, .look_amp = .1f, .blink = .2f},
    [PIP_CAMPFIRE]    = {"campfire", S(.w = 116, .h = 116, .roll = -.04f, .lid = .34f, .tilt = -.08f, .smile = .26f, .pw = .38f, .ph = .42f, .glint = .3f),
                         .gy = .5f, .head_roll = .06f, .look_rate = .2f, .look_amp = .15f, .slow = .5f, .blink = .5f},
    [PIP_STARGAZING]  = {"stargazing", S(.w = 116, .h = 128, .taper = -.06f, .smile = .06f, .pw = .34f, .ph = .37f, .glint = .5f),
                         .tint = 0xE6EEFF, .look_rate = .1f, .look_amp = .1f, .slow = .3f, .blink = .4f},
    [PIP_ZAPPED]      = {"zapped", S(.w = 118, .h = 132, .taper = -.06f, .pw = .34f, .ph = .37f), .look_rate = .1f, .look_amp = .1f, .blink = .05f},
    [PIP_SIPPING]     = {"sipping", S(.w = 116, .h = 116, .roll = -.04f, .taper = -.06f, .lid = .34f, .smile = .2f, .pw = .34f, .ph = .38f),
                         .gy = .45f, .tint = 0xC6D2DC, .look_rate = .1f, .look_amp = .08f, .slow = .2f, .blink = .3f},

    /* 38 more (Nov 2026) */
    [PIP_XD]          = {"xd", S(.w = 132, .h = 100, .taper = -.12f, .smile = .9f, .pw = .34f, .ph = .36f), .gy = -.3f,
                         .look_rate = .1f, .look_amp = .05f, .tremble = 2, .blink = .01f},
    [PIP_ROFL]        = {"rofl", S(.w = 124, .h = 110, .taper = -.12f, .smile = .96f, .pw = .34f, .ph = .36f),
                         .gy = -.2f, .blush = .3f, .look_rate = .1f, .look_amp = .05f, .tremble = .8f, .blink = .01f},
    [PIP_BOUNCY]      = {"bouncy", S(.w = 116, .h = 122, .taper = -.08f, .smile = .45f, .pw = .34f, .ph = .37f, .glint = .3f),
                         .gy = -.3f, .look_rate = .1f, .look_amp = .05f, .blink = .1f},
    [PIP_BLOOMING]    = {"blooming", S(.w = 114, .h = 124, .taper = -.08f, .smile = .3f, .pw = .34f, .ph = .37f, .glint = .3f),
                         .look_rate = .1f, .look_amp = .05f, .blink = .3f},
    [PIP_HEARTEYES]   = {"hearteyes", S(.w = 118, .h = 128, .taper = -.08f, .smile = .15f, .pw = .36f, .ph = .38f, .glint = .5f),
                         .tint = 0xFFE9F0, .look_rate = .1f, .look_amp = .05f, .slow = .3f, .blink = .3f},
    [PIP_FIREWORKS]   = {"fireworks", S(.w = 116, .h = 128, .taper = -.06f, .smile = .25f, .pw = .34f, .ph = .37f),
                         .look_rate = .1f, .look_amp = .05f, .blink = .2f},
    [PIP_MEDAL]       = {"medal", S(.w = 118, .h = 118, .roll = -.02f, .taper = -.08f, .lid = .12f, .tilt = -.04f, .smile = .4f, .pw = .34f, .ph = .37f),
                         .gy = -.3f, .look_rate = .1f, .look_amp = .08f, .blink = .3f},
    [PIP_DISCO]       = {"disco", S(.w = 114, .h = 120, .taper = -.08f, .smile = .5f, .pw = .34f, .ph = .37f),
                         .gy = -.2f, .look_rate = .1f, .look_amp = .05f, .blink = .3f},
    [PIP_POPEYED]     = {"popeyed", S(.w = 116, .h = 126, .pw = .34f, .ph = .37f), .look_rate = .1f, .look_amp = .05f, .blink = .05f},
    [PIP_LIGHTBULB]   = {"lightbulb", S(.w = 116, .h = 128, .taper = -.06f, .pw = .34f, .ph = .37f), .look_rate = .1f, .look_amp = .05f, .blink = .2f},
    [PIP_PAWS]        = {"paws", S(.w = 112, .h = 124, .roll = -.06f, .taper = -.06f, .smile = .15f, .pw = .36f, .ph = .4f),
                         .look_rate = .1f, .look_amp = .05f, .blink = .3f},
    [PIP_HELLO]       = {"hello", S(.w = 116, .h = 124, .taper = -.08f, .smile = .5f, .pw = .34f, .ph = .37f, .glint = .3f),
                         .gy = -.1f, .look_rate = .1f, .look_amp = .05f, .blink = .2f},
    [PIP_KING]        = {"king", S(.w = 118, .h = 114, .taper = -.06f, .lid = .26f, .smile = .38f, .pw = .34f, .ph = .37f),
                         .look_rate = .1f, .look_amp = .05f, .blink = .2f},
    [PIP_ANGEL]       = {"angel", S(.w = 114, .h = 126, .roll = -.06f, .taper = -.08f, .smile = .2f, .pw = .38f, .ph = .42f, .glint = .4f),
                         .look_rate = .1f, .look_amp = .05f, .blink = .05f},
    [PIP_IMP]         = {"imp", S(.w = 118, .h = 118, .roll = .02f, .taper = -.06f, .lid = .16f, .tilt = .06f, .smile = .52f, .pw = .36f, .ph = .4f),
                         .lid_asym = -.18f, .size_asym = .06f, .look_rate = .1f, .look_amp = .05f, .blink = .2f},
    [PIP_JUGGLER]     = {"juggler", S(.w = 116, .h = 122, .taper = -.06f, .smile = .22f, .pw = .34f, .ph = .37f, .glint = .3f),
                         .look_rate = .1f, .look_amp = .05f, .blink = .05f},
    [PIP_BALLOON]     = {"balloon", S(.w = 114, .h = 124, .taper = -.08f, .smile = .3f, .pw = .34f, .ph = .37f, .glint = .3f),
                         .look_rate = .1f, .look_amp = .05f, .blink = .2f},
    [PIP_ICECREAM]    = {"icecream", S(.w = 114, .h = 122, .taper = -.06f, .smile = .25f, .pw = .34f, .ph = .37f),
                         .look_rate = .1f, .look_amp = .05f, .blink = .2f},
    [PIP_UMBRELLA]    = {"umbrella", S(.w = 112, .h = 118, .taper = -.06f, .lid = .06f, .smile = .4f, .pw = .34f, .ph = .37f, .glint = .3f),
                         .look_rate = .1f, .look_amp = .05f, .blink = .3f},
    [PIP_STRONGMAN]   = {"strongman", S(.w = 116, .h = 118, .taper = -.04f, .pw = .3f, .ph = .34f), .look_rate = .1f, .look_amp = .05f, .blink = .05f},
    [PIP_MAGICIAN]    = {"magician", S(.w = 114, .h = 124, .taper = -.06f, .smile = .12f, .pw = .34f, .ph = .37f),
                         .look_rate = .1f, .look_amp = .05f, .blink = .1f},
    [PIP_SUNRISE]     = {"sunrise", S(.w = 116, .h = 118, .roll = -.04f, .taper = -.06f, .smile = .15f, .pw = .34f, .ph = .37f),
                         .look_rate = .1f, .look_amp = .05f, .blink = .05f},
    [PIP_SHEEP]       = {"sheep", S(.w = 116, .h = 116, .roll = -.04f, .lid = .3f, .smile = .05f, .pw = .34f, .ph = .37f),
                         .look_rate = .1f, .look_amp = .05f, .blink = .05f},
    [PIP_NINJA]       = {"ninja", S(.w = 122, .h = 100, .roll = .05f, .lid = .34f, .tilt = .1f, .pw = .28f, .ph = .34f),
                         .look_rate = .1f, .look_amp = .02f, .blink = .05f},
    [PIP_SPROUT]      = {"sprout", S(.w = 114, .h = 124, .taper = -.08f, .smile = .15f, .pw = .36f, .ph = .4f, .glint = .3f),
                         .look_rate = .1f, .look_amp = .05f, .blink = .2f},
    [PIP_RAINCLOUD]   = {"raincloud", S(.w = 112, .h = 120, .roll = -.12f, .taper = .1f, .lid = .28f, .tilt = -.34f, .pw = .38f, .ph = .42f),
                         .gy = .5f, .head_roll = -.06f, .tint = 0xDDE7F5, .look_rate = .1f, .look_amp = .1f, .slow = .4f, .blink = .5f},
    [PIP_SOBBING]     = {"sobbing", S(.w = 118, .h = 92, .roll = -.2f, .lid = .78f, .tilt = -.12f, .pw = .2f, .ph = .2f),
                         .gy = -1, .look_rate = .02f, .look_amp = .01f, .tremble = .8f, .blink = .01f},
    [PIP_HEARTBROKEN] = {"heartbroken", S(.w = 112, .h = 124, .roll = -.1f, .taper = .06f, .lid = .16f, .tilt = -.3f, .pw = .36f, .ph = .4f),
                         .look_rate = .1f, .look_amp = .05f, .slow = .2f, .blink = .3f},
    [PIP_FUMING]      = {"fuming", S(.w = 114, .h = 112, .roll = .18f, .taper = .25f, .lid = .3f, .tilt = .42f, .pw = .2f, .ph = .34f),
                         .look_rate = .1f, .look_amp = .05f, .blink = .05f},
    [PIP_BURNING]     = {"burning", S(.w = 116, .h = 116, .roll = .2f, .taper = .25f, .lid = .34f, .tilt = .5f, .pw = .14f, .ph = .3f),
                         .tint = 0xFF7A2A, .look_rate = .1f, .look_amp = .05f, .tremble = 1.2f, .blink = .02f},
    [PIP_SULKING]     = {"sulking", S(.w = 116, .h = 120, .roll = .06f, .lid = .3f, .tilt = .16f, .pw = .34f, .ph = .38f),
                         .head_roll = -.14f, .blush = .3f, .look_rate = .1f, .look_amp = .03f, .blink = .3f},
    [PIP_SPOOKED]     = {"spooked", S(.w = 120, .h = 140, .roll = -.1f, .taper = -.08f, .tilt = -.2f, .pw = .15f, .ph = .17f),
                         .look_rate = .1f, .look_amp = .02f, .blink = .3f},
    [PIP_TUMBLEWEED]  = {"tumbleweed", S(.w = 122, .h = 112, .lid = .46f, .tilt = -.04f, .pw = .32f, .ph = .35f),
                         .head_roll = .08f, .look_rate = .1f, .look_amp = .03f, .slow = .4f, .blink = .1f},
    [PIP_LOWBATT]     = {"lowbatt", S(.w = 116, .h = 110, .roll = -.06f, .lid = .42f, .tilt = -.14f, .pw = .34f, .ph = .37f),
                         .gy = .5f, .tint = 0xC9CED6, .look_rate = .1f, .look_amp = .1f, .slow = .6f, .blink = .3f},
    [PIP_HUNGRY]      = {"hungry", S(.w = 110, .h = 122, .roll = -.1f, .lid = .14f, .tilt = -.24f, .pw = .36f, .ph = .4f),
                         .look_rate = .1f, .look_amp = .05f, .slow = .1f, .blink = .3f},
    [PIP_KNOCKOUT]    = {"knockout", S(.w = 118, .h = 116, .taper = .04f, .lid = .42f, .pw = .3f, .ph = .32f), .lid_asym = .22f, .size_asym = .08f,
                         .look_rate = .02f, .look_amp = .01f, .slow = .3f, .blink = .01f},
    [PIP_WILTED]      = {"wilted", S(.w = 112, .h = 120, .roll = -.1f, .taper = .06f, .lid = .2f, .tilt = -.3f, .pw = .36f, .ph = .4f),
                         .look_rate = .1f, .look_amp = .03f, .slow = .4f, .blink = .1f},
    [PIP_SNIFFLES]    = {"sniffles", S(.w = 114, .h = 112, .roll = -.06f, .lid = .4f, .tilt = -.16f, .pw = .34f, .ph = .37f),
                         .gy = .2f, .tint = 0xE6E9F0, .look_rate = .1f, .look_amp = .1f, .slow = .3f, .blink = .6f},
    [PIP_CHARGED]     = {"charged", S(.w = 118, .h = 126, .taper = -.06f, .smile = .4f, .pw = .34f, .ph = .37f, .glint = .4f),
                         .gy = -.2f, .look_rate = .1f, .look_amp = .05f, .blink = .2f},
    [PIP_QUEASY]      = {"queasy", S(.w = 116, .h = 120, .roll = -.06f, .lid = .3f, .tilt = -.2f, .pw = .34f, .ph = .37f),
                         .gy = .2f, .tint = 0xD2F2B4, .look_rate = .1f, .look_amp = .05f, .slow = .4f, .blink = .2f},
    [PIP_EMPTY]       = {"empty", S(.w = 116, .h = 116, .roll = -.04f, .lid = .3f, .tilt = -.2f, .pw = .32f, .ph = .35f),
                         .gy = .4f, .tint = 0x8A9098, .look_rate = .1f, .look_amp = .05f, .slow = .5f, .blink = .2f},
    [PIP_CUTOFF]      = {"cutoff", S(.w = 116, .h = 118, .roll = -.04f, .lid = .2f, .tilt = -.18f, .pw = .34f, .ph = .37f),
                         .gy = .3f, .look_rate = .1f, .look_amp = .05f, .blink = .2f},
    [PIP_NEWTREE]     = {"newtree", S(.w = 116, .h = 124, .taper = -.08f, .smile = .3f, .pw = .34f, .ph = .37f),
                         .look_rate = .1f, .look_amp = .05f, .blink = .2f},
    [PIP_WREATH]      = {"wreath", S(.w = 116, .h = 122, .taper = -.06f, .smile = .3f, .pw = .34f, .ph = .37f),
                         .look_rate = .1f, .look_amp = .05f, .blink = .2f},
};
#undef S

static shape_t mood_shape(const mood_def_t *m)
{
    shape_t s = m->s;
    if (!s.w) s.w = 118;
    if (!s.h) s.h = 138;
    if (!s.pw) s.pw = .40f;
    if (!s.ph) s.ph = .44f;
    return s;
}
static float or1(float v) { return v ? v : 1; }

/* Damped spring: zeta < 1 overshoots, which is where the cartoon bounce comes from. */
typedef struct { float x, v; } spring_t;
static void spring(spring_t *s, float to, float stiff, float zeta, float dt)
{
    s->v += ((to - s->x) * stiff - s->v * 2.0f * sqrtf(stiff) * zeta) * dt;
    s->x += s->v * dt;
}

static struct {
    lv_obj_t *canvas, *text;
    lv_timer_t *tick_timer;
    lv_draw_buf_t *buf, *back; /* back: a second canvas when the face renders in the background (face_start) */
    face_t rface;              /* the frame being drawn in the background: a snapshot of face */
    face_rect_t front_rects[4], back_rects[4];
    bool pending;
    uint32_t last_swap;
    face_t face;
    imu_t imu;
    int mood, shown;
    /* reaction: a mood shown for a while on top of the base mood */
    int react_mood;
    float react_left, annoy, pet_total, pet_recent;
    const char *react_name;
    bool meet_pending;
    spring_t shape[2][SHAPE_N];
    spring_t open, squash, look_x, look_y, head_yaw, head_pitch, head_roll, dizzy;
    spring_t tint[3], blush, tremble, nod, wag;
    float squint_t[2]; /* s since that eye was poked (it screws up and opens again); < 0: not squinting */
    float squint[2];   /* how screwed up it is now, 0..1 */
    float finger_path;  /* px the finger moved lately (fading over a quarter second): its speed is this x 4 */
    const char *text_shown;
    float wink_t, next_wink;
    /* the pet behind the face */
    pet_t pet;
    bool auto_mood, bat_charging, no_shakes, shake_hard, phew_due; /* shake_hard: this bout of shaking has made him dizzy already; phew_due: righted off his face after a long while upside down, "phew" when he is back */
    bool shake_ko;                                     /* ...and, kept up hard, knocked him out (or not: told once a bout) */
    unsigned feat_off; /* PIP_F_* bits switched off */
    float intro;       /* seconds into waking up after power-on; < 0: done */
    float hatch;       /* seconds into his very first waking up (the first button after a clean flash); < 0: not now */
    int bat_pct;
    int tz;
    float stroke_t, shakes, last_voice, greet_t, shook_at; /* shook_at: s.t when he was last shaken at all (SHAKE_G_ANY) */
    float dark_in;  /* > 0: the screen goes dark in this many seconds (lying face down) */
    bool dark;      /* the screen is off: nothing to draw */
    float room_db, mic_t, bang_wait, bang_db, last_bang; /* the room's level, and a bang waiting to be sure it is no breath */
    float notes[8];                                      /* when the last notifications came */
    int note_i;
    float glance_x, glance_y, glance_left; /* something on screen worth a look, e.g. a notification */
    int fx_shown;
    float spawn_t;
    float t, blink_t, next_blink, next_look, tgx, tgy, idle_roll, beat;
    float micro_x, micro_y, next_micro;
    bool touching, petting;
    float fx, fy, path, frame_ms;
    uint32_t last_tick, press_tick, last_draw;
    bool eco;
    /* sneezing: s into one (< 0: not sneezing); s to the next on his own; one due shortly (after a poke); how tickled
       his nose is by pokes; his own dice (so a sneeze never shifts anyone else's random numbers) */
    float sneeze_anim, next_sneeze, sneeze_due, itch, last_sneeze;
    uint32_t sneeze_rng, burp_rng;
    float burp_anim, burp_due, next_burp; /* a burp: s into it (< 0: none); one due shortly (after a biscuit) */
    /* the newer feelings' inputs: where the finger went down (resting or moving); a small rubbed patch (tickling) and
       how long; brief strokes broken off (teasing); when he went face down awake (peekaboo) and upside down */
    float press_x, press_y, tk_x, tk_y, tk_spread, tickle_t, tease_t, upside_at;
    float laugh_k, laugh_beat, laugh_voice, laugh_side, laugh_tear; /* tickled: how helpless with it (0..1), s to the next "ha" bounce and laugh */
    int teases;
    bool washing;                   /* the bottom key tapped, then held: the sponge is on him */
    bool refusing;                  /* washed too often lately: the sponge comes and he flings it off */
    float wash_from, wash_t, fuss_t; /* how clean he was when it began; s of it; s of it while already clean */
    float washed_at;                 /* wash_t when a dirty Pip came clean under the sponge (< 0: not yet) */
    /* the phone's attention and his: s of it since anyone touched him (notifications, its music), when he last sulked */
    float phone_s, jealous_at;
    /* trust: the last flinch, the last time he said hi of his own accord; the jolt that was there a moment ago */
    float flinch_at, hi_at;
    bool jolted;
    /* chasing a finger about the glass: s of it, on, played already this touch; the finger's velocity (px/s) */
    float chase_t, fvx, fvy, pounce_t, fx_prev, fy_prev;
    uint32_t finger_tick;
    bool chasing, chased;
    /* dead: forgotten until he starved. Nothing of him is left but how many days he lived; only a clean flash brings
       another Pip (the board keeps this in NVS, pip_set_dead) */
    bool dead;
    int dead_days;
    lv_obj_t *scr, *grave;
    /* being alive (the life section): his own dice; s to his next moment of his own, s nobody has touched him */
    uint32_t life_rng;
    float life_next, life_idle, glance2_t;
    float wet, drip_next, shake_off_t;                 /* after a bath: 1 soaked .. 0 dry */
    float blow_t, chill, snort_next;                   /* s of breath lately; how cold, 0..1 */
    bool blowing, drying_paid, chill_ill;              /* blowing: in a go of breaths (pauses to breathe in included) */
    float blow_gap;                                    /* s since the last breath of this go */
    float wind;                                        /* 0..1: breath in his eyes now - both screw up against it */
    float eyeroll_t, roll_dir;                         /* s into rolling his eyes; which way round */
    float eureka_in, hello_in, sorry_in;               /* > 0: "aha!" after pondering, a hello, a sheepish sorry - this soon */
    int press_eye, eye_hold_mood, cheek_mood;          /* the eye a finger came down on (-1: none); its answer; a cheek's */
    bool eye_pressed, eye_hold_cross;                  /* a finger held on an eye now; held long enough to make him cross */
    float zone_paid;                                   /* the last cheek or chin touch that gave him a little fun */
    float warm_in;                                     /* s till he, frozen, sits down by a fire */
    int blow_stage;                                    /* how far the go has got: the answer, enough, chilly, cold */
    int blow_mood;                                     /* this breath's answer, held while it lasts */
    const char *blow_name;
    float lull, lullaby_next, rock_bored_t;            /* rocked: how near sleep, the next verse, s of it awake */
    bool rock_told;
    float boop_anim, boop_at;                          /* s into a boop on the nose (< 0: none); the last one that paid */
    int boop_kind;
    float fake_t;                                      /* pretending to sleep: s left */
    bool fake_on, dodged;                              /* ...; the finger coming at an eye was dodged */
    float tease_at, dodge_yaw, lean_roll, lean_pitch;  /* the last tease; the head ducking away, leaning into a hand */
    int pat_mood, stroke_mood;                         /* this stroke's face: on the head, or across him */
    int long_mood, chin_mood, rest_mood;               /* ...gone on long, under the chin; a finger resting (PIP_ZEN) */
    int sweet;                                         /* asleep: 0 not settled, 1 sweetly (PIP_SWEETDREAM), 2 plainly */
    int tickle_mood;                                   /* this tickle's laugh: the real one, "> <" (PIP_XD), rolling (PIP_ROFL) */
    bool was_asleep;                                   /* last frame: a good morning when he wakes */
    float sniffle_in;                                  /* s to a cold after a chill caught wet (PIP_SNIFFLES) */
    /* feelings with weight (the affect section): how good he feels inside, -1..1, following what his face shows - fast
       while something happens to him, slowly otherwise; the base mood's branch says if there was a reason for it */
    float affect, shown_at, talk_t, talk_beat, chat_again; /* chat_again: s to a second sentence (a chatty Pip goes on) */
    /* the voice section: when you last did something to him; the reaction showing - when it came,
       the memory it made; a tale due in tell_in s (about memory tell_mem, given up after tell_until s); when he
       last babbled */
    float input_at, react_at, tell_in, tell_until, babble_at;
    int react_mem, tell_mem;
    bool affect_set, mood_reason, blow_wet;
    int bridge;
    float bridge_at;
} s ={.bat_pct = -1, .shook_at = -10, .intro = -1, .hatch = -1, .last_bang = -10, .squint_t = {-1, -1}, .boop_anim = -1, .boop_at = -100, .press_eye = -1, .eye_hold_mood = -1, .cheek_mood = -1, .zone_paid = -100, .tease_at = -100, .pat_mood = -1, .stroke_mood = -1, .long_mood = -1, .chin_mood = -1, .rest_mood = -1, .tickle_mood = -1, .bridge = -1}; /* the battery is unknown until the power chip says */

static float frand(float a, float b) { return a + (b - a) * (float)lv_rand(0, 10000) / 10000.0f; }
static bool eyes_open = true; /* the mood leaves the eyes wide open: a poked eye can screw up (see update) */
static void blink(void) { s.blink_t = 0; }
#define SQUINT_IN 0.06f   /* a poked eye: shut this fast */
#define SQUINT_HOLD 0.12f /* held */
#define SQUINT_OUT 0.15f  /* and open again */
#define WIND_SQUINT 0.8f  /* blown on: both eyes screwed up this far (of a poke's squint), whatever face he makes */
/* the newer feelings' inputs */
#define TICKLE_SPREAD 34.0f /* px: rubbed this close round one patch is tickling, not stroking (a real tickle: 19 median,
                               33 at most; a cheek stroked to and fro ~95 px came out a tickle at 40) */
#define TICKLE_S 0.45f      /* s of it before he laughs (measured on the board: a tickle rubs 14..22 px round its patch at
                               425..960 px/s, anywhere - between the eyes, on the cheeks, the chin, the top) */
#define LAUGH_FULL_S 3.0f   /* tickled this long, he is helpless with it: shaking, squeezed shut, tears of laughter */
#define TOUCHED_S 7.0f      /* s of stroking (loved enough already) until he is moved */
#define COAX_NOTICE_S 2.5f  /* s of stroking a cross Pip before he shows it is getting through ("softening") */
#define REST_S 2.5f         /* s of a finger resting still on him: dreamy */
#define TRACK_S 2.0f        /* s of a finger moving slowly that he follows: focused */
#define TEASE_S 1.0f        /* a stroke shorter than this, three within TEASE_SPAN_S: teased, pouty */
#define TEASE_SPAN_S 8.0f
#define RELIEF_S 2.0f       /* upside down this long: "phew" when he is the right way up */
#define SHAKE_HARD_G 1.2f   /* imu energy (smoothed |a| - 1 g) over this is a hard shake: dizzy, never glad */
#define SHAKE_LONG_S 1.6f   /* a middling shake kept up this long is too much as well */
#define SHAKE_LIGHT_LONG_S 4.0f /* and a light one this long */
#define SHAKE_G_ANY 0.25f   /* imu energy over this is being shaken at all (a light shake measured 0.25..0.35) */
#define SHAKE_G 0.5f        /* imu.c's line for IMU_SHAKE, as it is there */
#define TUMBLE_N 4         /* turns over (two there and back) within TUMBLE_S: tumbling */
#define TUMBLE_S 4.0f
#define TUMBLE_MUCH 3.0f    /* tumbles in a row (one fading in 40 s): the fourth leaves him dizzy instead */
#define SWING_TILT 0.35f    /* swinging: the tilt past this either side ... */
#define SWING_GAP_S 1.3f    /* ... and back within this, four times running */
#define SWING_MIN_S 0.22f   /* but not sooner: side to side faster than this is jiggling him (a shake), not a swing */
const char *pip_mood_name(int mood) { return moods[mood].name; }

int pip_find_mood(const char *name)
{
    for (int i = 0; i < PIP_MOOD_COUNT; i++)
        if (!strcmp(moods[i].name, name)) return i;
    return -1;
}

/* what Pip says for a mood, -1 = nothing */
static int mood_voice(int mood)
{
    switch (mood) {
    case PIP_OUCH: return VOICE_POKE;
    case PIP_HAPPY: case PIP_CONTENT: case PIP_CHEEKY: case PIP_YES: return VOICE_HAPPY;
    case PIP_SAD: case PIP_HURT: case PIP_DESPONDENT: case PIP_LONELY: case PIP_GLOOMY: return VOICE_SAD;
    case PIP_ANGRY: case PIP_ANNOYED: case PIP_FURIOUS: case PIP_RAGE: case PIP_BELLIGERENT: case PIP_INDIGNANT: return VOICE_GROWL;
    case PIP_SURPRISED: case PIP_SHOCKED: case PIP_AMAZED: case PIP_AFRAID: case PIP_DISMAYED: return VOICE_OH;
    case PIP_LOVE: return VOICE_LOVE;
    case PIP_NAUSEOUS: return VOICE_BURP;
    case PIP_ASLEEP: case PIP_SLEEPY: case PIP_TIRED: case PIP_EXHAUSTED: return VOICE_YAWN;
    case PIP_DIZZY: case PIP_STONED: return VOICE_DIZZY;
    case PIP_DULL: return VOICE_DERP;
    case PIP_SICK: case PIP_PINKEYE: return VOICE_MUMBLE;
    case PIP_SHY: return VOICE_EHE;
    case PIP_BORED: return VOICE_SIGH;
    case PIP_COMFY: return VOICE_MMM;
    case PIP_NERVOUS: return VOICE_TAPTAP;
    case PIP_TUMBLING: return VOICE_LOOP;
    case PIP_CURIOUS: case PIP_PUZZLED: case PIP_SUSPICIOUS: return VOICE_HM;
    case PIP_SWEARING: return VOICE_CENSOR;
    case PIP_BERSERK: return VOICE_RANT;
    case PIP_SKEPTICAL: case PIP_CYNICAL: case PIP_DISGUSTED: case PIP_SMUG: case PIP_NO: return VOICE_HMPH;
    case PIP_ALLERGIC: return VOICE_SNEEZE;
    case PIP_FROZEN: case PIP_OVERHEATED: return VOICE_SAD;
    case PIP_SMITTEN: return VOICE_LOVE;
    case PIP_HYPNOTIZED: return VOICE_DIZZY;
    case PIP_HYPER: return VOICE_HAPPY;
    case PIP_ZOMBIE: return VOICE_YAWN;
    case PIP_EXCITED: return VOICE_WHEE;
    case PIP_DREAMY: return VOICE_HUM;
    case PIP_POUTY: return VOICE_POUT;
    case PIP_PROUD: return VOICE_TADA;
    case PIP_FOCUSED: return VOICE_THINK;
    case PIP_TOUCHED: return VOICE_AWW;
    case PIP_PLAYFUL: return VOICE_PEEK;
    case PIP_RELIEVED: return VOICE_PHEW;
    case PIP_LAUGHING: return VOICE_LAUGH;
    case PIP_GRATEFUL: return VOICE_THANKS;
    case PIP_DANCING: return VOICE_HAPPY;
    case PIP_WHIRLED: return VOICE_LOOP;
    case PIP_WOOZY: return VOICE_DIZZY;
    case PIP_IMPATIENT: return VOICE_TAPTAP;
    case PIP_WISTFUL: return VOICE_SIGH;
    case PIP_BLISSFUL: return VOICE_MMM;
    case PIP_ADORING: return VOICE_OOH;
    case PIP_SLY: return VOICE_SNICKER;
    case PIP_SHEEPISH: return VOICE_EHE;
    case PIP_DETERMINED: return VOICE_HUP;
    case PIP_EUREKA: return VOICE_AHA;
    case PIP_EYEROLL: return VOICE_UGH;
    case PIP_FLUSTERED: return VOICE_KYA;
    case PIP_SQUISHED: return VOICE_MMPH;
    case PIP_STRETCHED: return VOICE_BOING;
    case PIP_OVERWHELMED: return VOICE_EEK;
    case PIP_PINCHED: return VOICE_PINCH;
    case PIP_BEAMING: return VOICE_YAY;
    case PIP_CUDDLY: return VOICE_NUZZLE;
    case PIP_EAGER: return VOICE_OOHOOH;
    case PIP_YUMMY: return VOICE_YUM;
    case PIP_TRIUMPHANT: return VOICE_FANFARE;
    case PIP_DEFLATED: return VOICE_DEFLATE;
    case PIP_SPARKLING: return VOICE_SQUEAK;
    case PIP_GROGGY: return VOICE_GROGGY;
    case PIP_STARSTRUCK: return VOICE_WOW; case PIP_SINGING: return VOICE_LALA; case PIP_KISS: return VOICE_MWAH;
    case PIP_FLIRTY: return VOICE_HEYYOU; case PIP_RAINBOW: return VOICE_XYLO; case PIP_PEEKING: return VOICE_KNOCK;
    case PIP_ZEN: return VOICE_OM; case PIP_SWEETDREAM: return VOICE_SNORE; case PIP_MELTING: return VOICE_MELT;
    case PIP_APPLAUSE: return VOICE_CLAPS; case PIP_COOL: return VOICE_BASSLINE; case PIP_PUPPY: return VOICE_WHIMPER;
    case PIP_BUTTERFLY: return VOICE_FLUTTER; case PIP_BUBBLES: return VOICE_BLUB; case PIP_ROBOT: return VOICE_BEEPBOOP;
    case PIP_KITTY: return VOICE_PURR; case PIP_SILLY: return VOICE_BLBLBL; case PIP_SNICKER: return VOICE_PFFT;
    case PIP_CAMPFIRE: return VOICE_CRACKLE; case PIP_STARGAZING: return VOICE_SHOOTING; case PIP_ZAPPED: return VOICE_ZAP;
    case PIP_SIPPING: return VOICE_GULP;
    case PIP_XD: return VOICE_HEEHEE; case PIP_ROFL: return VOICE_ROLLING; case PIP_BOUNCY: return VOICE_BOINGS;
    case PIP_BLOOMING: return VOICE_PETALS; case PIP_HEARTEYES: return VOICE_LUBDUB; case PIP_FIREWORKS: return VOICE_ROCKETUP;
    case PIP_MEDAL: return VOICE_MEDALIST; case PIP_DISCO: return VOICE_DISCOBEAT; case PIP_POPEYED: return VOICE_EYEPOP;
    case PIP_LIGHTBULB: return VOICE_IDEAPING; case PIP_PAWS: return VOICE_PEEKSHY; case PIP_HELLO: return VOICE_HELLOO;
    case PIP_KING: return VOICE_ROYAL; case PIP_ANGEL: return VOICE_HALOSONG; case PIP_IMP: return VOICE_MWAHAHA;
    case PIP_JUGGLER: return VOICE_HOPLA; case PIP_BALLOON: return VOICE_LIFTOFF; case PIP_ICECREAM: return VOICE_LICK;
    case PIP_UMBRELLA: return VOICE_PITTER; case PIP_STRONGMAN: return VOICE_HEAVE; case PIP_MAGICIAN: return VOICE_ABRACADABRA;
    case PIP_SUNRISE: return VOICE_DAWN; case PIP_SHEEP: return VOICE_BAA; case PIP_NINJA: return VOICE_HIYA;
    case PIP_SPROUT: return VOICE_GROWING; case PIP_RAINCLOUD: return VOICE_GREYSIGH; case PIP_SOBBING: return VOICE_BAWL;
    case PIP_HEARTBROKEN: return VOICE_CRACKED; case PIP_FUMING: return VOICE_KETTLE; case PIP_BURNING: return VOICE_SMOULDER;
    case PIP_SULKING: return VOICE_HMFF; case PIP_SPOOKED: return VOICE_TEETH; case PIP_TUMBLEWEED: return VOICE_EMPTYWIND;
    case PIP_LOWBATT: return VOICE_POWERDOWN; case PIP_HUNGRY: return VOICE_RUMBLE; case PIP_KNOCKOUT: return VOICE_BIRDIES;
    case PIP_WILTED: return VOICE_LOVESMENOT; case PIP_SNIFFLES: return VOICE_SNIFFLE;
    case PIP_CHARGED: return VOICE_POPFULL; case PIP_CUTOFF: return VOICE_YANK; case PIP_EMPTY: return VOICE_POWERFAIL; case PIP_QUEASY: return VOICE_RETCH;
    case PIP_NEWTREE: return VOICE_JINGLE; case PIP_WREATH: return VOICE_HANGUP;
    }
    return -1;
}

/* Требовательный sleeps only face down: that one cannot be switched off then (the panel shows it locked on) */
/* held on: Требовательный sleeps only face down; waking by motion needs him to feel the shakes */
#define FEAT(f) (!(s.feat_off >> (f) & 1) || ((f) == PIP_F_FACEDOWN && pet_level == 2) ||                  ((f) == PIP_F_SHAKE && !(s.feat_off >> PIP_F_WAKE & 1)))
#define HEARS(f) (FEAT(f) && !s.eco) /* listening for breath or bangs keeps the mic on: off when saving energy */
#define ECO_FRAME_MS 50 /* energy saving: at most 20 frames a second (the face is most of the battery after the panel) */
void pip_eco(bool on) { s.eco = on; }
bool pip_eco_on(void) { return s.eco; }
void pip_feature(int f, bool on)
{
    if (f < 0 || f >= PIP_F_COUNT) return; /* a command's garbage: no shift past the word */
    s.feat_off = on ? s.feat_off & ~(1u << f) : s.feat_off | 1u << f;
}
bool pip_feature_on(int f) { return f >= 0 && f < PIP_F_COUNT && FEAT(f); }
void *pip_pet(void) { return &s.pet; }

static bool covered; /* pip_covered: the setup screen lies over him */
static bool away;    /* pip_away: the menu or an app is on screen - he is switched off, only the menu works */
static bool unmet(void);
static float life_rand(float a, float b);
static int vary_to(int r, int alt, float p);
/* one of several answers to the same thing (pick, answer) */
typedef struct { int mood; float w; const char *name; float secs; } answer_t;
static int answer(const answer_t *a, int n, int *last);
/* he listens (breath, bangs, music) only on his face, awake, lit: an open mic (its I2S) keeps the chip out of light sleep
   (a night of it emptied the battery) and its analysis costs a core's time. The menu, an app or a game, the setup screen,
   a sleeping or dormant Pip, a dark screen: the host may switch it off (the noise meter keeps the level alone, shell_mic) */
bool pip_mic_wanted(void)
{
    return !s.dark && !away && !covered && !s.dead && !s.pet.asleep && !unmet() &&
           (HEARS(PIP_F_BLOW) || HEARS(PIP_F_NOISE) || HEARS(PIP_F_MUSIC));
}
/* ---- his voice: how he sounds, what he says, and when he keeps quiet ----------------------------------------------
   Every sound of his goes through speak(), which keeps a budget (snd: a token every pet_tune.snd_refill s, up to 2.5):
   something you did to him (class 2) always gets an answer while the budget lasts and a short "o!" / "m?" / "eh" when it
   runs low; a fright, a fall, a throw (class 3) always speaks; a feeling that only comes and goes (class 1) speaks when
   there is budget, otherwise a little noise if the feeling changed a lot, otherwise his face alone; talk (class 0) needs
   a good deal of quiet. A sound never cuts off a more important one still playing. He sounds as he is (voice_tone): up
   and brisk when glad and lively, low and slow tired or glum. And each feeling has several sounds (mood_sound): the
   usual one most often, never the same one twice running. */
#define VB_BASE 1000 /* past every VOICE_* */
#define VB(kind) (VB_BASE + (kind)) /* a sound code that is a babble of this kind, said in the feeling's tone */
static float mood_val[PIP_MOOD_COUNT];
static bool night_now(void);
static struct {
    float tokens, at; /* the budget; when the last sound started */
    int cls;          /* the class of the last one */
    int mood;         /* the feeling it was said in */
    uint32_t rng;     /* its own dice: choosing sounds never shifts anyone else's random numbers */
    float bv, ba;     /* a babble's tone given outright (bset: a tale in its memory's tone) */
    bool bset;
} snd = {.tokens = 2, .at = -100, .mood = -1};
static float vrand(float a, float b)
{
    uint32_t *x = &snd.rng;
    if (!*x) *x = 0x5EED7A1Cu;
    *x ^= *x << 13, *x ^= *x >> 17, *x ^= *x << 5;
    return a + (b - a) * (*x % 10000) / 10000.0f;
}
/* how worked up a feeling is, 0 (drowsy, glum, cosy) .. 1 (beside himself) */
static float mood_aro(int m)
{
    switch (m) {
    case PIP_BERSERK: case PIP_RAGE: case PIP_SWEARING: case PIP_HYPER: case PIP_EXCITED: case PIP_SHOCKED: return 1;
    case PIP_FURIOUS: case PIP_LAUGHING: case PIP_AFRAID: case PIP_TUMBLING: case PIP_WHIRLED: case PIP_DANCING: return 0.85f;
    case PIP_ANGRY: case PIP_SURPRISED: case PIP_AMAZED: case PIP_PLAYFUL: case PIP_PROUD: case PIP_BELLIGERENT: case PIP_OUCH: return 0.7f;
    case PIP_SLEEPY: case PIP_TIRED: case PIP_ASLEEP: case PIP_DULL: case PIP_ZOMBIE: case PIP_STONED: return 0.1f;
    case PIP_EXHAUSTED: case PIP_DESPONDENT: case PIP_GLOOMY: case PIP_BORED: case PIP_COMFY: case PIP_DREAMY: case PIP_WOOZY: return 0.2f;
    case PIP_SAD: case PIP_LONELY: case PIP_CONTENT: case PIP_HYPNOTIZED: case PIP_HURT: return 0.3f;
    case PIP_EUREKA: return 0.9f;
    case PIP_IMPATIENT: case PIP_DETERMINED: case PIP_FLUSTERED: return 0.75f;
    case PIP_WISTFUL: case PIP_BLISSFUL: return 0.15f;
    case PIP_ADORING: case PIP_SHEEPISH: case PIP_EYEROLL: return 0.4f;
    case PIP_OVERWHELMED: case PIP_BEAMING: case PIP_TRIUMPHANT: case PIP_EAGER: return 0.85f;
    case PIP_PINCHED: case PIP_STRETCHED: case PIP_SQUISHED: return 0.7f;
    case PIP_CUDDLY: case PIP_GROGGY: case PIP_DEFLATED: return 0.15f;
    case PIP_YUMMY: case PIP_SPARKLING: return 0.45f;
    case PIP_STARSTRUCK: case PIP_APPLAUSE: case PIP_SILLY: case PIP_SNICKER: case PIP_ZAPPED: return 0.85f;
    case PIP_ZEN: case PIP_SWEETDREAM: case PIP_CAMPFIRE: case PIP_STARGAZING: return 0.12f;
    case PIP_MELTING: case PIP_KITTY: case PIP_BUTTERFLY: case PIP_SIPPING: return 0.3f;
    case PIP_XD: case PIP_ROFL: case PIP_BOUNCY: case PIP_FIREWORKS: case PIP_POPEYED: case PIP_DISCO: case PIP_BURNING: return 0.9f;
    case PIP_HELLO: case PIP_JUGGLER: case PIP_BALLOON: case PIP_STRONGMAN: case PIP_MAGICIAN: case PIP_NINJA: case PIP_FUMING:
    case PIP_SPOOKED: case PIP_LIGHTBULB: case PIP_SOBBING: case PIP_IMP: return 0.7f;
    case PIP_HEARTEYES: case PIP_MEDAL: case PIP_KING: case PIP_ICECREAM: case PIP_PAWS: case PIP_ANGEL: case PIP_SPROUT: case PIP_BLOOMING:
    case PIP_SULKING: return 0.45f;
    case PIP_UMBRELLA: case PIP_SUNRISE: case PIP_HEARTBROKEN: case PIP_RAINCLOUD: case PIP_HUNGRY: case PIP_SNIFFLES: case PIP_KNOCKOUT: return 0.3f;
    case PIP_SHEEP: case PIP_TUMBLEWEED: case PIP_LOWBATT: case PIP_WILTED: case PIP_EMPTY: return 0.12f;
    case PIP_QUEASY: case PIP_NEWTREE: return 0.4f;
    case PIP_WREATH: return 0.45f;
    }
    return 0.5f;
}
/* how he sounds just now: glad and lively up and brisk, tired or glum low and slow, sleepy at night */
static void tone_now(int mood)
{
    float a = mood >= 0 ? mood_aro(mood) : 0.5f, e = s.pet.energy;
    voice_tone(1.0f * s.affect + 1.4f * (e - 0.5f) + 0.8f * (a - 0.5f) - (night_now() ? 0.5f : 0) + vrand(-0.4f, 0.4f),
               1 + 0.3f * (0.5f - e) - 0.25f * (a - 0.5f));
}
static void say(int sound)
{
    if (sound < 0 || !FEAT(PIP_F_VOICE) || covered || away) return; /* under "РЕЖИМ НАСТРОЙКИ" or off his screen he keeps quiet */
    voice_tone(0, 1);
    voice_say(sound);
    s.last_voice = s.t;
}
/* one of his own sounds, coloured by how he is; a babble in this feeling's tone (valence, how worked up) */
static void say_as(int sound, int mood)
{
    if (sound < 0 || !FEAT(PIP_F_VOICE) || covered || away) return;
    tone_now(mood);
    if (sound >= VB_BASE) {
        float v = snd.bset ? snd.bv : mood >= 0 ? 0.7f * mood_val[mood] + 0.3f * s.affect : s.affect;
        voice_babble(sound - VB_BASE, v, snd.bset ? snd.ba : mood >= 0 ? mood_aro(mood) : 0.5f);
    } else voice_say(sound);
    s.last_voice = s.t;
}
/* the feeling's sounds: the usual one first (twice as likely), the rest its other ways of saying it */
static int mood_sound_at(int mood, int *count, int at) /* at >= 0: that one of the feeling's sounds, in order; -1: the next */
{
    int o[5] = {mood_voice(mood), -1, -1, -1, -1};
    switch (mood) {
    case PIP_HAPPY: case PIP_CONTENT: case PIP_YES: o[1] = VB(BABBLE_EXCLAIM), o[2] = VOICE_GIGGLE, o[3] = VB(BABBLE_SAY); break;
    case PIP_CHEEKY: o[1] = VOICE_TEASE, o[2] = VOICE_GIGGLE; break;
    case PIP_SAD: case PIP_HURT: case PIP_DESPONDENT: case PIP_LONELY: case PIP_GLOOMY: o[1] = VOICE_MUMBLE, o[2] = VB(BABBLE_SOB); break;
    case PIP_ANGRY: case PIP_FURIOUS: case PIP_BELLIGERENT: case PIP_RAGE: o[1] = VOICE_MUTTER, o[2] = VB(BABBLE_COMPLAIN); break;
    case PIP_ANNOYED: case PIP_INDIGNANT: o[1] = VOICE_HMPH, o[2] = VOICE_MUTTER, o[3] = VB(BABBLE_COMPLAIN); break;
    case PIP_SURPRISED: case PIP_AMAZED: o[1] = VB(BABBLE_EXCLAIM), o[2] = VB(BABBLE_ASK); break;
    case PIP_AFRAID: case PIP_DISMAYED: o[1] = VB(BABBLE_SOB); break;
    case PIP_LOVE: case PIP_SMITTEN: o[1] = VOICE_AWW, o[2] = VOICE_HUM; break;
    case PIP_SLEEPY: case PIP_TIRED: o[1] = VB(BABBLE_SAY), o[2] = VOICE_HUM; break;
    case PIP_DIZZY: case PIP_STONED: case PIP_HYPNOTIZED: case PIP_WOOZY: o[1] = VB(BABBLE_WOOZY); break;
    case PIP_CURIOUS: case PIP_PUZZLED: case PIP_SUSPICIOUS: o[1] = VB(BABBLE_ASK), o[2] = VOICE_THINK; break;
    case PIP_SKEPTICAL: case PIP_CYNICAL: case PIP_SMUG: o[1] = VB(BABBLE_COMPLAIN), o[2] = VOICE_HM; break;
    case PIP_EXCITED: o[1] = VB(BABBLE_EXCLAIM), o[2] = VOICE_LAUGH; break;
    case PIP_POUTY: o[1] = VOICE_MUMBLE, o[2] = VOICE_HMPH; break;
    case PIP_PROUD: o[1] = VB(BABBLE_EXCLAIM); break;
    case PIP_FOCUSED: o[1] = VOICE_HM; break;
    case PIP_TOUCHED: o[1] = VOICE_LOVE; break;
    case PIP_PLAYFUL: o[1] = VOICE_TEASE, o[2] = VOICE_GIGGLE; break;
    case PIP_RELIEVED: o[1] = VB(BABBLE_SAY); break;
    case PIP_LAUGHING: o[1] = VOICE_GIGGLE, o[2] = VB(BABBLE_GIGGLY); break;
    case PIP_GRATEFUL: o[1] = VOICE_HAPPY, o[2] = VOICE_AWW; break;
    case PIP_WHIRLED: o[1] = VOICE_WHEE; break;
    case PIP_BORED: case PIP_DULL: o[1] = VOICE_PHEW, o[2] = VOICE_MUMBLE; break;
    case PIP_IMPATIENT: o[1] = VB(BABBLE_ASK), o[2] = VOICE_HM; break;
    case PIP_WISTFUL: o[1] = VOICE_HUM, o[2] = VOICE_MUMBLE; break;
    case PIP_BLISSFUL: o[1] = VOICE_HUM; break;
    case PIP_ADORING: o[1] = VOICE_AWW, o[2] = VOICE_LOVE; break;
    case PIP_SLY: o[1] = VOICE_TEASE, o[2] = VB(BABBLE_GIGGLY); break;
    case PIP_SHEEPISH: o[1] = VOICE_GIGGLE; break;
    case PIP_DETERMINED: o[1] = VB(BABBLE_EXCLAIM); break;
    case PIP_EUREKA: o[1] = VB(BABBLE_EXCLAIM); break;
    case PIP_EYEROLL: o[1] = VOICE_HMPH, o[2] = VB(BABBLE_COMPLAIN); break;
    case PIP_FLUSTERED: o[1] = VOICE_GIGGLE, o[2] = VB(BABBLE_GIGGLY); break;
    /* the rest had one sound only: a second way of saying each */
    case PIP_OUCH: o[1] = VB(BABBLE_INTERJ), o[2] = VOICE_EEK; break;
    case PIP_ASLEEP: o[1] = VOICE_MMM; break;
    case PIP_COMFY: o[1] = VOICE_HUM, o[2] = VOICE_NUZZLE; break;
    case PIP_EXHAUSTED: o[1] = VOICE_SIGH, o[2] = VOICE_MUMBLE; break;
    case PIP_NAUSEOUS: o[1] = VOICE_UGH, o[2] = VOICE_MUMBLE; break;
    case PIP_SICK: case PIP_PINKEYE: o[1] = VOICE_SIGH, o[2] = VB(BABBLE_SOB); break;
    case PIP_SHY: o[1] = VOICE_AWW, o[2] = VOICE_GIGGLE; break;
    case PIP_NERVOUS: o[1] = VB(BABBLE_ASK), o[2] = VOICE_EEK; break;
    case PIP_SWEARING: o[1] = VOICE_MUTTER; break;
    case PIP_BERSERK: o[1] = VOICE_CENSOR; break;
    case PIP_DISGUSTED: o[1] = VOICE_UGH, o[2] = VB(BABBLE_COMPLAIN); break;
    case PIP_SHOCKED: o[1] = VOICE_EEK, o[2] = VB(BABBLE_EXCLAIM); break;
    case PIP_NO: o[1] = VB(BABBLE_COMPLAIN), o[2] = VOICE_UGH; break;
    case PIP_ALLERGIC: o[1] = VOICE_SNORT; break;
    case PIP_FROZEN: o[1] = VOICE_BRR; break;
    case PIP_OVERHEATED: o[1] = VOICE_PHEW, o[2] = VOICE_SIGH; break;
    case PIP_HYPER: o[1] = VOICE_WHEE, o[2] = VOICE_LAUGH; break;
    case PIP_ZOMBIE: o[1] = VOICE_MUMBLE, o[2] = VOICE_GROGGY; break;
    case PIP_DREAMY: o[1] = VOICE_SIGH, o[2] = VOICE_MMM; break;
    case PIP_DANCING: o[1] = VOICE_WHEE, o[2] = VOICE_HUM; break;
    case PIP_TUMBLING: o[1] = VOICE_WHEE, o[2] = VOICE_GIGGLE; break;
    /* the newer answers */
    case PIP_SQUISHED: o[1] = VB(BABBLE_COMPLAIN), o[2] = VOICE_GIGGLE; break;
    case PIP_STRETCHED: o[1] = VOICE_OH, o[2] = VB(BABBLE_WOOZY); break;
    case PIP_OVERWHELMED: o[1] = VOICE_OH, o[2] = VB(BABBLE_EXCLAIM); break;
    case PIP_PINCHED: o[1] = VOICE_EEK, o[2] = VB(BABBLE_COMPLAIN); break;
    case PIP_BEAMING: o[1] = VOICE_HAPPY, o[2] = VOICE_GIGGLE; break;
    case PIP_CUDDLY: o[1] = VOICE_MMM, o[2] = VOICE_AWW; break;
    case PIP_EAGER: o[1] = VB(BABBLE_ASK), o[2] = VOICE_TAPTAP; break;
    case PIP_YUMMY: o[1] = VOICE_NOM, o[2] = VOICE_MMM; break;
    case PIP_TRIUMPHANT: o[1] = VOICE_TADA, o[2] = VB(BABBLE_EXCLAIM); break;
    case PIP_DEFLATED: o[1] = VOICE_SIGH, o[2] = VB(BABBLE_SOB); break;
    case PIP_SPARKLING: o[1] = VOICE_HAPPY, o[2] = VOICE_TADA; break;
    case PIP_GROGGY: o[1] = VOICE_YAWN, o[2] = VOICE_MUMBLE; break;
    /* the scenes with pictures: two of their own, as often as each other (mood_sound) */
    case PIP_STARSTRUCK: o[1] = VOICE_TWINKLE; break; case PIP_SINGING: o[1] = VOICE_ARIA; break; case PIP_KISS: o[1] = VOICE_SMOOCH; break;
    case PIP_FLIRTY: o[1] = VOICE_WINK; break; case PIP_RAINBOW: o[1] = VOICE_PRISM; break; case PIP_PEEKING: o[1] = VOICE_POPUP; break;
    case PIP_ZEN: o[1] = VOICE_BOWL; break; case PIP_SWEETDREAM: o[1] = VOICE_DREAMSONG; break; case PIP_MELTING: o[1] = VOICE_COO; break;
    case PIP_APPLAUSE: o[1] = VOICE_BRAVO; break; case PIP_COOL: o[1] = VOICE_YEAH; break; case PIP_PUPPY: o[1] = VOICE_YIP; break;
    case PIP_BUTTERFLY: o[1] = VOICE_HUSHED; break; case PIP_BUBBLES: o[1] = VOICE_BUBBLEPOP; break; case PIP_ROBOT: o[1] = VOICE_SERVO; break;
    case PIP_KITTY: o[1] = VOICE_MEOW; break; case PIP_SILLY: o[1] = VOICE_YOYO; break; case PIP_SNICKER: o[1] = VOICE_CRACKUP; break;
    case PIP_CAMPFIRE: o[1] = VOICE_COZY; break; case PIP_STARGAZING: o[1] = VOICE_WISH; break; case PIP_ZAPPED: o[1] = VOICE_POWERUP; break;
    case PIP_SIPPING: o[1] = VOICE_FILLUP; break;
    case PIP_XD: o[1] = VOICE_WHEEZE; break; case PIP_ROFL: o[1] = VOICE_HOHO; break; case PIP_BOUNCY: o[1] = VOICE_YIPPEE; break;
    case PIP_BLOOMING: o[1] = VOICE_SPRINGSONG; break; case PIP_HEARTEYES: o[1] = VOICE_SWOON; break; case PIP_FIREWORKS: o[1] = VOICE_OOHAAH; break;
    case PIP_MEDAL: o[1] = VOICE_MHM; break; case PIP_DISCO: o[1] = VOICE_GROOVY; break; case PIP_POPEYED: o[1] = VOICE_WHOANELLY; break;
    case PIP_LIGHTBULB: o[1] = VOICE_GOTIT; break; case PIP_PAWS: o[1] = VOICE_BASHFUL; break; case PIP_HELLO: o[1] = VOICE_YOOHOO; break;
    case PIP_KING: o[1] = VOICE_HARRUMPH; break; case PIP_ANGEL: o[1] = VOICE_WHISTLING; break; case PIP_IMP: o[1] = VOICE_TEEHEE; break;
    case PIP_JUGGLER: o[1] = VOICE_CIRCUS; break; case PIP_BALLOON: o[1] = VOICE_POPFALL; break; case PIP_ICECREAM: o[1] = VOICE_FREEZE; break;
    case PIP_UMBRELLA: o[1] = VOICE_DRYHUM; break; case PIP_STRONGMAN: o[1] = VOICE_STRONG; break; case PIP_MAGICIAN: o[1] = VOICE_RABBIT; break;
    case PIP_SUNRISE: o[1] = VOICE_STRETCH; break; case PIP_SHEEP: o[1] = VOICE_COUNTING; break; case PIP_NINJA: o[1] = VOICE_SHADOW; break;
    case PIP_SPROUT: o[1] = VOICE_BLOSSOM; break; case PIP_RAINCLOUD: o[1] = VOICE_DRIZZLE; break; case PIP_SOBBING: o[1] = VOICE_SOBS; break;
    case PIP_HEARTBROKEN: o[1] = VOICE_LETGO; break; case PIP_FUMING: o[1] = VOICE_GRRMPH; break; case PIP_BURNING: o[1] = VOICE_ROARLET; break;
    case PIP_SULKING: o[1] = VOICE_NYEH; break; case PIP_SPOOKED: o[1] = VOICE_BOOO; break; case PIP_TUMBLEWEED: o[1] = VOICE_MEH; break;
    case PIP_LOWBATT: o[1] = VOICE_LOWBEEP; break; case PIP_HUNGRY: o[1] = VOICE_NOCOOKIE; break; case PIP_KNOCKOUT: o[1] = VOICE_KAPUT; break;
    case PIP_WILTED: o[1] = VOICE_FADING; break; case PIP_SNIFFLES: o[1] = VOICE_ACHOOLET; break;
    case PIP_CHARGED: o[1] = VOICE_CHARGEDUP; break; case PIP_CUTOFF: o[1] = VOICE_NOTFULL; break; case PIP_EMPTY: o[1] = VOICE_FEEBLE; break; case PIP_QUEASY: o[1] = VOICE_BURP; break;
    case PIP_NEWTREE: o[1] = VOICE_OHTREE; break; case PIP_WREATH: o[1] = VOICE_AAH; break;
    }
    int n = 1;
    while (n < 5 && o[n] >= 0) n++;
    if (count) { *count = o[0] < 0 ? 0 : n; return at >= 0 && at < n ? o[at] : o[0]; }
    if (n == 1 || o[0] < 0 || pet_tune.vary <= 0) return o[0];
    static int8_t last[PIP_MOOD_COUNT];
    int k;
    do k = vrand(0, n + 1) < 2 ? 0 : 1 + (int)vrand(0, n - 1.001f);
    while (k == last[mood] && vrand(0, 1) < 0.85f);
    last[mood] = (int8_t)k;
    return o[k];
}
static int mood_sound(int mood) { return mood_sound_at(mood, NULL, -1); }
int pip_mood_sounds(int mood) { int n; mood_sound_at(mood, &n, -1); return n; } /* pipsim --list: every feeling has two or more */
const char *pip_mood_say(int mood, int k, bool *babble) /* says one of the feeling's sounds, as it is (pipsim --sounds, for the emotions list): which voice */
{
    int n, o = mood_sound_at(mood, &n, k);
    if (k >= n) return NULL;
    voice_tone(0, 1);
    if ((*babble = o >= VB_BASE)) voice_babble(o - VB_BASE, 0.85f * mood_val[mood], mood_aro(mood));
    else voice_say(o);
    return *babble ? "babble" : voice_name(o);
}
/* speak if the budget allows (see above); true when something was heard */
static bool speak(int sound, int cls, int mood)
{
    if (sound < 0 || !FEAT(PIP_F_VOICE) || covered || away) return false;
    if (pet_tune.snd_refill <= 0) { say_as(sound, mood); return true; } /* no budget: every feeling speaks, as it used to */
    if (voice_busy() && cls < snd.cls && s.t - snd.at < 1.5f) return false; /* never over something that matters more */
    /* two answers to one thing (a swing that turns out a shake): the face goes to the later, the sound is the first's */
    if (voice_busy() && cls <= snd.cls && cls < 3 && s.t - snd.at < 0.35f) return false;
    float dv = mood >= 0 && snd.mood >= 0 ? fabsf(mood_val[mood] - mood_val[snd.mood]) : 1;
    int play = sound;
    float cost = 1;
    if (cls >= 3) cost = 0.5f;
    else if (cls == 2) { if (snd.tokens < -0.5f) play = snd.tokens >= -1.2f ? VB(BABBLE_INTERJ) : -1, cost = 0.3f; }
    else if (cls == 1) { if (snd.tokens < 1) play = snd.tokens >= 0.4f && dv >= 0.5f ? VB(BABBLE_INTERJ) : -1, cost = 0.4f; }
    else if (snd.tokens < 1.3f || voice_busy() || s.t - s.last_voice < 1.2f) play = -1; /* talk: only into a quiet */
    if (play < 0) return false;
    snd.tokens = fmaxf(-1.5f, snd.tokens - cost), snd.at = s.t, snd.cls = cls, snd.mood = mood;
    say_as(play, mood);
    return true;
}

/* ---- what happened to him lately: the last few things that moved him, to tell about ----------------------------- */
#define MEM_N 4
#define MEM_FADE_S 600.0f /* a memory's weight e-folds in 10 minutes: long after, there is nothing to tell */
typedef struct { int mood; float v, a, w, t; int told; bool wobbly; } memory_t;
static memory_t mem[MEM_N];
static float mem_w(int i) { return mem[i].w > 0 ? mem[i].w * expf(-(s.t - mem[i].t) / MEM_FADE_S) : 0; }
static int remember(int mood, float secs)
{
    float v = mood_val[mood];
    bool wobbly = mood == PIP_DIZZY || mood == PIP_WOOZY || mood == PIP_HYPNOTIZED || mood == PIP_TUMBLING || mood == PIP_WHIRLED;
    if ((fabsf(v) < 0.3f && !wobbly) || secs < 0.4f) return -1;
    float w = fminf(1, fabsf(v) * 0.7f + 0.3f * mood_aro(mood) + (wobbly ? 0.3f : 0)) * fminf(1, secs / 2.5f + 0.25f);
    for (int i = 0; i < MEM_N; i++) /* the same feeling again a moment later: the same memory, stronger */
        if (mem[i].w > 0 && mem[i].mood == mood && s.t - mem[i].t < 8) { mem[i].t = s.t, mem[i].w = fminf(1, mem[i].w + w * 0.3f); return i; }
    int k = 0;
    for (int i = 1; i < MEM_N; i++) k = mem_w(i) < mem_w(k) ? i : k;
    mem[k] = (memory_t){mood, v, mood_aro(mood), w, s.t, 0, wobbly};
    return k;
}

static bool unmet(void) { return s.pet.version != PET_VERSION || !s.pet.met; }

/* The last few reactions that spoke, and when. Two things that each refresh their reaction every frame (a breath on
   the mic during a stroke, a scrub and a stroke) would take turns, each turn restarting its sound - a buzz of cut-off
   starts. So a reaction that pushes out another one still showing, and has spoken within REACT_QUIET_S, comes back
   silently; one that follows nothing (a poke after the last has passed) always speaks. */
#define REACT_QUIET_S 3.0f
static struct { const char *name; float t; } spoke[4];
static struct { float plugs[8]; int n; int64_t plug_at, paid_at, fiddled_at; } cab; /* pip_battery's cable */
#define PEEK_GAP_S 20.0f  /* peekaboo pays fun and love once in this long */
#define CHASE_GAP_S 60.0f /* a chase pays a game's worth once in this long */
static float peek_at = -1000, chase_at = -1000, spiral_at = -1000; /* the last spiral that gave him a little fun */
/* hiccups (a run of "hic!"s) and a toot: see hic_step and toot_face; each its own dice, so no one else's shift */
#define HIC_POP 0.25f /* s each hiccup pops his eyes wide */
static struct { int left; float next, pop, cured_in; uint32_t rng; } hic;
static struct { float anim; float due; int after; uint32_t rng; } toot = {.anim = -1};

static bool spoke_lately(const char *name)
{
    for (int i = 0; i < 4; i++)
        if (spoke[i].name == name && s.t - spoke[i].t < REACT_QUIET_S) return true;
    return false;
}

/* How much a reaction matters, for who may take its place and whether it speaks: 3 a fright, a fall, a throw, and the
   few that must show (waking, the song, the alarm); 2 an answer to something you just did (a touch, a key, a shake, a
   breath, the phone); 1 the rest - his own feelings coming and going */
static int react_cls(int mood, const char *name)
{
    static const char *const must[] = {"thrown", "dropped", "bang", "face down", "woke up", "hello", "time's up", "birthday",
                                       "festive", "made up", "tumbled out", "head spin",
                                       "sobs", "knocked out", "heartbroken", "spooked"};
    if (mood == PIP_OUCH || mood == PIP_SHOCKED || mood == PIP_AFRAID) return 3;
    for (unsigned i = 0; i < sizeof must / sizeof *must; i++)
        if (!strcmp(name, must[i])) return 3;
    return s.t - s.input_at < 1.5f ? 2 : 1; /* what follows a moment after (the biscuit landing, a game's end) too */
}
/* one held back by the one showing (see react): it comes when that one has had its moment, unless it is stale by then */
static struct { int mood, cls; float secs, at; const char *name; bool force; } rq = {.mood = -1};
static void tell_after(int i);
static void react(int mood, float secs, const char *name)
{
    if (unmet() || (s.hatch >= 0 && strcmp(name, "hello"))) return; /* asleep for good until a button, or waking */
    if (hic.left > 0 && (mood == PIP_SHOCKED || mood == PIP_AFRAID || !strcmp(name, "boo"))) /* a fright cures hiccups */
        hic.left = 0, hic.cured_in = 2.4f;
    int cls = rq.force ? rq.cls : react_cls(mood, name);
    bool fresh = mood != s.react_mood || name != s.react_name, crowding = s.react_mood >= 0 && s.react_left > 0;
    /* a face has its moment: a feeling of his own (class 1) does not take the place of one showing before pet_tune.hold, so
       faces do not flicker past one another; the one held back waits (the newest of them). An answer to you is at once */
    if (fresh && crowding && !rq.force && cls == 1 && pet_tune.hold > 0 && s.t - s.react_at < pet_tune.hold) {
        rq.mood = mood, rq.cls = cls, rq.secs = secs, rq.at = s.t, rq.name = name;
        return;
    }
    s.react_mood = mood;
    s.react_left = secs;
    s.react_name = name;
    if (!fresh) return; /* petting refreshes the same reaction every frame */
    if (rq.mood >= 0 && !rq.force) rq.mood = -1; /* something newer came: the one waiting is out of date */
    s.react_at = s.t;
    s.react_mem = cls >= 2 ? remember(mood, secs) : -1; /* what you did to him: something to tell about after */
    if (cls >= 2 && s.talk_t > 0) voice_hush(), s.talk_t = 0, s.chat_again = 0; /* interrupted mid-sentence */
    if (cls >= 2) s.tell_in = 0;
    if (crowding && spoke_lately(name)) return; /* back after being crowded out a moment: the face returns, not the sound */
    if (away) return; /* the menu or an app on screen (a game's result): kept, short, for the face; not a sound */
    int oldest = 0;
    for (int i = 1; i < 4; i++) oldest = spoke[i].t < spoke[oldest].t ? i : oldest;
    spoke[oldest].name = name, spoke[oldest].t = s.t;
    static const struct { const char *name; int sound; } said[] = {
        {"yum", VOICE_NOM}, {"charging", VOICE_NOM}, {"blown", VOICE_OH}, {"too full", VOICE_OH}, {"burp", -1}, {"dislikes you", VOICE_HMPH}, {"time's up", VOICE_ALARM},
        {"fed up", VOICE_GROWL}, {"pet", VOICE_GIGGLE}, {"meet", VOICE_HM}, {"face down", VOICE_YAWN}, {"hello", VOICE_LOVE}, {"won", VOICE_HAPPY}, {"lost", VOICE_SAD},
        {"boop", -1}, {"rocked", -1}, {"drying", -1}, {"dry", VOICE_HAPPY}, {"cold", VOICE_BRR}, {"shiver", VOICE_BRR}, {"yawns", VOICE_YAWN},
        {"sighs", VOICE_PHEW}, {"giggles", VOICE_GIGGLE}, {"ponders", VOICE_THINK}, {"shy look", VOICE_AWW}, {"fake sleep", -1}, {"boo", VOICE_PEEK},
        {"peeks", VOICE_LAUGH}, {"dodged", VOICE_LAUGH}, {"eyes pried open", VOICE_GROWL}, {"eyes closed", VOICE_YAWN}, {"lulled", VOICE_YAWN},
        {"shakes off", VOICE_SNORT}, {"misses you", VOICE_SAD}, {"head pat", VOICE_AWW}, {"poke: ignore", VOICE_HMPH}, {"poke: again", VOICE_TEASE}, {"blown: chilly", VOICE_BRR}, {"blown: enough", VOICE_HMPH}, {"chats", -1}, {"tells", -1}, {"coughs", -1},
        {"drums", -1}, {"your turn", VOICE_HM}, {"head spin", VOICE_DIZZY}, {"tumbling", VOICE_LOOP}, {"tumbled out", VOICE_DIZZY}, {"birthday", VOICE_BIRTHDAY}, {"festive", VOICE_TADA},
        {"hic", -1}, {"toot", -1}, {"chin rub", VOICE_MMM}, {"hiccups gone", VOICE_PHEW}, {"aha", VOICE_AHA}, {"rolls eyes", VOICE_UGH},
        {"let go", VOICE_BOING}, {"palm off", VOICE_PHEW}, {"fingers gone", VOICE_PHEW}, {"popped", VOICE_PEEK},
    };
    for (unsigned i = 0; i < sizeof said / sizeof said[0]; i++)
        if (!strcmp(name, said[i].name)) { /* the timer's song is the interface's: it rings even with his voice off */
            if (said[i].sound == VOICE_ALARM) voice_say(VOICE_ALARM), s.last_voice = s.t;
            else if (said[i].sound == VOICE_BIRTHDAY) say(VOICE_BIRTHDAY); /* a tune: its own pitch and pace */
            else speak(said[i].sound, cls, mood);
            return;
        }
    speak(mood_sound(mood), cls, mood);
}

void pip_react(int mood, float secs, const char *name) { react(mood, secs, name); }

/* the 38 scenes of Nov 2026 as reactions: each its own name (an old one would bring its old sound, said[] above) and one
   round of its loop (the scenes are drawn to loop: show_face) */
static const struct { const char *name; float secs; } show_as[PIP_MOOD_COUNT - PIP_XD] = {
    {"squeals", 3.2f}, {"rolls laughing", 2.0f}, {"bounces", 2.4f}, {"blooms", 5.0f}, {"heart eyes", 3.6f},
    {"fireworks", 4.5f}, {"medal", 4.0f}, {"dancing", 4.0f}, {"eyes pop", 3.8f}, {"bright idea", 4.6f},
    {"hides in paws", 5.2f}, {"waves hi", 3.0f}, {"king", 5.0f}, {"angel", 5.0f}, {"imp", 3.4f},
    {"juggles", 1.6f}, {"balloon", 5.4f}, {"ice cream", 6.0f}, {"umbrella", 3.6f}, {"lifts weights", 5.0f},
    {"magic trick", 5.2f}, {"good morning", 6.0f}, {"counts sheep", 6.0f}, {"ninja", 4.2f}, {"sprouts", 6.0f},
    {"rain cloud", 4.4f}, {"sobs", 4.8f}, {"heartbroken", 5.0f}, {"fumes", 4.0f}, {"burns", 3.0f},
    {"turns away", 5.0f}, {"spooked", 6.0f}, {"tumbleweed", 6.0f}, {"run down", 4.2f}, {"dreams of food", 5.6f},
    {"knocked out", 4.0f}, {"wilts", 6.4f}, {"sniffles", 4.6f},
    {"unplugged: full", 4.0f}, {"unplugged: low", 4.4f}, {"unplugged: empty", 4.6f}, {"queasy", 4.2f},
    {"new year tree", 4.8f}, {"wreath", 5.0f},
};
static const char *show_name(int m) { return show_as[m - PIP_XD].name; }
static void show_react(int m) { react(m, show_as[m - PIP_XD].secs, show_as[m - PIP_XD].name); }
static bool walked_lately(void);
static void shaken_hard(void);
/* lifted off the table with the screen dozing (woke: out of a face-down sleep, a pocket): out on a walk with half the day's
   steps done he shows you the barbell (every time, but not again within STRONG_GAP_S); left alone a while he waves; by day
   now and then a balloon has him */
#define STRONG_GAP_S 300.0f
static float strong_at = -STRONG_GAP_S; /* file scope: clock_rebase moves it */
static void picked_up(bool woke)
{
    if (s.pet.asleep || unmet() || away || pet_tune.vary <= 0 || (!woke && s.react_mood >= 0)) return;
    int h = pet_hour(s.pet.last, s.tz);
    float x = life_rand(0, 1);
    if (walked_lately()) { if (s.t - strong_at >= STRONG_GAP_S) strong_at = s.t, show_react(PIP_STRONGMAN); }
    else if (woke) return;
    else if (s.t - s.input_at > 300 && x < 0.45f) show_react(PIP_HELLO);
    else if (h >= 9 && h < 20 && x > 0.85f) show_react(PIP_BALLOON);
}
/* woken in the morning (6..11 by his clock), fed and loved, and not cross about it: good morning, once a day */
static void good_morning(void)
{
    static int32_t day = -1;
    int h = pet_hour(s.pet.last, s.tz);
    int32_t d = (int32_t)((s.pet.last + s.tz * 60) / 86400);
    if (h < 6 || h >= 11 || d == day || s.pet.food < 0.5f || s.pet.love < 0.5f || s.dark || away || covered) return;
    if (s.react_mood >= 0 && mood_val[s.react_mood] < 0) return;
    if (!vary_to(0, 1, 0.6f)) return;
    day = d, show_react(PIP_SUNRISE);
}

void pip_set_mood(int mood)
{
    s.mood = (mood % PIP_MOOD_COUNT + PIP_MOOD_COUNT) % PIP_MOOD_COUNT;
    s.auto_mood = false;
    say(mood_voice(s.mood)); /* picked by hand: he says it */
}

void pip_auto(bool on) { s.auto_mood = on; }

static void die(void);
static bool bday_today(void);
#define SNOW_S 9.0f /* 1 January's snowfall, s */
static void snow_start(float secs);
#define CHARGE_FOOD_H 9.0f /* food an hour on the cable: empty to full in ~7 minutes, as the board's 0.005 every 2 s was */
void pip_time(int64_t now, int tz_min)
{
    s.tz = tz_min;
    if (s.dead) return; /* there is no one left to live the time */
    if (s.pet.version != PET_VERSION) pet_init(&s.pet, now, tz_min, lv_rand(1, 0x7FFFFFFF));
    if (unmet()) s.pet.last = s.pet.born = now; /* dormant: nothing drops, and day 1 is the day he wakes */
    else if (FEAT(PIP_F_TIME)) {
        int64_t was = s.pet.last;
        pet_update(&s.pet, now, tz_min);
        /* the cable feeds him by the clock, the same on the board and the sims - but only up to full_line: past it he is
           stuffed, and a Pip just flashed (on the cable, born at full_line) felt sick at his very first biscuit */
        if (FEAT(PIP_F_BATTERY) && s.bat_charging && s.pet.food < pet_tune.full_line)
            s.pet.food = fminf(pet_tune.full_line, s.pet.food + CHARGE_FOOD_H * (float)(s.pet.last - was) / 3600);
    }
    else s.pet.last = now; /* time stands still for him: nothing drops while filming */
    if (pet_dead(&s.pet)) { die(); return; } /* forgotten for days, he starved */
    pet_festive = bday_today(); /* before the tree picks: the owner's birthday is a great day */
    bool bat = FEAT(PIP_F_BATTERY), charging = bat && s.bat_charging; /* what the mood tree reads of the battery */
    if (charging && !s.pet.charging) s.pet.charge_since = s.pet.last;
    s.pet.charging = charging, s.pet.bat = (int8_t)(bat ? s.bat_pct : -1);
    if (s.auto_mood) {
        s.mood = pet_settle(&s.pet, tz_min);
        pet_why_t w;
        pet_mood_why(&s.pet, tz_min, &w);
        /* asleep, a rare card, the body, an aftertaste, a situation (just woke, out walking, charging): something made it,
           it shows at once. The day's palette, a need, the night: drifts, and his face gets there by degrees */
        s.mood_reason = w.kind != PET_WHY_POOL && w.kind != PET_WHY_URGE && w.kind != PET_WHY_NIGHT;
    }
}

/* the battery is his dinner: plugged in he cheers up and eats, a little every reading, until the charger goes */
void pip_shake_ok(bool ok) { s.no_shakes = !ok; }
void pip_wake_up(void) { s.intro = 0, s.open.x = 0, s.open.v = 0; }
static bool pet_dirty;
/* the battery run flat (main.c: the moment before it powers him off, under EMPTY_MV for EMPTY_S with no cable - never a
   reset, a flash or the top key): food and energy drop at once; switched on again (main.c read "drained" back at boot)
   the power-on intro ends angry, not cheeky, and he stays cross a while (PET_AFTER_DRAINED, a grudge) */
static bool woke_drained;
void pip_drained(void)
{
    if (unmet() || s.dead) return;
    pet_event(&s.pet, PET_DRAINED);
    pet_dirty = true;
}
void pip_drained_wake(void) { woke_drained = !s.dead; }

/* ---------- the first meeting: after a clean flash he lies dormant, eyes shut, deaf to the screen, until a button ---------- */
bool pip_unmet(void) { return unmet() || s.hatch >= 0; } /* dormant, or still waking for the first time */
bool pip_pet_changed(void) { bool c = pet_dirty; pet_dirty = false; return c; }
void pip_meet_quietly(void) { if (s.pet.version == PET_VERSION && !s.pet.met) s.pet.met = true, s.pet.born = s.pet.last; }
static void grave_show(void);
void pip_forget(void)
{
    if (s.grave) lv_obj_delete(s.grave), s.grave = NULL;
    s.dead = false, s.dead_days = 0;
    pet_init(&s.pet, s.pet.last, s.tz, lv_rand(1, 0x7FFFFFFF)), s.hatch = -1, s.react_mood = -1;
}

/* ---------- death: forgotten, he starved (pet_dead). All of him is wiped; what is left is a black screen saying he
   died and how many days he lived. Nothing answers any more (shell.c, phone.c); only a clean flash, which erases the
   board's NVS, brings a new Pip - an update keeps this ("dead" in NVS, main.c) ---------- */
static void die(void)
{
    int days = pet_age_days(&s.pet, s.tz);
    pip_set_dead(days);
    pet_dirty = true; /* the board stores it at once */
}

static void dead_lit(void);
void pip_set_dead(int days)
{
    int64_t last = s.pet.last;
    memset(&s.pet, 0, sizeof s.pet); /* nothing of him is kept */
    s.pet.last = last;
    s.dead = true, s.dead_days = days < 1 ? 1 : days;
    s.react_mood = -1, s.hatch = -1, s.intro = -1, s.washing = s.chasing = s.touching = s.petting = false;
    s.sneeze_anim = s.burp_anim = s.boop_anim = -1, s.fake_on = false;
    grave_show();
    dead_lit(); /* the news shows for DEAD_DARK_S, then the screen goes dark */
}
bool pip_dead(void) { return s.dead; }
int pip_dead_days(void) { return s.dead ? s.dead_days : 0; }
void pip_die(void) { if (!s.dead && !unmet()) die(); }

/* The glass of his screen cracked: lines from a point of impact out past the rim, a branch off each, and pieces of
   rings between neighbours, the spider-web of broken glass. Made once (a fixed pattern from its own xorshift, libm only
   here, never per frame) as plain LVGL lines on the black: the screen is static and goes dark in DEAD_DARK_S anyway. */
#define CRACK_RAYS 9
#define CRACK_SEGS 7   /* points of a ray after the impact */
#define CRACK_TWIGS 3  /* points of a branch after where it leaves its ray */
#define CRACK_RINGS 2  /* ring pieces between each pair of neighbouring rays */
static lv_point_precise_t crack_ray[CRACK_RAYS][CRACK_SEGS + 1], crack_twig[CRACK_RAYS][CRACK_TWIGS + 1],
    crack_ring[CRACK_RAYS][CRACK_RINGS][2];

static void crack_line(lv_obj_t *g, const lv_point_precise_t *pts, int n, int w, lv_opa_t opa)
{
    lv_obj_t *l = lv_line_create(g);
    lv_line_set_points(l, pts, n);
    lv_obj_set_style_line_width(l, w, 0);
    lv_obj_set_style_line_color(l, lv_color_hex(th->dim), 0);
    lv_obj_set_style_line_opa(l, opa, 0);
    lv_obj_set_style_line_rounded(l, false, 0);
}

static void cracks(lv_obj_t *g)
{
    uint32_t r = 0x9E3779B9u;
#define CR_RND(a, b) (r ^= r << 13, r ^= r >> 17, r ^= r << 5, (a) + ((b) - (a)) * (float)(r % 1000) / 1000.0f)
    const float cx = 168, cy = 150; /* the blow landed up and to the left of the words */
    float ang[CRACK_RAYS];
    for (int i = 0; i < CRACK_RAYS; i++) ang[i] = (i + CR_RND(0.15f, 0.85f)) * 6.2832f / CRACK_RAYS;
    for (int i = 0; i < CRACK_RAYS; i++) {
        float x = cx, y = cy, a = ang[i];
        crack_ray[i][0] = (lv_point_precise_t){(int32_t)x, (int32_t)y};
        for (int k = 1; k <= CRACK_SEGS; k++) { /* a jagged run outward, each step a little longer */
            a += CR_RND(-0.32f, 0.32f);
            float len = CR_RND(24, 40) + k * 8;
            x += cosf(a) * len, y += sinf(a) * len;
            crack_ray[i][k] = (lv_point_precise_t){(int32_t)x, (int32_t)y};
        }
        int from = 2 + (int)CR_RND(0, 3); /* a branch leaves the ray part way out */
        float bx = (float)crack_ray[i][from].x, by = (float)crack_ray[i][from].y, ba = ang[i] + (CR_RND(0, 1) < 0.5f ? -0.7f : 0.7f);
        crack_twig[i][0] = crack_ray[i][from];
        for (int k = 1; k <= CRACK_TWIGS; k++) {
            ba += CR_RND(-0.4f, 0.4f), bx += cosf(ba) * CR_RND(18, 30), by += sinf(ba) * CR_RND(18, 30);
            crack_twig[i][k] = (lv_point_precise_t){(int32_t)bx, (int32_t)by};
        }
    }
    for (int i = 0; i < CRACK_RAYS; i++) /* rings: from a point of one ray to the same step of the next */
        for (int k = 0; k < CRACK_RINGS; k++) {
            int at = 1 + k * 2;
            crack_ring[i][k][0] = crack_ray[i][at], crack_ring[i][k][1] = crack_ray[(i + 1) % CRACK_RAYS][at];
        }
#undef CR_RND
    for (int i = 0; i < CRACK_RAYS; i++) {
        for (int k = 0; k < CRACK_RINGS; k++) crack_line(g, crack_ring[i][k], 2, 1, LV_OPA_40);
        crack_line(g, crack_twig[i], CRACK_TWIGS + 1, 1, LV_OPA_50);
        crack_line(g, crack_ray[i], CRACK_SEGS + 1, 2, LV_OPA_70);
    }
}

static lv_obj_t *grave_text(lv_obj_t *g, const lv_font_t *font, uint32_t color, const char *text, int y)
{
    lv_obj_t *l = th_text_at(g, font, color, text, -1, y); /* on a patch of the black: the cracks run under it */
    lv_obj_set_style_bg_color(l, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(l, 12, 0);
    lv_obj_set_style_pad_ver(l, 6, 0); /* the two patches meet: no stray bits of crack between the lines */
    return l;
}

static void grave_show(void)
{
    if (!s.scr || s.grave) return;
    lv_obj_t *g = s.grave = lv_obj_create(s.scr);
    lv_obj_remove_style_all(g);
    lv_obj_set_size(g, SCR, SCR);
    lv_obj_set_style_bg_color(g, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
    lv_obj_remove_flag(g, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g, LV_OBJ_FLAG_SCROLLABLE); /* the rays run past the rim */
    cracks(g);
    const lv_font_t *big = th->title ? th->title : th->body;
    int h = lv_font_get_line_height(big), y = SCR / 2 - h / 2 - 18;
    grave_text(g, big, th->text, th_label("Пип умер"), y);
    int d = s.dead_days, m10 = d % 10, m100 = d % 100;
    const char *word = m100 >= 11 && m100 <= 14 ? "дней" : m10 == 1 ? "день" : m10 >= 2 && m10 <= 4 ? "дня" : "дней";
    char t[40];
    snprintf(t, sizeof t, "прожил %d %s", d, word);
    grave_text(g, th->small, th->dim, th_label(t), y + h + 10);
}
static void light_up_at(int line);
#define light_up() light_up_at(__LINE__)
static int lit_line, seen_line; /* what lit the screen last (pip.c's line), and pip_seen's caller (pip_seen_from) */
int pip_lit_why(int *seen) { if (seen) *seen = seen_line; return lit_line; }
void pip_hatch(void)
{
    if (s.dead) return;
    if (!unmet() || s.pet.version != PET_VERSION) return; /* met already (or no clock yet) */
    s.pet.met = true;
    s.pet.born = s.pet.last; /* his first day */
    s.pet.asleep = false, s.pet.after_mood = PET_NO_MOOD;
    s.hatch = 0, s.react_mood = -1;
    pet_dirty = true; /* the board saves him at once */
    light_up();
}
void pip_birth_show(float at) /* debug `birth [s]`, bench.py: his birth played again for a met Pip, from here on - it only wakes him */
{
    if (s.dead || unmet()) return;
    s.hatch = fmaxf(0, at), s.react_mood = -1, s.pet.asleep = false;
    light_up();
}

void pip_battery(int pct, bool charging)
{
    if (FEAT(PIP_F_BATTERY) && !unmet() && !s.dead && s.bat_pct >= 0) {
        /* the cable: plugged in he is glad; the second time within fiddle_s a doubtful look; from the fiddle_n-th on every
           plug is fiddling, and each one makes him crosser (pet.c: angry, furious, rage). Taken off kindly early he is
           pleased only for a real charge (unplug_min minutes on it), not straight after fiddling and once in unplug_gap
           hours: in and out again and again is no way to farm his mood */
        if (charging && !s.bat_charging) {
            cab.plugs[cab.n++ % 8] = s.t;
            int recent = 0;
            for (int i = 0; i < 8; i++) recent += cab.plugs[i] > 0 && s.t - cab.plugs[i] < pet_tune.fiddle_s;
            cab.plug_at = s.pet.last;
            if (recent >= (int)pet_tune.fiddle_n) {
                cab.fiddled_at = s.pet.last;
                int r = pet_event(&s.pet, PET_CABLE_FIDDLE);
                if (!s.pet.asleep && (r == PIP_ANGRY || r == PIP_FURIOUS) && vary_to(0, 1, 0.35f)) show_react(PIP_FUMING); /* steam out of his ears */
                else if (!s.pet.asleep) react(r, 3.0f, "cable fiddling");
            } else if (s.pet.asleep) {
            } else if (recent >= 2) react(PIP_SKEPTICAL, 2.5f, "cable again");
            else { /* glad of it: drinks it in if he is low or tired, a jolt of energy if he is not */
                static int last;
                bool low = pct < 30 || s.pet.energy < 0.35f;
                const answer_t a[] = {{PIP_HAPPY, 0.7f, "charging", 4.0f}, {PIP_ZAPPED, low ? 0.6f : 3, "charging: zap", 3.6f},
                                      {PIP_SIPPING, low ? 3 : 0.9f, "charging: sip", 5.0f}};
                answer(a, 3, &last);
            }
        }
        if (!charging && s.bat_charging) { /* taken off: overcharged, or kindly early */
            const pet_tune_t *k = &pet_tune;
            bool earned = s.pet.last - cab.plug_at >= (int64_t)(k->unplug_min * 60)
                && (!cab.fiddled_at || s.pet.last - cab.fiddled_at >= (int64_t)(k->fiddle_s * 3))
                && (!cab.paid_at || s.pet.last - cab.paid_at >= (int64_t)(k->unplug_gap * 3600));
            s.pet.bat = (int8_t)(earned ? pct : -1); /* out of unplug_lo..hi: no praise this time (overcharge still counts) */
            int r = pet_event(&s.pet, PET_UNPLUGGED);
            s.pet.bat = (int8_t)pct;
            if (r == PIP_HAPPY) cab.paid_at = s.pet.last;
            bool fiddled = s.pet.last - cab.plug_at < 20; /* in and straight out: no show for that */
            if (r == PIP_HAPPY && !s.pet.asleep && vary_to(0, 1, 0.6f)) show_react(PIP_CHARGED); /* taken off kindly, full: proud of it */
            else if (r >= 0 && !s.pet.asleep) react(r, 3.0f, r == PIP_HURT ? "overcharged" : "unplugged right");
            else if (r < 0 && !s.pet.asleep && !fiddled && pet_tune.vary > 0) show_react(pct >= 80 ? PIP_CHARGED : pct >= 30 ? PIP_CUTOFF : PIP_EMPTY); /* by the charge he got */
        }
    }
    s.bat_pct = pct, s.bat_charging = charging;
}

void pip_action(int act)
{
    if (act == PIP_ACT_FEED) { pip_feed(); return; } /* with the biscuit, from wherever it is asked */
    int r = pet_event(&s.pet, PET_PLAY);
    if (r >= 0)
        react(r, 2.0f, act == PIP_ACT_PLAY ? (r == PIP_NO ? "worn out" : r == PIP_DISGUSTED ? "too dirty" : r == PIP_HURT ? "sulking" : "play")
                       : r == PIP_NAUSEOUS ? "too full" : r == PIP_DISGUSTED ? "enough" : "yum");
}

void pip_play_move(void) { pet_event(&s.pet, PET_MOVE), pet_rouse(&s.pet, s.tz, PET_SEEN); }

void pip_attention(int kind, float dt) /* shell.c: you busy off his face (1 an app or the menu, 2 a game, 3 one with him) */
{
    if (kind <= 0 || pip_dead() || unmet()) return;
    pet_attention(&s.pet, dt, kind);
}

bool pip_wants_play(void)
{
    return !s.pet.asleep && s.pet.energy >= pet_tune.play_tired && s.pet.clean >= pet_tune.play_dirty && !pet_sulking(&s.pet);
}

/* a game with him ended off his face: its face waits GAME_KEPT_S for you to come back to him, then plays from the start */
#define GAME_KEPT_S 10.0f
static struct game_kept { int mood; const char *name; float at; } game_kept = {.mood = -1};
void pip_game(int result)
{
    s.input_at = s.t;
    static const struct { int ev; const char *name; } g[] = {{PET_WIN, "won"}, {PET_LOSE, "lost"}, {PET_RECORD, "record"}};
    int m = pet_event(&s.pet, g[result].ev);
    bool sore = result == PIP_GAME_LOST && s.pet.after_mood != PET_NO_MOOD && s.pet.after_why == PET_AFTER_SORE;
    if (!sore && m >= 0 && result == PIP_GAME_WON && mood_val[m] >= 0) m = vary_to(vary_to(vary_to(vary_to(m, PIP_COOL, 0.25f), PIP_TRIUMPHANT, 0.45f), PIP_MEDAL, 0.2f), PIP_KING, 0.2f);  /* he beat you */
    if (!sore && m >= 0 && result == PIP_GAME_LOST && mood_val[m] <= 0 && mood_val[m] > -0.6f) { /* you beat him: let down - or, a good sport, he claps */
        m = vary_to(m, PIP_DEFLATED, 0.45f);
        if (s.pet.trust > 0.4f && !pet_cold(&s.pet)) m = vary_to(m, PIP_APPLAUSE, 0.3f);
    }
    if (m >= 0 && result == PIP_GAME_RECORD && !pet_cold(&s.pet)) m = vary_to(vary_to(vary_to(m, PIP_STARSTRUCK, 0.5f), PIP_APPLAUSE, 0.35f), PIP_FIREWORKS, 0.3f); /* a record of yours! */
    bool shows = m >= PIP_STARSTRUCK; /* a scene: long enough to play */
    const char *said = sore ? "sore loser" : m >= PIP_XD ? show_name(m) : shows && m == PIP_APPLAUSE ? "claps" : g[result].name;
    react(m, m >= PIP_XD ? show_as[m - PIP_XD].secs : shows || sore ? 3.5f : 2.5f, said); /* beaten once too often: it shows */
    /* ended on the game's screen: kept for when you come back to him (game_back), so the face is seen, not spent unseen */
    if (away && m >= 0 && (result != PIP_GAME_RECORD || shows)) game_kept = (struct game_kept){m, said, s.t};
    /* a game with him ends while its screen is up (away: his voice is off there): he still lets you hear how he took it -
       glad when he won, sad when he lost (one sound, in his tone of the moment; never the same one twice running) */
    if (result == PIP_GAME_RECORD || !away || covered || !FEAT(PIP_F_VOICE)) return;
    static const int won[] = {VOICE_TADA, VOICE_HAPPY, VOICE_LAUGH}, lost[] = {VOICE_SAD, VOICE_MUMBLE, VOICE_POUT};
    static int last = -1;
    int k = (int)vrand(0, 2.999f);
    if (k == last) k = (k + 1) % 3;
    last = k;
    tone_now(m >= 0 ? m : result == PIP_GAME_WON ? PIP_HAPPY : PIP_SAD);
    voice_say(m == PIP_APPLAUSE ? VOICE_BRAVO : result == PIP_GAME_WON ? won[k] : sore ? VOICE_SAD : lost[k]);
    s.last_voice = s.t;
}

void pip_phone(int what)
{
    s.input_at = s.t;
    static const struct { int mood; const char *name; } r[] = {
        {PIP_YES, "answered"}, {PIP_NO, "declined"}, {PIP_PUZZLED, "missed call"}, {PIP_YES, "paired"}};
    if (!s.pet.asleep) react(r[what].mood, 2.5f, r[what].name);
}

/* a pile of notifications at once dismays him; one by one they are just a glance */
/* asleep face down: the phone does not disturb him - no sound, and the screen nobody can see stays dark */
bool pip_sleeping_face_down(void) { return !unmet() && s.pet.asleep && s.imu.face_down; }
void pip_wake(void) { if (!pip_sleeping_face_down()) light_up(); } /* something to look at came: a notification, a call */

void pip_note(void)
{
    if (s.dead) return;
    s.input_at = s.t;
    int h = pet_hour(s.pet.last, s.tz);
    if (s.pet.asleep && (h >= 23 || h < 7)) pet_event(&s.pet, PET_NIGHT_NOTE); /* the night's sleep broken: cross in the morning */
    s.phone_s += 60; /* the phone wants looking at again */
    if (!pip_sleeping_face_down()) light_up();
    s.notes[s.note_i++ % 8] = s.t;
    int n = 0;
    for (int i = 0; i < 8; i++) n += s.notes[i] > 0 && s.t - s.notes[i] < pet_tune.flood_s;
    if (n < pet_tune.flood_n || s.pet.asleep) return;
    react(pet_event(&s.pet, PET_FLOOD), 3.0f, "flood");
    memset(s.notes, 0, sizeof s.notes);
}

/* lying face down the screen goes dark a second after he falls asleep; turned over, or a button, wakes it */
#define DARK_AFTER_S 1.0f
#define DARK_AGAIN_S 5.0f /* woken by a button while still face down: dark again after this */
bool pip_display_on(void) { return !s.dark; }
bool pip_drawing(void) { return !s.dark && !away && !covered && !s.dead; } /* his eyes are being drawn all the time */

/* Power: left alone for a while (no touch, no button, not picked up, nothing from the phone) the screen dims, then goes
   dark and the face stops drawing - the face and the panel are most of the battery. A touch, a button, picking him up
   (turned more than 25 degrees from how he lay) or a notification wakes him. Apps that are there to be looked at
   (the torch, the noise meter, the timer...) hold him awake while open. */
#define DIM_BEFORE_S 5.0f
static struct {
    int after;      /* seconds to sleep, 0 = never */
    float idle, lp[3], g[3];
    bool dozed, dim, hold;
} pw = {.after = 30};
void pip_power(int seconds) { pw.after = seconds < 0 ? 0 : seconds > 3600 ? 3600 : seconds, pw.idle = 0; } /* kept as an i16 in NVS */
int pip_power_after(void) { return pw.after; }
void pip_hold_awake(bool on) { pw.hold = on; }
void pip_covered(bool on) { covered = on; }
void pip_away(bool on)
{
    bool back = away && !on;
    away = on;
    if (back && game_kept.mood >= 0) { /* back on his face soon after a game: how he took it, from the start */
        if (s.t - game_kept.at <= GAME_KEPT_S && !s.pet.asleep && !s.dead) {
            s.react_mood = -1; /* fresh: the face (and its scene) begins again, and he says it */
            react(game_kept.mood, game_kept.mood >= PIP_XD ? show_as[game_kept.mood - PIP_XD].secs : game_kept.mood >= PIP_STARSTRUCK ? 3.4f : 3.2f, game_kept.name);
        }
        game_kept.mood = -1;
    }
}
int pip_display_level(void) { return s.dark ? 0 : pw.dim ? 25 : 100; }

static void dead_lit(void) { pw.idle = 0, pw.dozed = pw.dim = false, s.dark = false, lit_line = __LINE__; }

static void light_up_at(int line)
{
    lit_line = line;
    if (pw.dozed) pet_event(&s.pet, PET_UNDOZE); /* the screen comes on: a nap ends, calmly */
    pw.idle = 0, pw.dozed = pw.dim = false;
    s.dark = false;
    s.dark_in = s.imu.face_down && FEAT(PIP_F_FACEDOWN) ? DARK_AGAIN_S : 0;
}

static void say_hi(float idle) /* a bold Pip (trust over trust_bold), looked at again after a while: hi! */
{
    const pet_tune_t *k = &pet_tune;
    if (s.pet.trust <= k->trust_bold || idle < 20 || s.pet.asleep || s.react_mood >= 0 || (s.hi_at > 0 && s.t - s.hi_at < 120)) return;
    if (frand(0, 1) >= (s.pet.trust - k->trust_bold) / (1 - k->trust_bold)) return;
    s.hi_at = s.t;
    if (vary_to(0, 1, 0.4f)) show_react(PIP_HELLO);
    else react(PIP_HAPPY, 1.6f, "says hi"), s.squash.v -= 3, blink();
}

void pip_seen(void)
{
    s.input_at = s.t;
    if (s.dead) { light_up(); return; } /* the screen lights; there is no one to see */
    float idle = pw.dozed ? 999 : pw.idle;
    light_up();
    s.phone_s = 0; /* attention for him: no reason to be jealous */
    int r = pet_event(&s.pet, PET_SEEN); /* back after hours alone: glad, or sulking if he felt left */
    if (r >= 0) {
        int m = vary_to(r, r == PIP_HURT ? PIP_HEARTBROKEN : PIP_HELLO, 0.35f);
        if (m >= PIP_XD) show_react(m);
        else react(r, 3.0f, r == PIP_HURT ? "sulks" : "glad you're back");
        s.greet_t = 1.2f;
    }
    else say_hi(idle);
}

/* timid (trust under trust_flinch): a finger coming down or a jolt may make him flinch - the lower, the likelier */
static bool flinch(void)
{
    const pet_tune_t *k = &pet_tune;
    if (s.pet.trust >= k->trust_flinch || s.pet.asleep || (s.flinch_at > 0 && s.t - s.flinch_at < 3)) return false;
    if (frand(0, 1) >= 0.6f * (k->trust_flinch - s.pet.trust) / k->trust_flinch) return false;
    s.flinch_at = s.t;
    react(PIP_AFRAID, 0.9f, "flinch"), s.squash.v += 4, s.tremble.v += 2;
    return true;
}

const void *pip_pet_state(size_t *size)
{
    *size = sizeof s.pet;
    return &s.pet;
}

bool pip_pet_restore(const void *data, size_t size)
{
    const pet_t *p = data;
    if (size != sizeof *p || p->version != PET_VERSION) return false;
    s.pet = *p;
    return true;
}

static int meet_from; /* what he showed when you came over: it colours the answer */
static void meet(void)
{
    if (!s.meet_pending) meet_from = s.shown; /* a second press while he is still looking keeps the first */
    pet_rouse(&s.pet, s.tz, PET_SEEN);        /* any attention may shake off his drowsiness, now and then */
    react(PIP_CURIOUS, 1.4f, "meet");
    s.meet_pending = true;
}

/* ---------- taps of the bottom key on his screen: each one is answered at once, a quick series builds up (hello,
   tickled, enough, angry, a spinning head), the series is blinked back, a slow even beat starts "repeat the rhythm" ---------- */
#define RH_MAX 6 /* the longest pattern he drums */
enum { RH_OFF, RH_GROOVE, RH_PLAY, RH_WAIT };
static struct {
    float last;                     /* s.t of the last tap */
    int n;                          /* quick taps in this series */
    float gaps[3];                  /* the last slow gaps: an even beat? */
    int ng;
    int count, counted;             /* blinking the series back: how many, how many done */
    float count_t;                  /* s to the next of those blinks */
    bool phew;                      /* a spinning head wears off into a "phew" */
    int rh;                         /* the rhythm game: RH_* */
    float beat, rh_t, side, first;  /* its beat (s); s into the phase; which way he bobs; when your answer began */
    float pat[RH_MAX], yours[RH_MAX]; /* his taps and yours, s from the first */
    int len, played, got;           /* the pattern's length, taps he has drummed, taps you have answered */
    bool scored;                    /* this game has counted as a game with him */
} tp = {.last = -10, .side = 1, .len = 3};

static void bob(float kick) /* on the beat: a hop, eyes to the other side */
{
    s.squash.v -= kick;
    tp.side = -tp.side;
    s.tgx = 0.45f * tp.side, s.tgy = -0.1f, s.idle_roll = 0.1f * tp.side;
    s.next_look = tp.beat > 0 ? tp.beat : 0.6f; /* no wandering gaze between beats */
}

static void rh_stop(void) { tp.rh = RH_OFF, tp.len = 3, tp.scored = false, tp.n = tp.ng = 0; }

static void rh_compose(void) /* a pattern on your beat: whole and half beats (and one and a half), never all alike */
{
    static const float mult[] = {1, 0.5f, 1, 1.5f, 0.5f};
    bool plain = true;
    tp.pat[0] = 0;
    for (int i = 1; i < tp.len; i++) {
        float m = mult[lv_rand(0, 4)];
        plain &= m == 1;
        tp.pat[i] = tp.pat[i - 1] + m * tp.beat;
    }
    if (plain) /* all whole beats is no pattern: halve one gap */
        for (int i = (int)lv_rand(1, (uint32_t)tp.len - 1); i < tp.len; i++) tp.pat[i] -= tp.beat * 0.5f;
    tp.played = 0;
}

static void rh_judge(void)
{
    bool ok = tp.got == tp.len;
    for (int i = 1; ok && i < tp.len; i++) {
        float his = tp.pat[i] - tp.pat[i - 1], yours = tp.yours[i] - tp.yours[i - 1];
        ok = fabsf(yours - his) <= fmaxf(0.12f, his * 0.3f);
    }
    pet_event(&s.pet, PET_MOVE);
    if (!ok) { react(PIP_SMUG, 2.5f, "wrong beat"); rh_stop(); return; }
    if (!tp.scored) tp.scored = true, pet_event(&s.pet, PET_PLAY); /* one game's worth of fun, however long it runs */
    if (tp.len >= RH_MAX) { react(PIP_LOVE, 3.0f, "rhythm master"); rh_stop(); return; }
    react(PIP_HAPPY, 1.4f, "right beat");
    tp.len++, tp.rh = RH_PLAY, tp.rh_t = -1.4f; /* a longer one, once he has cheered */
    rh_compose();
}

void pip_tap(void)
{
    if (pip_unmet()) return;
    s.input_at = s.t;
    const pet_tune_t *k = &pet_tune;
    float gap = s.t - tp.last;
    tp.last = s.t;
    bool quick = gap < k->tap_quick;
    tp.n = quick ? tp.n + 1 : 1;
    tp.count = 0; /* a new tap stops him counting the last series */
    if (tp.rh == RH_PLAY) return; /* he is drumming: hush */
    if (tp.rh == RH_WAIT) {       /* your answer */
        if (!tp.got) tp.first = s.t;
        tp.yours[tp.got++] = s.t - tp.first;
        bob(1.5f);
        if (tp.got == tp.len) rh_judge();
        return;
    }
    if (s.pet.asleep) { /* deep asleep: he stirs, and a few taps in a row wake him, not pleased */
        if (tp.n < (int)k->tap_wake) { blink(), s.squash.v -= 1.5f; return; }
        int r = pet_event(&s.pet, PET_WAKE);
        react(r == PIP_INDIGNANT ? r : vary_to(PIP_ANNOYED, PIP_GROGGY, 0.4f), 2.5f, "tapped awake");
        tp.n = 0;
        return;
    }
    /* a slow even beat: he picks it up and bobs along */
    if (!quick && gap < k->tap_slow) tp.gaps[0] = tp.gaps[1], tp.gaps[1] = tp.gaps[2], tp.gaps[2] = gap, tp.ng = tp.ng < 3 ? tp.ng + 1 : 3;
    else tp.ng = 0;
    if (tp.rh == RH_GROOVE) {
        if (fabsf(gap - tp.beat) < tp.beat * 0.35f) tp.beat = tp.beat * 0.7f + gap * 0.3f; /* follows you speeding up or slowing down */
        bob(3.0f);
        return;
    }
    if (tp.ng == 3) {
        float mean = (tp.gaps[0] + tp.gaps[1] + tp.gaps[2]) / 3;
        bool even = true;
        for (int i = 0; i < 3; i++) even &= fabsf(tp.gaps[i] - mean) <= mean * k->tap_even;
        if (even) {
            tp.rh = RH_GROOVE, tp.beat = mean, s.meet_pending = false;
            react(PIP_HAPPY, 1.0f, "groove");
            bob(3.0f);
            return;
        }
    }
    if (tp.n == 1) { meet(); return; } /* one tap: a hello, as ever */
    s.meet_pending = false;            /* more than a hello */
    if (pet_drowsy(&s.pet, s.tz)) {    /* drowsy: each tap of a series may rouse him (likelier every miss), grumbling */
        int up = pet_rouse(&s.pet, s.tz, PET_POKE);
        if (up >= 0) react(PIP_ANNOYED, 2.0f, "tapped awake"), tp.n = 0;
        else blink(), s.squash.v -= 1.5f;
        return;
    }
    /* how many taps it takes before "stop it" and "pestered": one more for Требовательный than the knobs, 3 more for
       Уравновешенный, 5 for Неприхотливый (the head spin past tap_spin stays: a kind one just gets giddy instead) */
    int more = (int)(1 + 4 * (pet_patience() - 1) + 0.5f), annoy_at = (int)k->tap_annoy + more, angry_at = (int)k->tap_angry + more;
    if (tp.n >= (int)k->tap_spin) { /* drummed silly: the head spins */
        if (tp.n == (int)k->tap_spin) react(PIP_DIZZY, 3.0f, "head spin"), tp.phew = true, s.squash.v -= 4;
        return;
    }
    if (tp.n >= angry_at && angry_at < (int)k->tap_spin) { /* past funny: angry, then hurt a while */
        if (tp.n == angry_at) react(pet_event(&s.pet, PET_PESTER), 2.5f, "pestered");
        else s.react_left = 2.5f;
        return;
    }
    if (tp.n >= annoy_at) { react(PIP_ANNOYED, 1.5f, "stop it"); return; }
    if (tp.n >= (int)k->tap_tickle) pet_event(&s.pet, PET_TICKLE), react(PIP_LAUGHING, 1.2f, "tickled"), s.squash.v -= 2.5f;
}

/* music on the mic (a beat that keeps time, mic.c), or the linked phone playing some (AMS: he does not hear it, so no
   tempo comes with it - he keeps his own, PHONE_BPM, unless the mic hears the song too): he dances - a hop, a nod and his
   eyes to the other side on every beat - while nothing else is going on (a reaction of its own plays out first). Music
   getting him going is a little fun and love, once until it stops: it cheers him up. The panel's "Музыка" off, or
   energy saving: neither. Not for ever: a bout of
   pet_tune.dance_s..1.6x s, then "danced out" and he sits out dance_rest..2x s before the next one if it still plays.
   Disturbed (a touch, a button, lifting him, any reaction of his own) he stops at once and leaves it for dance_break s.
   Asleep, dark or not listening: no. */
#define PHONE_BPM 112.0f
static float xs_rand(uint32_t *r, uint32_t seed, float a, float b);
static struct { float phase, left, rest, idle, own; int side; bool on, song, disco; uint32_t rng; } dn = {.side = 1};
/* in the mood? a cheerful one always, a sore, sad, frightened or poorly one never, the rest by how his day feels */
static float dance_chance(void)
{
    switch (s.mood) {
    case PIP_HAPPY: case PIP_LOVE: case PIP_EXCITED: case PIP_PLAYFUL: case PIP_HYPER: case PIP_LAUGHING: case PIP_CHEEKY:
    case PIP_SMITTEN: case PIP_STONED: case PIP_PROUD: case PIP_AMAZED: return 1;
    case PIP_SAD: case PIP_ANGRY: case PIP_ASLEEP: case PIP_SLEEPY: case PIP_NAUSEOUS: case PIP_SICK: case PIP_TIRED:
    case PIP_EXHAUSTED: case PIP_ANNOYED: case PIP_FURIOUS: case PIP_RAGE: case PIP_SWEARING: case PIP_BERSERK: case PIP_INDIGNANT:
    case PIP_BELLIGERENT: case PIP_HURT: case PIP_DISGUSTED: case PIP_AFRAID: case PIP_NERVOUS: case PIP_DESPONDENT:
    case PIP_GLOOMY: case PIP_DULL: case PIP_DISMAYED: case PIP_POUTY: case PIP_ZOMBIE: case PIP_FROZEN: case PIP_PINKEYE: return 0;
    default: return fmaxf(0, fminf(1, pet_tune.dance_will + pet_valence(&s.pet)));
    }
}
static void dance_step(float dt)
{
    bool heard = mic_on() && HEARS(PIP_F_MUSIC) && mic_music() > 0, played = HEARS(PIP_F_MUSIC) && phone_playing(); /* energy saving: no music at all */
    dn.own = fmodf(dn.own + dt * PHONE_BPM / 60, 1);
    float ph = heard ? mic_beat_phase() : dn.own, bpm = heard ? mic_bpm() : PHONE_BPM;
    bool music = (heard || played) && !s.pet.asleep && !s.dark && !unmet() && !s.washing && !away;
    bool dancing = s.react_mood >= 0 && !strcmp(s.react_name, "dancing");
    bool disturbed = pw.idle < dn.idle - 0.05f || (s.react_mood >= 0 && !dancing); /* the screen's idle time was reset: touched, pressed, lifted */
    /* music holds nothing: left lying untouched he dozes off on the sleep timer as ever, mid-song (s.dark stops him) */
    dn.idle = pw.idle;
    if (dn.rest > 0) dn.rest -= dt;
    if (!music) {
        if (dancing) s.react_left = fminf(s.react_left, 0.3f); /* the song stopped: so does he */
        dn.on = false, dn.phase = ph, dn.song = (mic_music() > 0 || phone_playing()) && dn.song;
        return;
    }
    if (dn.on && disturbed) { /* stopped mid-dance: whatever you did plays out, the dance waits */
        dn.on = false, dn.rest = pet_tune.dance_break;
        if (dancing) s.react_left = fminf(s.react_left, 0.1f);
    }
    if (dn.on && (dn.left -= dt) <= 0) { /* a good long dance: enough for now */
        dn.on = false, dn.rest = frand(pet_tune.dance_rest, 2 * pet_tune.dance_rest);
        if (vary_to(0, 1, 0.35f)) react(PIP_SINGING, 4.5f, "sings"); /* the tune has stayed with him */
        else react(PIP_RELIEVED, 1.6f, "danced out");
    }
    if (!dn.on && dn.rest <= 0 && s.react_mood < 0 && xs_rand(&dn.rng, 0xDA7CEu, 0, 1) >= dance_chance()) /* not in the mood */
        dn.rest = frand(pet_tune.dance_rest, 2 * pet_tune.dance_rest);
    else if (!dn.on && dn.rest <= 0 && s.react_mood < 0) {
        dn.on = true, dn.left = frand(pet_tune.dance_s, 1.6f * pet_tune.dance_s);
        dn.disco = vary_to(0, 1, 0.3f); /* this bout under a mirror ball */
        if (!dn.song) dn.song = true, pet_event(&s.pet, PET_TICKLE); /* a little fun when a song gets him going, once a song */
    }
    if (!dn.on) { dn.phase = ph; return; }
    react(dn.disco ? PIP_DISCO : PIP_DANCING, 1.0f, "dancing");
    if (ph < dn.phase - 0.5f) { /* the beat: the phase came round */
        dn.side = -dn.side;
        s.squash.v -= 3.2f, s.head_pitch.v += 2.6f;
        s.tgx = 0.35f * dn.side, s.tgy = -0.05f, s.idle_roll = 0.12f * dn.side;
        s.next_look = bpm > 0 ? 60 / bpm : 0.5f; /* no glances of his own between beats */
    }
    dn.phase = ph;
}

static void taps_step(float dt)
{
    float since = s.t - tp.last;
    if (tp.n > 0 && tp.rh == RH_OFF && since > pet_tune.tap_quick + 0.25f) { /* the series is over: counted back, 2 to 5 */
        if (tp.n >= 2 && tp.n <= 5 && !s.pet.asleep) tp.count = tp.n, tp.counted = 0, tp.count_t = 0.15f;
        tp.n = 0;
    }
    if (tp.n >= 2 || tp.count) s.next_blink = fmaxf(s.next_blink, 1.0f); /* no blinks of his own to muddle the count */
    if (tp.count) {
        if ((tp.count_t -= dt) <= 0) {
            blink(), s.squash.v -= 1.2f, tp.count_t = 0.4f;
            if (++tp.counted >= tp.count) tp.count = 0;
        }
    }
    if (tp.rh == RH_OFF) return;
    if (s.dark || s.pet.asleep) { rh_stop(); return; }
    tp.rh_t += dt;
    if (tp.rh == RH_GROOVE) {
        if (floorf(since / tp.beat) != floorf((since - dt) / tp.beat)) bob(2.0f); /* keeps the beat between your taps */
        react(PIP_HAPPY, 1.0f, "groove");
        if (since > tp.beat * 2.2f) { /* you stopped: his turn, if he has it in him to play */
            if (!pip_wants_play()) { react(PIP_CONTENT, 2.0f, "groove"); rh_stop(); return; }
            tp.rh = RH_PLAY, tp.rh_t = 0;
            rh_compose();
        }
    } else if (tp.rh == RH_PLAY) {
        if (tp.rh_t < 0) return; /* still cheering the last one */
        react(PIP_DETERMINED, 1.0f, "drums"); /* intent on getting it right */
        float t = tp.rh_t - 0.5f;
        while (tp.played < tp.len && t >= tp.pat[tp.played]) say(VOICE_TOCK), bob(3.5f), tp.played++;
        if (tp.played == tp.len && t > tp.pat[tp.len - 1] + 0.35f) tp.rh = RH_WAIT, tp.rh_t = 0, tp.got = 0;
    } else { /* RH_WAIT */
        react(PIP_CURIOUS, 1.0f, "your turn");
        if (!tp.got && tp.rh_t > 4) { react(PIP_BORED, 2.0f, "no answer"); rh_stop(); }
        else if (tp.got && since > tp.beat * 2.5f + 0.5f) rh_judge(); /* stopped short */
    }
}

static void sneeze_poked(void);
static void poke_answer(bool in_eye);
/* One of several answers to the same thing - a poke, a breath, a shake, a stroke - weighed by how he is (the caller sets the
   weights), the last one given less likely: never quite predictable. pet_tune.vary 0: always the first, the classic one */
static float life_rand(float a, float b);
static float affect_fit(int mood, float spread);
static int pick(const answer_t *a, int n, int *last)
{
    int k = 0;
    if (pet_tune.vary > 0 && n > 1 && life_rand(0, 1) < pet_tune.vary) {
        float tot = 0, w[12];
        for (int i = 0; i < n && i < 12; i++) /* and by how near each is to how he feels: a sad Pip does not giggle at a poke */
            w[i] = fmaxf(0, a[i].w) * (i == *last ? 0.3f : 1) * (i ? 0.15f + 0.85f * affect_fit(a[i].mood, 0.55f) : 1), tot += w[i];
        float x = life_rand(0, tot);
        for (k = 0; k < n - 1 && x >= w[k]; k++) x -= w[k];
    }
    *last = k;
    return k;
}
static void react(int mood, float secs, const char *name);
static int vary_to(int r, int alt, float p) { return pet_tune.vary > 0 && life_rand(0, 1) < p ? alt : r; } /* a face's other face now and then */
static int answer(const answer_t *a, int n, int *last)
{
    int k = pick(a, n, last);
    react(a[k].mood, a[k].secs, a[k].name);
    return k;
}
static bool rocking(void);
static void rock_feel(void);
static bool life_tap(float x, float y);
static void life_press(float x, float y);
static void life_swipe(uint32_t ms);
static void bday_sing(void);
static void bday_burst(bool poppers);
static void pip_sneeze(void);
static void pip_burp(void);
static void pip_hiccups(float in);
static void pip_toot(void);
/* quick pokes add 1 each (fading 0.4 a second): this many and he is fed up - ~8 quick ones for Требовательный, ~10
   Уравновешенный, ~12 Неприхотливый (was 5 for all: he is kind) */
#define POKE_FED_UP (3.0f + 4.5f * pet_patience())
/* how pestered he is just now, 0 .. 1 (fed up): what makes a "hey!" likely among his answers - not from the first touch */
static float irk(void) { return s.annoy / POKE_FED_UP; }
/* eye -1: a poke at the face (both eyes wince); 0/1: right in that eye, which screws up on its own */
static void poke_at(int eye)
{
    if (eye < 0 || s.pet.asleep) blink();
    if (s.pet.asleep) { /* woken up: grumpy unless he has slept enough */
        int r = pet_event(&s.pet, PET_WAKE);
        react(r, 2.5f, "woken up");
        return;
    }
    if (s.greet_t > 0) return; /* the touch that brought him back is not a poke */
    if (s.sneeze_anim >= 0) return; /* mid-sneeze: he does not even notice */
    pet_event(&s.pet, PET_POKE);
    int up = pet_rouse(&s.pet, s.tz, PET_POKE); /* drowsy: a poke may wake him up too, less surely than a shake */
    if (up >= 0) { react(up, 1.5f, "perked up"); return; }
    sneeze_poked(); /* poke him enough and his nose tickles */
    if ((s.annoy += 1) >= POKE_FED_UP) { /* about six quick pokes in a row; keep at it and he goes cross, then furious */
        int r = pet_event(&s.pet, PET_FED_UP);
        int m = r == PIP_RAGE ? vary_to(r, PIP_BURNING, 0.5f) : r == PIP_ANGRY || r == PIP_FURIOUS ? vary_to(r, PIP_FUMING, 0.3f) : r;
        if (m >= PIP_XD) show_react(m);
        else react(r, r == PIP_SWEARING || r == PIP_BERSERK ? 4.0f : 2.5f, "fed up");
        s.annoy = 0;
    } else if (eye >= 0 && eyes_open) { /* a scowl, a smile or half-shut eyes have nothing to screw up: a plain poke */
        s.squint_t[eye] = 0;
        if (s.t - s.last_voice > 0.4f) say(VOICE_POKE);
    } else {
        poke_answer(eye >= 0); /* on an eye it cannot screw up (smiling, half shut): still no giggle */
    }
}
static void poke(void) { poke_at(-1); }

/* which eye a tap landed on (with a little slack round it), or -1 */
static int eye_at(float x, float y)
{
    for (int i = 0; i < 2; i++) {
        const eye_t *e = &s.face.eye[i];
        float dx = (x - e->x) / (e->w * 0.5f + 8), dy = (y - e->y) / (e->h * 0.5f + 8);
        if (dx * dx + dy * dy <= 1) return i;
    }
    return -1;
}

void pip_meet(void) { meet(); }

void pip_key(char k)
{
    static const char keys[] = "1234567890-=";
    for (int i = 0; i < (int)sizeof keys - 1 && i < PIP_MOOD_COUNT; i++)
        if (k == keys[i]) { pip_set_mood(i); s.react_mood = -1; }
    if (k == ' ') { pip_set_mood(s.mood + 1); s.react_mood = -1; }
    if (k == 'b') blink();
    if (k == 'm') meet();
    if (k == 'f') pip_feed();
    if (k == 'z') pip_sneeze();
    if (k == 'x') pip_burp();
    if (k == 'h') pip_hiccups(0.3f); /* a run of hiccups */
    if (k == 't') pip_toot();       /* a toot */
    if (k == 'y') bday_sing();       /* the birthday's parts on their own (to try them out): the song */
    if (k == 'p') bday_burst(true);  /* a burst of poppers */
    if (k == 'r') bday_burst(false); /* a rain of confetti */
    if (k == 's') snow_start(SNOW_S); /* 1 January's snowfall */
}

/* swung side to side, about a second a swing (the tilt crossing over and back four times running): "wheee!" */
static struct { int side, flips; float last, jig_from; bool jig_told, jig_glad; } sw;
static void swing_step(void)
{
    float tx = s.imu.tilt_x;
    int side = tx > SWING_TILT ? 1 : tx < -SWING_TILT ? -1 : 0;
    if (s.t - sw.last > SWING_GAP_S) sw.flips = 0, sw.jig_from = 0, sw.jig_told = sw.jig_glad = false;
    if (!side || side == sw.side) return;
    float half = s.t - sw.last;
    if (sw.side && half < SWING_MIN_S) { /* jiggled side to side, too fast for a swing: a shake - by how hard and how long */
        sw.side = side, sw.last = s.t, sw.flips = 0;
        if (!sw.jig_from) sw.jig_from = s.t;
        float dur = s.t - sw.jig_from;
        bool on = !s.pet.asleep && !s.imu.face_down && !s.no_shakes && FEAT(PIP_F_SHAKE);
        /* measured on a hand (Nov 2026, imu energy): a light shake 0.25..0.35, a middling one 0.3..0.6, a hard one 1.7..2,
           a wild one ~4.5. A light one under SHAKE_G (no IMU_SHAKE at all) is fun once a bout; a middling one kept up past
           SHAKE_LONG_S, or a light one past SHAKE_LIGHT_LONG_S, makes him dizzy (a hard one is pip_imu's, at once) */
        if (on && !sw.jig_glad && !sw.jig_told && dur > 0.6f && !s.imu.shaking && s.imu.energy < SHAKE_G) {
            sw.jig_glad = true;
            pet_event(&s.pet, PET_TICKLE);
            static int last;
            const answer_t a[] = {{PIP_EXCITED, 2, "shaken: whee", 2.0f}, {PIP_LAUGHING, s.pet.fun > 0.5f ? 1.2f : 0.4f, "shaken: giggle", 1.6f},
                                  {PIP_HAPPY, 1, "shaken: light", 1.6f}, {PIP_JUGGLER, s.pet.fun > 0.5f ? 0.8f : 0.2f, "juggles", 3.2f}};
            answer(a, 4, &last);
        }
        if (on && !sw.jig_told && !s.shake_hard && ((dur > SHAKE_LONG_S && s.imu.energy > SHAKE_G * 0.7f) || dur > SHAKE_LIGHT_LONG_S)) {
            sw.jig_told = true;
            shaken_hard();
        }
        return;
    }
    sw.jig_from = 0;
    sw.flips = sw.side && half < SWING_GAP_S ? sw.flips + 1 : 0;
    sw.side = side, sw.last = s.t;
    if (sw.flips < 4 || rocking() || s.pet.asleep || s.imu.shaking || s.imu.face_down || s.no_shakes || !FEAT(PIP_F_SHAKE)) return;
    sw.flips = 0;
    pet_event(&s.pet, PET_TICKLE); /* a little fun */
    static int last;
    const answer_t a[] = {{PIP_EXCITED, 3, "swing", 2.5f}, {PIP_HAPPY, 1.2f, "swing", 2.5f}, {PIP_LAUGHING, s.pet.fun > 0.5f ? 1 : 0.3f, "swing", 2.5f}};
    answer(a, 3, &last);
}

/* shaken hard: dizzy, now and then knocked out at once (x x, stars round his head); kept up past twice SHAKE_LONG_S, more
   often than not knocked out (pip_imu) */
static void shaken_hard(void)
{
    if (vary_to(0, 1, 0.15f)) show_react(PIP_KNOCKOUT);
    else react(PIP_DIZZY, 3.0f, "shaken hard");
}

/* turned upside down and back, again and again: each turn counted, TUMBLE_N of them within TUMBLE_S is a tumble - giddy and
   glad of it (fun, PET_TUMBLED); kept at it, the fourth tumble running leaves him dizzy. A hop on every turn while it lasts */
static struct { float at[TUMBLE_N]; int n; float much; } tb;
static void tumble_turn(void)
{
    bool going = s.react_mood == PIP_TUMBLING;
    if (going) s.squash.v -= 3, s.react_left = fmaxf(s.react_left, 1.6f); /* a hop on every turn while it lasts */
    tb.at[tb.n++ % TUMBLE_N] = s.t;
    for (int i = 0; i < TUMBLE_N; i++)
        if (tb.at[i] <= 0 || s.t - tb.at[i] > TUMBLE_S) return;
    memset(tb.at, 0, sizeof tb.at);
    tb.much += 1; /* every full tumble counts, kept up or not; it fades in update, one in 40 s */
    if (tb.much > TUMBLE_MUCH) { tb.much = 0; react(PIP_DIZZY, 2.5f, "tumbled out"); return; }
    pet_event(&s.pet, PET_TUMBLED);
    if (going) return; /* still at it: no new sound */
    s.squash.v -= 5;
    react(PIP_TUMBLING, 2.4f, "tumbling");
}

/* debug `touchtrace 1`: what the finger is doing as touch_cb sees it, for tuning the tickle on real glass */
static int zone_at(float x, float y);
const char *pip_touch_trace(char *out, int n)
{
    float speed = s.finger_path * 4;
    snprintf(out, n, "%d %d %d %.0f %.0f %d %.2f %d %s", s.touching, (int)s.fx, (int)s.fy, speed, s.tk_spread, s.petting, s.tickle_t,
             zone_at(s.fx, s.fy), s.react_mood >= 0 && s.react_name ? s.react_name : "-");
    return out;
}
/* debug `imutrace 1`: how hard he is being shaken, for tuning the shake's lines on a real hand (debug.c prints `@I`) */
static float tr_emax, tr_amax;
const char *pip_imu_trace(float *energy, float *emax, float *amax, float *shake_t, int *shaking)
{
    *energy = s.imu.energy, *emax = tr_emax, *amax = tr_amax, *shake_t = s.imu.shaking ? s.imu.shake_t : 0, *shaking = s.imu.shaking;
    tr_emax = tr_amax = 0;
    return s.react_mood >= 0 && s.react_name ? s.react_name : "-";
}
void pip_imu(float ax, float ay, float az, float dt)
{
    if (s.dead) return; /* nothing to feel: no wake by motion, no steps */
    int ev = imu_feed(&s.imu, ax, ay, az, dt);
    float mag = sqrtf(ax * ax + ay * ay + az * az);
    if (mag > tr_amax) tr_amax = mag;
    if (s.imu.energy > tr_emax) tr_emax = s.imu.energy;
    if (s.imu.energy > SHAKE_G_ANY) s.shook_at = s.t; /* the mic hears the case rattle: update() ignores breath and bangs */
    /* picked up? gravity, smoothed, against how he lay when he dozed off - from samples that can be gravity (0.6..1.4 g):
       a jolt, a knock on the table or one bad sample cannot turn him over by itself; a hand lifting him can */
    float k = fminf(1, dt * 5), m2 = ax * ax + ay * ay + az * az;
    if (m2 > 0.36f && m2 < 1.96f) pw.lp[0] += (ax - pw.lp[0]) * k, pw.lp[1] += (ay - pw.lp[1]) * k, pw.lp[2] += (az - pw.lp[2]) * k;
    if (s.imu.energy > 0.35f) pw.idle = 0; /* handled, not just carried */
    if (s.imu.energy > 0.5f || (ev & ~(IMU_STEP | IMU_FACE_DOWN))) s.input_at = s.t; /* swung, shaken, turned: answers to you */
    bool jolt = s.imu.energy > 0.7f; /* timid: a sudden jolt makes him flinch */
    if (jolt && !s.jolted && !s.dark && !pip_unmet() && !ev && !away) flinch();
    s.jolted = jolt;
    if (pw.dozed && FEAT(PIP_F_WAKE)) { /* the panel's "Просыпается от движения": off, motion never lights him up */
        float d = pw.lp[0] * pw.g[0] + pw.lp[1] * pw.g[1] + pw.lp[2] * pw.g[2];
        float n = sqrtf((pw.lp[0] * pw.lp[0] + pw.lp[1] * pw.lp[1] + pw.lp[2] * pw.lp[2]) * (pw.g[0] * pw.g[0] + pw.g[1] * pw.g[1] + pw.g[2] * pw.g[2]));
        if (n > 0.1f && d / n < 0.906f) light_up(), picked_up(false); /* turned more than 25 degrees */
        else if (ev & IMU_SHAKE) light_up();         /* or shaken */
    }
    if (pip_unmet()) return; /* nothing wakes him but a button */
    /* the menu or an app on screen (a die shaken, the ball rolled, the 8 asked): he feels nothing of it - no shake, swing,
       tumble, fall or peekaboo; steps still count, and face down still puts the screen to sleep */
    bool upright = ev & IMU_UPRIGHT; /* seen before the masks: righted off his face, "upside down" still ends */
    if (away) ev &= IMU_STEP | IMU_FACE_DOWN | IMU_FACE_UP;
    /* "Просыпается от движения" off and the screen dark: motion stirs nothing - no face, no sound, not even turning him over
       out of a face-down sleep (a button or a touch it is) - but steps still count, silently */
    bool still = s.dark && !FEAT(PIP_F_WAKE);
    if (still) ev &= IMU_STEP;
    if (!FEAT(PIP_F_FACEDOWN)) ev &= ~(IMU_FACE_DOWN | IMU_FACE_UP);
    if (ev & IMU_FACE_DOWN) { react(PIP_ASLEEP, INFINITY, "face down"); pet_event(&s.pet, PET_SLEEP); s.dark_in = DARK_AFTER_S; }
    if (ev & IMU_FACE_UP) {
        s.dark = false, s.dark_in = 0, lit_line = __LINE__;
        int r = pet_event(&s.pet, PET_WAKE);
        react(vary_to(r >= 0 ? r : PIP_SURPRISED, PIP_GROGGY, 0.35f), 1.8f, "woke up");
        blink();
        if (r < 0 || mood_val[r] >= 0) picked_up(true); /* out of a pocket on a walk: the barbell */
    }
    if (ev & IMU_PEEK) { /* turned face down and straight back, under a second: peekaboo! */
        /* the face every time, the fun and love once in PEEK_GAP_S: face down and back over and over is no way to fill them */
        int r = s.t - peek_at < PEEK_GAP_S ? (s.pet.asleep ? -1 : PIP_PLAYFUL) : (peek_at = s.t, pet_event(&s.pet, PET_PEEKABOO));
        if (r == PIP_PLAYFUL) { /* the game: glad, but not always the same way */
            static int last;
            const answer_t a[] = {{PIP_PLAYFUL, 3, "peekaboo", 2.5f}, {PIP_LAUGHING, 1.2f, "peekaboo", 2.0f}, {PIP_SURPRISED, 0.8f, "peekaboo", 1.6f},
                                  {PIP_AMAZED, 0.5f, "peekaboo", 1.8f}, {PIP_PEEKING, 1.6f, "hides", 4.5f},
                                  {PIP_POPEYED, 0.8f, "eyes pop", 3.8f}};
            answer(a, 6, &last), blink();
        } else if (r >= 0) react(r, 2.5f, "peekaboo"), blink();
    }
    if ((ev & IMU_TURN) && FEAT(PIP_F_TUMBLE) && !s.pet.asleep) tumble_turn();
    if ((ev & IMU_UPSIDE) && !s.pet.asleep) s.upside_at = s.t, react(PIP_PUZZLED, INFINITY, "upside down"); /* the world turned over */
    if ((ev & IMU_UPRIGHT) && s.react_mood >= 0 && !strcmp(s.react_name, "upside down")) { /* the right way up again */
        if (s.t - s.upside_at >= RELIEF_S) react(PIP_RELIEVED, 2.0f, "phew");
        else s.react_left = 0;
    }
    else if (upright && s.react_mood >= 0 && !strcmp(s.react_name, "upside down")) { /* righted unseen: over, the relief when he is back */
        s.react_left = 0;
        s.phew_due = s.t - s.upside_at >= RELIEF_S;
    }
    if (ev & (IMU_FALL | IMU_THROW)) pet_soil(&s.pet, 0.04f); /* it picks up dirt where it lands */
    if (!FEAT(PIP_F_FALL)) ev &= ~(IMU_FALL | IMU_THROW); /* switched off on the panel: a fall is just a fall */
    bool low = pet_valence(&s.pet) < -0.3f || s.pet.love < 0.15f; /* upset already, or hardly loved */
    if ((ev & IMU_FALL) && !s.pet.asleep) { /* shocked, then hurt */
        int r = pet_event(&s.pet, PET_DROPPED), m = low ? vary_to(r, PIP_SOBBING, 0.55f) : vary_to(r, PIP_KNOCKOUT, 0.15f);
        if (m >= PIP_XD) show_react(m);
        else react(r, 2.0f, "dropped");
    }
    if ((ev & IMU_THROW) && !s.pet.asleep) { /* thrown: cross, and hurt after */
        int r = pet_event(&s.pet, PET_THROWN), m = low ? vary_to(r, PIP_SOBBING, 0.5f) : vary_to(r, PIP_HEARTBROKEN, 0.25f);
        if (m >= PIP_XD) show_react(m);
        else react(r, r == PIP_SWEARING || r == PIP_BERSERK ? 4.0f : 3.0f, "thrown");
        s.squash.v -= 6;
    }
    /* how hard: a light shake may be fun (whee, a giggle); a hard one (the smoothed shove over SHAKE_HARD_G) or one kept up
       past SHAKE_LONG_S always leaves him dizzy - told once a bout, even if a light start got a glad face */
    bool shake_on = !s.imu.face_down && !s.no_shakes && FEAT(PIP_F_SHAKE) && !s.pet.asleep && !still && !away;
    if (!s.imu.shaking) s.shake_hard = s.shake_ko = false;
    else if (shake_on && s.shake_hard && !s.shake_ko && s.imu.shake_t > 2 * SHAKE_LONG_S) { /* still at it: out for the count */
        s.shake_ko = true;
        if (s.react_mood != PIP_KNOCKOUT && vary_to(0, 1, 0.6f)) show_react(PIP_KNOCKOUT);
    }
    else if (shake_on && !s.shake_hard && !(ev & IMU_SHAKE) && (s.imu.energy > SHAKE_HARD_G || s.imu.shake_t > SHAKE_LONG_S)) {
        s.shake_hard = true, s.shakes = 0;
        shaken_hard();
    }
    if (ev & IMU_SHAKE) tb.n = 0; /* whatever "turns" the shake's first strokes made, they were no tumble */
    if ((ev & IMU_SHAKE) && !s.imu.face_down && !s.no_shakes && FEAT(PIP_F_SHAKE)) {
        pet_event(&s.pet, PET_SHAKE);
        int up = pet_rouse(&s.pet, s.tz, PET_SHAKE); /* drowsy: a shake may wake him up properly */
        if (s.imu.energy > SHAKE_HARD_G && !s.pet.asleep) { s.shake_hard = true, s.shakes = 0; shaken_hard(); }
        else if (up >= 0) { react(up, 1.5f, "perked up"); s.shakes = 0; }
        else if ((s.shakes += 1) >= 2.5f) { react(PIP_DULL, 40, "shaken silly"); s.shakes = 0; } /* about three in a row */
        else { /* shaken: giddy as a rule, but it depends on him */
            static int last;
            float f = s.pet.fun, tr = s.pet.trust;
            const answer_t a[] = {{PIP_DIZZY, 3, "dizzy", 3.0f}, {PIP_EXCITED, f > 0.5f ? 1.3f : 0.2f, "shaken: whee", 2.0f},
                                  {PIP_ANNOYED, s.pet.annoy > 0.5f * pet_patience() ? 1.2f : 0.15f, "shaken: hey", 1.6f}, {PIP_AFRAID, tr < 0.4f ? 1.2f : 0.2f, "shaken: scared", 2.0f},
                                  {PIP_LAUGHING, f > 0.6f ? 0.8f : 0.1f, "shaken: giggle", 1.5f}};
            answer(a, 5, &last);
        }
    }
    if (ev & IMU_STEP) {
        int r = pet_event(&s.pet, PET_STEP);
        if (r == PIP_PROUD) r = vary_to(vary_to(vary_to(r, PIP_STARSTRUCK, 0.3f), PIP_MEDAL, 0.4f), PIP_FIREWORKS, 0.2f);
        if (r >= PIP_XD && !s.pet.asleep && !still) show_react(r);
        else if (r >= 0 && !s.pet.asleep && !still) react(r, 3.0f, "walk goal"); /* the day's walk done: proud of himself */
    }
    if (!still && !away) rock_feel(), swing_step();
}

void pip_get_info(pip_info_t *o)
{
    *o = (pip_info_t){moods[s.shown].name, s.react_mood >= 0 ? s.react_name : "", (int)s.pet.steps_today,
                      s.imu.tilt_x, s.imu.tilt_y, s.imu.energy, s.imu.face_down, s.frame_ms,
                      s.pet.food, s.pet.energy, s.pet.fun, s.pet.love, s.pet.walk, s.pet.day_mood,
                      s.pet.asleep, s.auto_mood, pet_hour(s.pet.last, s.tz), pet_age_days(&s.pet, s.tz), s.pet.clean, s.washing, s.pet.trust,
                      pet_mood_level(&s.pet)};
}

/* ---------- scene effects: particles and sprites drawn over the face ---------- */
typedef struct { lv_obj_t *o; float x, y, vx, vy, life, max; } part_t;
#define N_SWEAR 7 /* swearing has one at a time; berserk a hail of them */
#define N_SNOW 7
#define N_SWEAT 2
static part_t swear[N_SWEAR], snow[N_SNOW], sweat[N_SWEAT];

static part_t *free_part(part_t *pool, int n)
{
    for (int i = 0; i < n; i++)
        if (pool[i].life <= 0) return &pool[i];
    return NULL;
}

static void launch(part_t *p, float x, float y, float vx, float vy, float life)
{
    *p = (part_t){p->o, x, y, vx, vy, life, life};
    if (p->o) lv_obj_remove_flag(p->o, LV_OBJ_FLAG_HIDDEN);
}

/* move and fade (no shrinking: a scaled object is drawn through a layer, slow on the board) */
static void animate(part_t *pool, int n, float dt, float gravity)
{
    for (int i = 0; i < n; i++) {
        part_t *p = &pool[i];
        if (p->life <= 0) continue;
        if ((p->life -= dt) <= 0) { if (p->o) lv_obj_add_flag(p->o, LV_OBJ_FLAG_HIDDEN); continue; }
        p->vy += gravity * dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        if (p->y > SCR + 10 || p->x < -10 || p->x > SCR + 10) { p->life = 0; if (p->o) lv_obj_add_flag(p->o, LV_OBJ_FLAG_HIDDEN); continue; } /* gone */
        if (!p->o) continue; /* drawn by its pool's own layer (the confetti) */
        float k = p->life / p->max, age = p->max - p->life;
        float opa = fminf(1, age / 0.12f) * fminf(1, k / 0.4f);
        lv_obj_set_style_opa(p->o, (lv_opa_t)(opa * 255), 0);
        lv_obj_set_pos(p->o, (int32_t)p->x - lv_obj_get_width(p->o) / 2, (int32_t)p->y - lv_obj_get_height(p->o) / 2);
    }
}

/* ---------- eye colours: picked in Настройки > Глаза or on the panel, kept in NVS by main.c ---------- */
static const struct { const char *name; uint32_t pupil, white; } eye_sets[PIP_EYES_COUNT] = {
    {"Стандартный", 0x1C1C1C, 0xFFFFFF}, /* #1C1C1C: a touch above black so a pupil at the rim still reads on AMOLED */
    {"Бабл-гам", 0xFFFFFF, 0xF542E6},     {"Снежок", 0x5878F3, 0xFFFFFF},  {"Карамель", 0xE8FCA5, 0xF673D9},
    {"Персик", 0xF76E5C, 0xFDFBE2},       {"Смузи", 0xCE0959, 0xFEECE0},   {"Лазурь", 0xFFFFFF, 0x03A9F7},
    {"Леди Баг", 0x1C1C1C, 0xC3110D},     /* #000000 asked; the same lifted black as the standard pupil */
    {"Петуния", 0xF006B5, 0xF7AFFB},      {"Золотая рыбка", 0xDF9109, 0xE0F4FB},
};
static int eyes_set;

void pip_eyes(int set)
{
    if (set < 0 || set >= PIP_EYES_COUNT) set = 0;
    eyes_set = set;
}
int pip_eyes_get(void) { return eyes_set; }

void pip_care(int level) { pet_level = level >= 0 && level < PET_LEVELS ? level : 1; }
int pip_care_get(void) { return pet_level; }
const char *pip_eyes_name(int set) { return set >= 0 && set < PIP_EYES_COUNT ? eye_sets[set].name : ""; }

/* Colours of a frame, worked out once per frame from the set and the mood's tint: nothing per pixel, the renderer paints
   any colour at the same cost. RGB in 0..1. */
typedef struct { float r, g, b; } rgbf_t;
static rgbf_t rgbf(uint32_t c) { return (rgbf_t){((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f}; }
static rgbf_t rgb_mix(rgbf_t a, rgbf_t b, float t) { return (rgbf_t){a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
static uint16_t rgb565(rgbf_t c)
{
    return lv_color_to_u16(lv_color_make((uint8_t)(fminf(fmaxf(c.r, 0), 1) * 255 + .5f), (uint8_t)(fminf(fmaxf(c.g, 0), 1) * 255 + .5f),
                                         (uint8_t)(fminf(fmaxf(c.b, 0), 1) * 255 + .5f)));
}
static float rgb_dist(rgbf_t a, rgbf_t b) /* rough perceptual distance, 0..1 */
{
    float dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
    return sqrtf(0.3f * dr * dr + 0.59f * dg * dg + 0.11f * db * db);
}
/* c drawn over bg must read: when the two come close, c slides to whichever of the alternatives stands out most */
static rgbf_t legible(rgbf_t c, rgbf_t bg, const uint32_t *alt, int n)
{
    const float need = 0.28f;
    float d = rgb_dist(c, bg);
    if (d >= need) return c;
    rgbf_t best = c;
    float bd = d;
    for (int i = 0; i < n; i++) {
        rgbf_t a = rgbf(alt[i]);
        float ad = rgb_dist(a, bg);
        if (ad > bd) bd = ad, best = a;
    }
    return rgb_mix(c, best, fminf(1, (need - d) / (need * 0.25f))); /* a short slide: no muddy middle for long */
}

/* The white: the set's colour under the mood's tint (springs in s.tint, white = no tint). Standard eyes take the tint as
   it is, as always. A coloured white takes a faint tint as light on it (multiplied: a hint of warmth, never washed out)
   and gives way to a strong one (pinkeye, nausea, gloom, rage) by how far that one is from white, so the illness reads
   on every set. Then the pupil, the heart, the veins and the tears are kept legible on whatever the white became. */
static void eye_colors(void)
{
    rgbf_t t = {fminf(fmaxf(s.tint[0].x, 0), 1), fminf(fmaxf(s.tint[1].x, 0), 1), fminf(fmaxf(s.tint[2].x, 0), 1)};
    rgbf_t w = rgbf(eye_sets[eyes_set].white), white = t;
    if (eyes_set) {
        float far = 1 - fminf(t.r, fminf(t.g, t.b)); /* 0 untinted .. ~0.8 rage */
        float k = fminf(1, fmaxf(0, (far - 0.1f) * 5));
        white = rgb_mix((rgbf_t){w.r * t.r, w.g * t.g, w.b * t.b}, t, k);
    }
    static const uint32_t ink[] = {0x1C1C1C, 0xFFFFFF};
    static const uint32_t hearts[] = {0xFF4479, 0xFFFFFF, 0xB0002A};
    static const uint32_t veins[] = {0xD8343A, 0x5A0A14, 0xFFB0B4};
    static const uint32_t water[] = {0xA8D8FF, 0xFFFFFF, 0x1E5AA8};
    s.face.color = rgb565(white);
    s.face.pupil_color = rgb565(legible(rgbf(eye_sets[eyes_set].pupil), white, ink, 2));
    s.face.heart_color = rgb565(legible(rgbf(hearts[0]), white, hearts + 1, 2));
    s.face.vein_color = rgb565(legible(rgbf(veins[0]), white, veins + 1, 2));
    s.face.water_color = rgb565(legible(rgbf(water[0]), white, water + 1, 2));
}

static void effects(int fx, float dt)
{
    if (fx != s.fx_shown) s.fx_shown = fx, s.spawn_t = 0;
    s.spawn_t -= dt;
    part_t *p;
    if (fx == FX_SWEAR && s.spawn_t <= 0 && (p = free_part(swear, N_SWEAR))) {
        /* grawlix out of where a mouth would be, drifting up and away */
        static const char glyphs[] = "%$#@!&*";
        static const uint32_t colors[] = {0xFF4A3A, 0xFFD23A, 0xFFFFFF, 0xFF8A2A};
        char t[2] = {glyphs[lv_rand(0, sizeof glyphs - 2)], 0};
        lv_label_set_text(p->o, t);
        lv_obj_set_style_text_color(p->o, lv_color_hex(colors[lv_rand(0, 3)]), 0);
        launch(p, SCR / 2 + frand(-35, 35), 372, frand(-55, 55), frand(-130, -90), frand(1.2f, 1.7f));
        s.spawn_t = 5.0f; /* one curse at a time, with its beep */
        if (s.t - s.last_voice > 1) say(VOICE_CENSOR);
    }
    if (fx == FX_RAGE && s.spawn_t <= 0 && (p = free_part(swear, N_SWEAR))) {
        /* berserk: grawlix bursting out all round his eyes, fast, and the tantrum over and over */
        static const char glyphs[] = "%$#@!&*?";
        static const uint32_t colors[] = {0xFF2A1A, 0xFFD23A, 0xFFFFFF, 0xFF8A2A, 0xFF4AD0};
        char t[2] = {glyphs[lv_rand(0, sizeof glyphs - 2)], 0};
        lv_label_set_text(p->o, t);
        lv_obj_set_style_text_color(p->o, lv_color_hex(colors[lv_rand(0, 4)]), 0);
        float side = lv_rand(0, 1) ? 1 : -1;
        launch(p, SCR / 2 + side * frand(60, 150), frand(110, 360), side * frand(60, 170), frand(-200, -60), frand(0.8f, 1.2f));
        s.spawn_t = frand(0.32f, 0.6f); /* two or three in the air: each one LVGL draws costs the eyes' frame rate */
        if (s.t - s.last_voice > 2.6f) say(VOICE_RANT);
    }
    if (fx == FX_SNOW && s.spawn_t <= 0 && (p = free_part(snow, N_SNOW))) { /* flakes drifting down past him */
        launch(p, frand(90, SCR - 90), -10, frand(-14, 14), frand(38, 62), frand(5.5f, 7.5f));
        s.spawn_t = frand(0.5f, 0.9f);
    }
    if (fx == FX_SWEAT && s.spawn_t <= 0 && (p = free_part(sweat, N_SWEAT))) { /* a drop runs down by one eye */
        const eye_t *e = &s.face.eye[lv_rand(0, 1)];
        float side = e->x < SCR / 2 ? -1 : 1;
        float x = e->x + side * e->w * 0.48f, y = e->y - e->h * 0.32f, dx = x - SCR / 2, dy = y - SCR / 2, d = sqrtf(dx * dx + dy * dy);
        if (d > 190) x = SCR / 2 + dx * 190 / d, y = SCR / 2 + dy * 190 / d; /* an eye near the rim: the drop stays on the glass */
        launch(p, x, y, side * 6, 14, 1.6f);
        s.spawn_t = frand(1.8f, 2.6f);
    }
    animate(swear, N_SWEAR, dt, 0);
    animate(snow, N_SNOW, dt, 0);
    animate(sweat, N_SWEAT, dt, 70);
}

/* ---------- the owner's birthday (the panel's "Дни рождения", off after a flash). On that day, by the real clock only, the
   first time his face is up and he is awake he sings "Happy Birthday" (VOICE_BIRTHDAY), excited, hopping to the tune, and
   party poppers go off from both lower corners twice (no words: his face and the confetti say it). All day the great
   palette (pet_festive) and, every bday_gap..2x minutes he is looked at, a burst of it: poppers or a confetti rain, a hop,
   a glad face (happy, excited or laughing), "ta-daa"; one burst in 1/bday_encore is the song again. A date set anew
   (not the one read at power-on) is greeted afresh. Confetti: small plain rectangles that only move and fade ---------- */
#define N_CONFETTI 28
#define BDAY_SHOW_S 5.0f
#define BDAY_HOP_S 0.46f /* a hop on every second note of the song, about */
static part_t confetti[N_CONFETTI];
/* The confetti is one object drawing all its pieces (not an object a piece): each piece then costs LVGL one small area a
   frame - where it was and is, together - instead of two areas every 4 ms tick. 28 pieces of separate objects ran past
   LVGL's 32 areas (a whole-screen redraw) and held core 0 from the eyes: the song ran at ~6 fps on the board. */
static struct {
    lv_obj_t *layer;
    lv_area_t shown[N_CONFETTI]; /* where each piece was drawn last (x2 < x1: nowhere) */
    uint8_t opa[N_CONFETTI];
    uint32_t color[N_CONFETTI];
    uint8_t w[N_CONFETTI], h[N_CONFETTI]; /* a piece's own size (0: the confetti's strips and squares) - the birth's dots */
    bool round;                           /* the pieces as round dots (the birth's star dust), not paper */
} confl;
static void cf_size(int i, int *w, int *h)
{
    if (confl.w[i]) { *w = confl.w[i], *h = confl.h[i]; return; }
    *w = i % 3 == 0 ? 8 : i % 3 == 1 ? 5 : 11, *h = i % 3 == 0 ? 8 : i % 3 == 1 ? 12 : 5;
}
static void cf_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = confl.round ? LV_RADIUS_CIRCLE : 1;
    for (int i = 0; i < N_CONFETTI; i++) {
        if (confl.shown[i].x2 < confl.shown[i].x1 || !confl.opa[i]) continue;
        d.bg_color = lv_color_hex(confl.color[i]), d.bg_opa = confl.opa[i];
        lv_draw_rect(layer, &d, &confl.shown[i]);
    }
}
/* the pieces where they are now: each one's old and new place as one area, and those near each other as one box (at
   most CF_BOXES: a burst flies as two jets, so a few compact boxes hold it; LVGL gets them as they are, not on tiles) */
#define CF_BOXES 8
static bool cf_painting;
static void cf_paint(void)
{
    if (!confl.layer) return;
    lv_area_t box[CF_BOXES];
    int nb = 0;
    for (int i = 0; i < N_CONFETTI; i++) {
        part_t *p = &confetti[i];
        lv_area_t was = confl.shown[i], now = {0, 0, -1, -1};
        uint8_t opa = 0;
        if (p->life > 0) {
            int w, h;
            cf_size(i, &w, &h);
            float k = p->life / p->max, age = p->max - p->life;
            opa = (uint8_t)(fminf(1, age / 0.12f) * fminf(1, k / 0.4f) * 255);
            now = (lv_area_t){(int32_t)p->x - w / 2, (int32_t)p->y - h / 2, 0, 0};
            now.x2 = now.x1 + w - 1, now.y2 = now.y1 + h - 1;
        }
        bool had = was.x2 >= was.x1, has = now.x2 >= now.x1;
        if (!had && !has) continue;
        if (had == has && was.x1 == now.x1 && was.y1 == now.y1 && was.x2 == now.x2 && was.y2 == now.y2 && confl.opa[i] == opa) continue;
        lv_area_t u = had && has ? (lv_area_t){LV_MIN(was.x1, now.x1), LV_MIN(was.y1, now.y1), LV_MAX(was.x2, now.x2), LV_MAX(was.y2, now.y2)}
                    : had ? was : now;
        confl.shown[i] = now, confl.opa[i] = opa;
        int best = -1;
        int32_t best_cost = 0;
        for (int b = 0; b < nb; b++) { /* the box it grows least (px added beyond the two areas) */
            lv_area_t j = {LV_MIN(box[b].x1, u.x1), LV_MIN(box[b].y1, u.y1), LV_MAX(box[b].x2, u.x2), LV_MAX(box[b].y2, u.y2)};
            int32_t cost = (int32_t)lv_area_get_size(&j) - (int32_t)lv_area_get_size(&box[b]) - (int32_t)lv_area_get_size(&u);
            if (best < 0 || cost < best_cost) best = b, best_cost = cost;
        }
        if (best >= 0 && (nb == CF_BOXES || best_cost < 1500)) /* close enough (or no box left): joined */
            box[best] = (lv_area_t){LV_MIN(box[best].x1, u.x1), LV_MIN(box[best].y1, u.y1), LV_MAX(box[best].x2, u.x2), LV_MAX(box[best].y2, u.y2)};
        else box[nb++] = u;
    }
    cf_painting = true; /* fx_tiles leaves these as they are */
    for (int b = 0; b < nb; b++) lv_obj_invalidate_area(confl.layer, &box[b]);
    cf_painting = false;
}
static void sp_paint(void) { cf_painting = true, sprite_paint(), cf_painting = false; } /* its boxes as they are, not on tiles */
static struct { uint8_t day, month; bool clock, set; float pop2_t, next_s, hop_t, hops_left; } bd = {.clock = true, .pop2_t = -1};

static void bday_next(float least) { bd.next_s = frand(least, 2) * pet_tune.bday_gap * 60; }

void pip_birthday(int day, int month)
{
    static const uint8_t mdays[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool ok = month >= 1 && month <= 12 && day >= 1 && day <= mdays[month - 1]; /* 31.04 would never come */
    uint8_t d = (uint8_t)(ok ? day : 0), m = (uint8_t)(ok ? month : 0);
    if (bd.set && (d != bd.day || m != bd.month) && d) s.pet.bday_greeted = 0, pet_dirty = true; /* a new date: sung for anew */
    bd.day = d, bd.month = m, bd.set = true;
    pet_festive = bday_today();
    bday_next(0.5f); /* already greeted today: the first burst some minutes on, not at once */
}
bool pip_birthday_get(int *day, int *month)
{
    if (day) *day = bd.day;
    if (month) *month = bd.month;
    return bd.day != 0;
}
void pip_clock_known(bool known) { bd.clock = known; }

static bool bday_today(void) /* 29 February is kept on the 28th in other years */
{
    if (!bd.day || !bd.clock || unmet() || s.dead) return false;
    int y, m, d;
    pet_date(s.pet.last, s.tz, &y, &m, &d);
    bool leap = y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
    return m == bd.month && d == (bd.month == 2 && bd.day == 29 && !leap ? 28 : bd.day);
}

static void confetti_go(bool poppers) /* poppers: all of it shot up from the lower corners; else a light rain from the top */
{
    static const uint32_t colors[] = {0xFF4D6D, 0xFFD23F, 0x3EC1FF, 0x7CF06B, 0xC77DFF, 0xFF9F1C};
    confl.round = false; /* paper again, if the birth's dust had them */
    for (int i = 0; i < N_CONFETTI; i++) {
        part_t *p = &confetti[i];
        if (!poppers && (p->life > 0 || i % 2)) continue; /* the rain: half of it, the pieces not flying already */
        confl.color[i] = colors[i % 6], confl.w[i] = confl.h[i] = 0;
        if (poppers) {
            float side = i % 2 ? 1 : -1; /* from (90, 380) and (376, 380): inside the round, low */
            launch(p, SCR / 2 + side * 143 + frand(-8, 8), 380, -side * frand(40, 200), -frand(380, 540), frand(1.8f, 2.5f));
        } else launch(p, frand(80, SCR - 80), -8, frand(-30, 30), frand(40, 110), frand(2.4f, 3.2f));
    }
}

/* ---------- his birth: the first waking after a clean flash, once in his life, half a minute of it. First the sky (cosmos.c:
   stars, a falling one, the spark and its heartbeat, the galaxy, the dark, the bang; his eyes not drawn meanwhile: the
   canvas is hidden), then him: two gold slits out of the fading light, stirring, one eye first; the world, stars in his
   eyes; a butterfly on his nose; you; fireworks (the sprites' and the confetti's); "hello" and hearts. A chord runs under
   all of it (voice_pad), and his own sounds over it. ---------- */
#define HATCH_EYES (COSMOS_END - 0.1f) /* out of the fading light: his eyes, shut */
#define HATCH_E(x) (HATCH_EYES + (x))
#define HATCH_END HATCH_E(16.4f)       /* "hello": he loves you */
static const uint32_t hatch_dust[5] = {0xFFE7A3, 0xA9E4FF, 0xFFB8E0, 0xFFFFFF, 0xCDB8FF};
static void hatch_burst(float x, float y, int n, int hue) /* a firework: sprites flung round, falling, dying out */
{
    static const uint32_t c[4][3] = {{0xFFD23F, 0xFFF0B0, 0xFF9F1C}, {0xFF7FB0, 0xFFC0DC, 0xFF4D8D}, {0x7FD8FF, 0xCFF2FF, 0x3EA8FF}, {0xB48CFF, 0xE0D0FF, 0x8CFFC8}};
    for (int i = 0; i < n; i++) {
        float a = i * 6.2832f / n + 0.2f, v = 120 + 50 * (i % 2);
        sprite_t *p = sprite_go(i % 3 == 0 ? SP_STAR : i % 3 == 1 ? SP_SPARK : SP_HEART, i % 3 == 2 ? 0 : 1, x, y, v * cosf(a), v * sinf(a) - 40, 0.85f, c[hue & 3][i % 3]);
        if (p) p->ay = 190;
    }
}
static void hatch_dots(float x, float y, int hue) /* ...of round dots */
{
    static const uint32_t c[2][4] = {{0x7FD8FF, 0xFFFFFF, 0x3EA8FF, 0xB0F0FF}, {0xB48CFF, 0x8CFFC8, 0xFFE27A, 0xFF8FC0}};
    confl.round = true;
    for (int i = 0; i < 18; i++) {
        float a = i * 2.39996f, v = 70 + 120 * (i % 4) / 3.0f;
        confl.w[i] = confl.h[i] = (uint8_t)(6 + i % 3 * 2), confl.color[i] = c[hue][i % 4];
        launch(&confetti[i], x, y, v * cosf(a), v * sinf(a) - 110, 0.85f);
    }
}
static void hatch_pad(const float *hz, int n, float level, float secs) { voice_pad(hz, FEAT(PIP_F_VOICE) && !covered && !away ? n : 0, level, secs); }
static void hatch_fx(float t, float t0)
{
#define AT(x) (t0 < (x) && t >= (x))
    static const float am[] = {220, 329.6f, 440, 523.3f}, f[] = {349.2f, 440, 523.3f, 659.3f}, g[] = {392, 523.3f, 587.3f, 784},
                       gup[] = {392, 493.9f, 587.3f, 784, 987.8f}, big[] = {261.6f, 392, 523.3f, 659.3f, 784}, soft[] = {349.2f, 440, 523.3f},
                       cm[] = {329.6f, 392, 523.3f};
    if (AT(0.4f)) hatch_pad(am, 4, 0.16f, 3.5f);
    if (AT(0.55f)) say(VOICE_SKYLIT);
    if (AT(COSMOS_STAR)) say(VOICE_SHOOTING);
    if (AT(COSMOS_SPARK)) say(VOICE_SPARK), hatch_pad(f, 4, 0.2f, 1.2f);
    for (int k = 0; k < COSMOS_BEATS; k++) if (AT(cosmos_beat[k] - 0.02f)) say(VOICE_HEART);
    if (AT(cosmos_beat[2])) hatch_pad(g, 4, 0.24f, 1.5f);
    if (AT(COSMOS_GALAXY)) say(VOICE_STARDUST);
    if (AT(COSMOS_GALAXY + 1.35f)) say(VOICE_RISER), hatch_pad(gup, 5, 0.42f, 2.6f);
    if (AT(COSMOS_COLLAPSE)) hatch_pad(NULL, 0, 0, 0.18f);            /* out: not a sound */
    if (AT(COSMOS_BANG)) say(VOICE_BLOOM), hatch_pad(big, 5, 0.55f, 0.15f);
    if (AT(COSMOS_BANG + 1.2f)) say(VOICE_GRAND), hatch_pad(big, 5, 0.32f, 2.5f);
    if (AT(HATCH_EYES)) s.squash.v -= 6;
    if (AT(HATCH_E(3.2f))) hatch_pad(soft, 3, 0.16f, 3);
    if (AT(HATCH_E(5.6f))) hatch_pad(cm, 3, 0.18f, 1.5f);
    if (AT(HATCH_E(6.6f))) hatch_pad(soft, 3, 0.14f, 2);
    if (AT(HATCH_E(10.0f))) hatch_pad(f, 4, 0.18f, 1.5f);
    if (AT(HATCH_E(12.0f))) hatch_pad(big, 5, 0.34f, 0.4f);
    if (AT(HATCH_END)) hatch_pad(NULL, 0, 0, 3);
    if (t > HATCH_E(0.6f) && t < HATCH_E(3.4f) && fmodf(t, 0.4f) < fmodf(t0, 0.4f)) { /* the last of the light drifting down on him */
        sprite_t *p = sprite_go(SP_SPARK, (int)frand(0, 1.99f), frand(70, SCR - 70), frand(20, 90), frand(-10, 10), frand(34, 60), 2.8f, hatch_dust[(int)frand(0, 4.99f)]);
        if (p) p->sway = 22, p->ph = frand(0, 6);
    }
    /* fireworks for him: the pictures' burst and the round dots' by turns, one dying out as the next goes up (two at once
       are areas LVGL copies the canvas under: the eyes' frames dropped), and the poppers last */
    if (AT(HATCH_E(12.05f))) hatch_burst(130, 96, 10, 0);
    if (AT(HATCH_E(12.85f))) hatch_dots(336, 100, 0);
    if (AT(HATCH_E(13.65f))) hatch_burst(233, 76, 10, 1);
    if (AT(HATCH_E(14.4f))) hatch_dots(140, 104, 1);
    if (AT(HATCH_E(15.15f))) hatch_burst(326, 92, 10, 2);
    if (AT(HATCH_END)) /* he loves you: hearts */
        for (int i = 0; i < 9; i++) {
            sprite_t *p = sprite_go(SP_HEART, i % 4 == 0 ? 3 : 1, 80 + i * 38, 420 + (i % 3) * 24, 0, -70 - 22 * (i % 4), 3.4f, i % 2 ? 0xFF4479 : 0xFF8FB5);
            if (p) p->sway = 24, p->ph = i;
        }
#undef AT
}

static void bday_sing(void) /* the song, excited, hopping to it, two pairs of poppers */
{
    react(PIP_EXCITED, BDAY_SHOW_S, "birthday");
    s.squash.v -= 6; /* a hop at once */
    bd.hop_t = BDAY_HOP_S, bd.hops_left = 6;
    confetti_go(true), bd.pop2_t = 1.6f; /* and a second pair as the tune goes on */
}

static void bday_burst(bool poppers) /* the day's bursts: poppers or a rain, a hop, a glad face */
{
    static const int glad[] = {PIP_HAPPY, PIP_EXCITED, PIP_LAUGHING};
    confetti_go(poppers);
    react(glad[lv_rand(0, 2)], 2.0f, "festive");
    s.squash.v -= 5;
}

/* ---------- a snowfall (1 January): a few minutes apart, ~SNOW_S of flakes drifting down over his face - the sprites' own
   flying snowflakes, one every SNOW_EVERY s, three or four at once, the small 18 px one only, melting away above his eyes
   (SNOW_LIFE). Measured on the board (Nov 2026): flakes over the whole height cost a plain face ~29.5 -> ~24.7 fps (each
   frame streamed ~330 rows against ~200), however few; kept in the top band they stay cheap. Not all day, and never over
   a dark screen, the setup screen or off his face ---------- */
#define SNOW_EVERY 0.9f
#define SNOW_LIFE 2.6f /* s: from the rim down to ~120 px, faded out over its last third - above the eyes */
static struct { float left, next; } ny_snow;
static void snow_start(float secs) { ny_snow.left = secs, ny_snow.next = 0; }
static void snow_step(float dt)
{
    if (ny_snow.left <= 0) return;
    if (s.dark || covered || away || unmet()) { ny_snow.left = 0; return; }
    ny_snow.left -= dt;
    if ((ny_snow.next -= dt) > 0) return;
    ny_snow.next = SNOW_EVERY * frand(.7f, 1.3f);
    sprite_t *p = sprite_go(SP_FLAKE, 0, frand(110, SCR - 110), -6, frand(-5, 5), frand(42, 52), SNOW_LIFE * frand(.9f, 1.1f), frand(0, 1) < .5f ? 0xEAF4FF : 0xFFFFFF);
    if (p) p->sway = frand(14, 22), p->ph = frand(0, 6.28f);
}

/* ---------- 1 January, by the real clock only (a clock never set says nothing of the date): the first time that day his face
   is up and he is awake (never waking him) he shows the New Year tree or the wreath; then, every NY_GAP_MIN..2x minutes he is
   looked at, a burst: mostly a snowfall over whatever face he has (snow_start), now and then one of the two scenes (they
   bring their own snow). Not all day long: a few seconds every quarter to half an hour. 2..10 January: only a snowfall
   (ny_late), one a day, two on NY_LATE_TWO of the days. The day greeted and the snowfalls still due are kept beside the pet (pip_ny_state, NVS "ny"): a
   field in pet_t would change the save's size and lose the pet on the update ---------- */
#define NY_GAP_MIN 15.0f /* 1 January: a burst every 15..30 minutes his face is seen */
#define NY_SCENE 0.35f    /* a burst is one of the two scenes this often, else a snowfall (a snowfall costs a plain face
                             ~30 -> ~24 fps on the board while it lasts: kept rare) */
#define NY_LATE_TWO 0.3f  /* 2..10 January: one snowfall every day, two on this share of the days */
/* 2..10 January: a snowfall every day, now and then two (NY_LATE_TWO) - their times drawn each morning (NY_LATE_FROM..TO
   h by his clock); come due, it falls the next time his face is seen (not made up for later if the day ends first) */
#define NY_LATE_FROM 9
#define NY_LATE_TO 21
/* what is kept across restarts (pip_ny_state; main.c NVS "ny") - next_s, the gap on the 1st, is not */
typedef struct { int32_t greeted, planned, due[2], n_due; } ny_keep_t;
static ny_keep_t nyk = {.greeted = -1, .planned = -1};
static bool ny_dirty;
static float ny_next_s;
const void *pip_ny_state(size_t *size) { *size = sizeof nyk; return &nyk; }
bool pip_ny_restore(const void *data, size_t size)
{
    if (size != sizeof nyk) return false;
    memcpy(&nyk, data, sizeof nyk);
    if (nyk.n_due < 0 || nyk.n_due > 2) nyk.n_due = 0;
    return true;
}
bool pip_ny_changed(void) { bool c = ny_dirty; ny_dirty = false; return c; }
static int ny_day(void) /* the day of January by the real clock, or 0 */
{
    if (!bd.clock || unmet() || s.dead) return 0;
    int y, m, d;
    pet_date(s.pet.last, s.tz, &y, &m, &d);
    return m == 1 ? d : 0;
}
static int32_t ny_sod(void) { int64_t l = s.pet.last + (int64_t)s.tz * 60; return (int32_t)(((l % 86400) + 86400) % 86400); } /* local s of the day */
static void ny_late(int32_t today)
{
    if (nyk.planned != today) { /* this day's one or two, at random times by day */
        nyk.planned = today, nyk.n_due = frand(0, 1) < NY_LATE_TWO ? 2 : 1, ny_dirty = true;
        for (int i = 0; i < nyk.n_due; i++) nyk.due[i] = (int32_t)frand(NY_LATE_FROM * 3600, NY_LATE_TO * 3600);
        if (nyk.n_due == 2 && nyk.due[1] < nyk.due[0]) { int32_t x = nyk.due[0]; nyk.due[0] = nyk.due[1], nyk.due[1] = x; }
    }
    if (nyk.n_due > 0 && s.react_mood < 0 && ny_snow.left <= 0 && ny_sod() >= nyk.due[0]) {
        snow_start(SNOW_S);
        nyk.due[0] = nyk.due[1], nyk.n_due--, ny_dirty = true;
    }
}
static void ny_scene(void) { show_react(frand(0, 1) < .5f ? PIP_NEWTREE : PIP_WREATH); }
static void ny_step(float dt)
{
    int day = ny_day();
    if (day < 1 || day > 10 || s.dark || covered || s.pet.asleep || s.intro >= 0 || s.hatch >= 0 || s.washing || pet_tune.vary <= 0
        || lv_screen_active() != lv_obj_get_screen(s.canvas)) return; /* only on his face, awake and seen: never wakes him */
    int32_t today = pet_local_day(s.pet.last, s.tz);
    if (day > 1) { ny_late(today); return; }
    if (nyk.greeted != today) { /* the first time today */
        if (s.react_mood >= 0) return; /* after what is showing now */
        nyk.greeted = today, ny_dirty = true, ny_next_s = frand(1, 2) * NY_GAP_MIN * 60;
        ny_scene();
        return;
    }
    if (s.react_mood < 0 && ny_snow.left <= 0 && (ny_next_s -= dt) <= 0) {
        ny_next_s = frand(1, 2) * NY_GAP_MIN * 60;
        if (frand(0, 1) < NY_SCENE) ny_scene();
        else snow_start(SNOW_S);
    }
}

static void bday_step(float dt)
{
    animate(confetti, N_CONFETTI, dt, 420);
    snow_step(dt);
    ny_step(dt);
    if (bd.pop2_t >= 0 && (bd.pop2_t -= dt) < 0) confetti_go(true);
    if (bd.hops_left > 0 && (bd.hop_t -= dt) <= 0) {
        if (s.react_name && !strcmp(s.react_name, "birthday")) s.squash.v -= 4, s.head_roll.v += frand(-0.8f, 0.8f), bd.hop_t = BDAY_HOP_S, bd.hops_left--;
        else bd.hops_left = 0; /* crowded out: the dance stops with it */
    }
    if (!pet_festive || s.dark || covered || s.pet.asleep || s.intro >= 0 || s.hatch >= 0 || s.washing
        || lv_screen_active() != lv_obj_get_screen(s.canvas)) return; /* only on his face, awake and seen: never wakes him */
    if (s.pet.bday_greeted != pet_local_day(s.pet.last, s.tz)) { /* the first time today */
        s.pet.bday_greeted = pet_local_day(s.pet.last, s.tz), pet_dirty = true; /* once today, even across a restart */
        bday_sing(), bday_next(1);
        return;
    }
    if (s.react_mood < 0 && (bd.next_s -= dt) <= 0) { /* now and then all day: a burst of it */
        bday_next(1);
        if (frand(0, 1) < pet_tune.bday_encore) { bday_sing(); return; } /* now and then the song once more */
        bday_burst(frand(0, 1) < 0.5f);
    }
}

/* ---------- feeding (hold the bottom key): a biscuit drops in, he watches it, snaps it up and munches; crumbs fly.
   A dozen small plain objects that only move and fade (no scaling, no layers): nothing for the frame rate. ---------- */
#define N_CRUMBS 8
#define FEED_DROP 0.55f /* s for the biscuit to fall to his mouth */
#define FEED_LEN 1.6f   /* the whole bite, chewing included */
#define MOUTH_Y 350
static part_t crumbs[N_CRUMBS];
static lv_obj_t *biscuit;
static float feed_t = -1; /* s into the bite; < 0: not eating */
static bool feed_no;      /* this biscuit is turned away: it comes a little way down and goes back up */
#define REFUSE_DOWN 0.35f
#define REFUSE_LEN 0.85f

static float sneeze_rand(float a, float b), burp_rand(float a, float b);
static float xs_rand(uint32_t *r, uint32_t seed, float a, float b);
static float grub(void) /* how grubby, for burps: 0 at dust_line of clean or cleaner .. 1 filthy */
{
    float line = pet_tune.dust_line;
    return fmaxf(0, fminf(1, (line - s.pet.clean) / line));
}
static void feed_bite(void) /* the biscuit reaches him: the pet eats, the face says how it went */
{
    lv_obj_add_flag(biscuit, LV_OBJ_FLAG_HIDDEN);
    int r = pet_event(&s.pet, PET_FEED);
    pet_rouse(&s.pet, s.tz, PET_SEEN);
    if ((r == PIP_GRATEFUL || r == PIP_CONTENT) && pet_cold(&s.pet)) { /* still cross: he eats it, and no thank you */
        react(s.pet.grudge > 0.6f ? PIP_POUTY : PIP_SKEPTICAL, 2.4f, "sulky bite");
        s.squash.v -= 2.0f;
        return;
    }
    if (r == PIP_CONTENT || r == PIP_HAPPY || r == PIP_GRATEFUL) { /* munching it, an ice cream (in summer more), a bounce */
        int month = pet_month(s.pet.last, s.tz);
        r = vary_to(vary_to(r, PIP_YUMMY, 0.45f), PIP_ICECREAM, month >= 6 && month <= 8 ? 0.35f : 0.2f);
        if (r == PIP_HAPPY) r = vary_to(r, PIP_BOUNCY, 0.35f);
    }
    if (r == PIP_NAUSEOUS) r = vary_to(r, PIP_QUEASY, 0.5f); /* too much: now and then it nearly comes back up */
    if (r >= PIP_XD) { show_react(r), s.squash.v -= 3.0f; goto burp; }
    react(r, r == PIP_YUMMY ? 2.6f : 2.4f, r == PIP_NAUSEOUS ? "too full" : r == PIP_DISGUSTED ? "enough" : r == PIP_GRATEFUL ? "thanks" : "yum");
    s.squash.v -= 3.0f; /* the snap */
burp:
    /* stuffed: a burp is coming, for sure; an ordinary biscuit now and then too, the grubbier the likelier (his own dice:
       no one else's shift) */
    if (r == PIP_QUEASY) s.burp_due = 0; /* the scene has its own retch: no burp from an earlier biscuit over it */
    else if (r == PIP_NAUSEOUS || r == PIP_DISGUSTED) s.burp_due = 1.4f;
    else if (burp_rand(0, 1) < 0.25f + (pet_tune.burp_dirty - 0.25f) * grub()) s.burp_due = 2.6f;
    else if (pet_tune.vary > 0) { /* or the biscuit goes down the wrong way: hiccups; or it comes out the other end */
        float r2 = xs_rand(&toot.rng, 0x7007F00Du, 0, 1);
        if (r2 < 0.12f) pip_hiccups(2.2f);
        else if (r2 < 0.22f) toot.due = 2.8f;
    }
}

void pip_feed(void)
{
    if (pip_unmet()) return;
    s.input_at = s.t;
    if (feed_t >= 0) return; /* one bite at a time */
    if (s.pet.asleep) { poke(); return; } /* food does not wake him gently: grumpy, and no snack */
    feed_t = 0;
    lv_obj_set_pos(biscuit, SCR / 2 - 17, -40);
    lv_obj_remove_flag(biscuit, LV_OBJ_FLAG_HIDDEN);
    if ((feed_no = pet_refuses_food(&s.pet))) { /* full and he said so: "no", or angry by now; nothing is eaten */
        int r = pet_event(&s.pet, PET_FEED);
        react(r, r == PIP_SWEARING || r == PIP_BERSERK ? 4.0f : 2.4f, r == PIP_NO ? "no more food" : "stop feeding");
        return;
    }
    int no = pet_spurns(&s.pet, true);
    if ((feed_no = no >= 0)) { /* still cross with you: he turns his nose up at it */
        react(no, 2.6f, "sulky: no food");
        return;
    }
    pip_glance(SCR / 2, MOUTH_Y, FEED_DROP + 0.2f); /* eyes on the food */
    react(vary_to(PIP_SURPRISED, PIP_EAGER, s.pet.food < 0.8f ? 0.85f : 0.4f), FEED_DROP, "food"); /* here it comes: eager */
}

static void feed_step(float dt)
{
    if (feed_t >= 0) {
        float t0 = feed_t;
        feed_t += dt;
        if (s.dark) { /* the screen went off mid-bite: eat at once, no show */
            if (t0 < FEED_DROP && !feed_no) feed_bite();
            lv_obj_add_flag(biscuit, LV_OBJ_FLAG_HIDDEN);
            feed_t = -1;
        } else if (feed_no) { /* comes into sight, stops short, goes back up: he will not have it */
            float k = feed_t < REFUSE_DOWN ? feed_t / REFUSE_DOWN : 1 - (feed_t - REFUSE_DOWN) / (REFUSE_LEN - REFUSE_DOWN);
            k = fmaxf(0, k);
            lv_obj_set_pos(biscuit, SCR / 2 - 17, (int32_t)(-40 + 100 * k * (2 - k))); /* eases in and out */
            if (feed_t >= REFUSE_LEN) lv_obj_add_flag(biscuit, LV_OBJ_FLAG_HIDDEN), feed_t = -1;
        } else if (feed_t < FEED_DROP) {
            float k = feed_t / FEED_DROP; /* falls faster and faster */
            lv_obj_set_pos(biscuit, SCR / 2 - 17, (int32_t)(-40 + (MOUTH_Y + 23) * k * k));
        } else {
            if (t0 < FEED_DROP) {
                feed_bite();
                static const uint32_t ink[] = {0xD9A35B, 0xB97A3A, 0xE8C080};
                for (int i = 0; i < N_CRUMBS; i++) { /* crumbs burst up and out, then drop */
                    lv_obj_set_style_bg_color(crumbs[i].o, lv_color_hex(ink[i % 3]), 0);
                    float a = -3.1416f * (0.1f + 0.8f * i / (N_CRUMBS - 1)), v = frand(150, 260);
                    launch(&crumbs[i], SCR / 2 + frand(-8, 8), MOUTH_Y + 6, cosf(a) * v, sinf(a) * v, frand(0.6f, 0.9f));
                }
            }
            for (int k = 1; k <= 2; k++) /* two more chews */
                if (t0 < FEED_DROP + 0.3f * k && feed_t >= FEED_DROP + 0.3f * k) s.squash.v -= 2.2f;
            if (feed_t >= FEED_LEN) feed_t = -1;
        }
    }
    animate(crumbs, N_CRUMBS, dt, 700);
}

/* ---------- washing (on his screen the bottom key tapped, then held): the sponge scrubs, the dust comes off bit by bit. Grubby,
   he screws his eyes up and enjoys it, and is glad after; already clean, he squirms, crosser the longer it goes ---------- */
#define FUSS_SPOILS 2.2f /* scrubbed this long while clean, the bath is spoilt: no "fresh" joy after it */
#define WASH_GLAD_S 1.5f /* a dirty Pip come clean under the sponge is glad of it this long before scrubbing on annoys him */
void pip_wash(bool on)
{
    if (on == s.washing) return;
    s.input_at = s.t;
    if (on) {
        if (pip_unmet() || s.dark) return;
        light_up();
        if (s.pet.asleep) poke(); /* a sponge wakes him, and not kindly */
        s.washing = true, s.wash_from = s.pet.clean, s.wash_t = s.fuss_t = 0, s.washed_at = -1, s.meet_pending = false;
        s.refusing = pet_refuses_wash(&s.pet); /* washed clean twice lately: as with food, he will not have it */
        if (s.refusing) {
            int r = pet_wash_refused(&s.pet);
            react(r, r == PIP_SWEARING || r == PIP_BERSERK ? 4.0f : 2.5f, "no washing");
        } else { /* still cross with you: he will not let you near him with it */
            int no = pet_spurns(&s.pet, false);
            if ((s.refusing = no >= 0)) react(no, 2.5f, "sulky: no washing");
        }
        grime_scrub(true);
        return;
    }
    s.washing = false;
    grime_scrub(false);
    if (s.refusing) return; /* told already */
    bool clean_already = s.wash_from >= pet_tune.wash_clean || s.fuss_t > FUSS_SPOILS;
    int r = pet_washed(&s.pet, clean_already ? 1.0f : s.wash_from);
    bool fresh = !clean_already && s.pet.clean >= 0.9f;
    if (!clean_already && pet_cold(&s.pet)) r = s.pet.grudge > 0.6f ? PIP_POUTY : PIP_SKEPTICAL; /* clean now, still sore */
    if (fresh) { /* squeaky clean, bubbles still about, an angel, the rain kept off him, a sprout watered */
        r = vary_to(vary_to(vary_to(vary_to(r, PIP_BUBBLES, 0.45f), PIP_SPARKLING, 0.4f), PIP_ANGEL, 0.15f), PIP_UMBRELLA, 0.12f);
        if (pet_valence(&s.pet) > 0.3f) r = vary_to(r, PIP_SPROUT, 0.15f);
    }
    if (r >= PIP_XD) show_react(r);
    else react(r, r == PIP_BUBBLES ? 6.0f : 2.5f, clean_already ? "not dirty" : fresh ? "fresh" : "washed");
    if (fresh) grime_sparkle();
    s.wet = 1, s.drying_paid = s.chill_ill = false, s.shake_off_t = 2; /* soaked: he drips, and dries (blown dry, gladly) */
    pet_dirty = true; /* the board keeps it at once */
}

static float life_rand(float a, float b);
static void wash_fling(int mood, const char *why) /* he has had enough: the sponge goes flying, the bath is over */
{
    s.washing = false;
    grime_fling();
    react(mood, 3.0f, why);
    s.squash.v -= 6, s.head_yaw.v -= 4; /* a shove */
    pet_dirty = true;
    if (pet_tune.vary > 0 && s.pet.trust > 0.4f && life_rand(0, 1) < 0.5f) s.sorry_in = 3.4f; /* then he feels a bit sheepish about it */
}

static void wash_step(float dt)
{
    if (s.dark || unmet()) { pip_wash(false); return; }
    pw.idle = 0; /* nobody dozes off mid-bath */
    s.wash_t += dt;
    if (s.refusing) { /* the sponge just touches him and off it goes */
        if (s.wash_t > 0.35f) wash_fling(s.react_mood >= 0 ? s.react_mood : PIP_NO, "no washing");
        return;
    }
    s.pet.clean = fminf(1, s.pet.clean + pet_tune.wash_rate * dt);
    bool was_clean = s.wash_from >= pet_tune.wash_clean;
    if (!was_clean && s.pet.clean >= 1 && s.washed_at < 0) s.washed_at = s.wash_t; /* he has come clean */
    bool glad = s.washed_at >= 0 && s.wash_t - s.washed_at < WASH_GLAD_S;
    if (was_clean || (s.washed_at >= 0 && !glad)) s.fuss_t += dt; /* scrubbing someone already clean */
    if (glad) { react(PIP_HAPPY, 0.6f, "scrub: clean"); return; } /* clean! a moment glad of it before it is too much */
    if (s.fuss_t <= 0) { react(PIP_CONTENT, 0.6f, "scrub"); return; } /* ^ ^: eyes screwed up, enjoying it */
    if (s.fuss_t > pet_tune.wash_push) { /* scrubbed clean for ages: furious, and the sponge goes flying */
        pet_washed(&s.pet, 1.0f);
        wash_fling(s.pet.annoy >= pet_tune.rage ? PIP_RAGE : PIP_FURIOUS, "sponge away");
        return;
    }
    /* skeptical, then annoyed, cross, and boiling over towards the end */
    float k = s.fuss_t / pet_tune.wash_push;
    react(k < 0.22f ? PIP_SKEPTICAL : k < 0.5f ? PIP_ANNOYED : k < 0.78f ? PIP_ANGRY : PIP_FURIOUS, 0.6f, "not dirty");
}

/* ---------- a sneeze: "a-a-a" - his eyes go wide and he tips his head back, trembling more and more - "CHOO!" - eyes
   screwed shut, a jerk forward and down, a squash, a fine spray - then a dazed blink and back to himself. Only the
   face's own springs and five tiny droplets: it costs no more than any mood, and reads in every eye colour.
   Now and then on his own (every sneeze_min..3x minutes lit and awake), likelier the more he is poked (his nose tickles), and
   every few seconds with hay fever (the allergic card). ---------- */
#define SNEEZE_BURST 0.45f /* the "CHOO" in VOICE_SNEEZE comes this far in */
#define SNEEZE_LEN 1.35f
#define SNEEZE_REST 60 /* s after a sneeze when pokes cannot set off another */
#define N_SPRAY 5
static part_t spray[N_SPRAY];
static float xs_rand(uint32_t *r, uint32_t seed, float a, float b) /* a xorshift of its own: no one else's dice shift */
{
    if (!*r) *r = seed;
    *r ^= *r << 13, *r ^= *r >> 17, *r ^= *r << 5;
    return a + (b - a) * (float)(*r % 10000) / 10000.0f;
}
static float sneeze_rand(float a, float b) { return xs_rand(&s.sneeze_rng, 0x5EED5EEDu, a, b); }
static float burp_rand(float a, float b) { return xs_rand(&s.burp_rng, 0xB0E1B0E1u, a, b); }

static void pip_sneeze(void)
{
    if (unmet() || !FEAT(PIP_F_SNEEZE) || s.pet.asleep || s.dark || s.sneeze_anim >= 0 || s.burp_anim >= 0 || s.intro >= 0 || s.hatch >= 0 || s.washing) return;
    s.sneeze_anim = 0, s.itch = 0, s.sneeze_due = 0, s.last_sneeze = s.t;
    s.next_sneeze = sneeze_rand(pet_tune.sneeze_min * 60, pet_tune.sneeze_min * 180);
    react(PIP_SURPRISED, SNEEZE_LEN, "sneeze"); /* what the status says; the face is drawn by sneeze_face */
    say(VOICE_SNEEZE);
}

/* the face for this moment of the sneeze, over whatever he was showing */
static const mood_def_t *sneeze_face(float dt)
{
    static mood_def_t f;
    float t0 = s.sneeze_anim, t = s.sneeze_anim += dt;
    if (t < SNEEZE_BURST) { /* the build-up: wide eyes rolling up, head back, a growing tremble */
        float k = t / SNEEZE_BURST;
        f = moods[PIP_SURPRISED];
        f.s.h = 150 + 14 * k, f.s.lid = 0.12f * k, f.s.tilt = -0.2f * k;
        f.gy = -0.75f, f.look_rate = 0.01f, f.blink = 0.01f, f.tremble = 0.4f + 1.6f * k, f.slow = 0.15f;
        if (t0 < 0.18f && t >= 0.18f) s.squash.v += 2.5f; /* a breath in: a little stretch */
    } else if (t < SNEEZE_BURST + 0.3f) { /* CHOO */
        f = moods[PIP_OUCH];
        f.s.h = 18, f.gy = 0.6f, f.blink = 0.01f, f.look_rate = 0.01f;
        if (t0 < SNEEZE_BURST) {
            s.squint_t[0] = s.squint_t[1] = 0;
            s.squash.v -= 9, s.head_pitch.v += 6, s.head_roll.v += sneeze_rand(-2, 2);
            part_t *p;
            for (int i = 0; i < N_SPRAY && (p = free_part(spray, N_SPRAY)); i++) { /* a fine spray from under the eyes */
                float a = 3.1416f * (0.15f + 0.7f * i / (N_SPRAY - 1)), v = sneeze_rand(160, 260);
                launch(p, SCR / 2 + sneeze_rand(-12, 12), 330, cosf(a) * v, sinf(a) * v * 0.6f + 40, sneeze_rand(0.35f, 0.55f));
            }
        }
    } else { /* dazed for a moment, a blink, and he is himself again */
        f = moods[PIP_SLEEPY];
        f.s.lid = 0.25f * (1 - (t - SNEEZE_BURST - 0.3f) / (SNEEZE_LEN - SNEEZE_BURST - 0.3f)), f.gy = 0.15f, f.slow = 0.3f;
        if (t0 < SNEEZE_BURST + 0.42f && t >= SNEEZE_BURST + 0.42f) blink();
    }
    if (t >= SNEEZE_LEN) s.sneeze_anim = -1;
    return &f;
}

/* when the next one comes: on his own now and then, a moment after a poke that tickled, often with hay fever */
static void sneeze_step(float dt, bool allergic, bool awake)
{
    animate(spray, N_SPRAY, dt, 500);
    s.itch = fmaxf(0, s.itch - dt / 10); /* the tickle fades in ten seconds */
    if (!FEAT(PIP_F_SNEEZE)) { s.sneeze_due = 0; return; } /* switched off on the panel: none at all */
    if (s.sneeze_anim >= 0 || !awake || s.dark) return;
    if (s.sneeze_due > 0 && (s.sneeze_due -= dt) <= 0) { pip_sneeze(); return; }
    if (allergic && s.next_sneeze > 9) s.next_sneeze = sneeze_rand(4.5f, 9); /* hay fever: every few seconds */
    if ((s.next_sneeze -= dt) <= 0) {
        if (s.react_mood < 0 || allergic) pip_sneeze();
        else s.next_sneeze = sneeze_rand(5, 20); /* busy with something: a little later */
    }
}

/* ---------- a burp: he swells (eyes narrowed and a little smaller, a wobble), then "BUUERP" - head thrown back, eyes
   gone round, trembling with the rattle, three pale puffs drifting up - and then he is embarrassed: blushing, eyes
   aside. The face's springs and three small blobs, nothing more. After a biscuit now and then, always when stuffed;
   grubby, more often after one and now and then on his own too (pet_tune.burp_dirty, burp_min). ---------- */
#define BURP_AT 0.35f /* the sound starts here (it is all one rattle, so the face follows it from the start) */
#define BURP_LEN 1.9f
#define N_PUFF 3
static part_t puff[N_PUFF];

static void pip_burp(void)
{
    if (unmet() || !FEAT(PIP_F_BURP) || s.pet.asleep || s.dark || s.burp_anim >= 0 || s.sneeze_anim >= 0 || s.intro >= 0 || s.hatch >= 0) return;
    s.burp_anim = 0, s.burp_due = 0;
    react(PIP_SURPRISED, BURP_LEN, "burp"); /* what the status says; the face is drawn by burp_face */
}

static const mood_def_t *burp_face(float dt)
{
    static mood_def_t f;
    float t0 = s.burp_anim, t = s.burp_anim += dt;
    if (t < BURP_AT) { /* swelling up */
        float k = t / BURP_AT;
        f = moods[PIP_NEUTRAL];
        f.s.w = 112, f.s.h = 122 - 10 * k, f.s.lid = 0.22f * k, f.s.pw = .36f, f.s.ph = .38f;
        f.gy = 0.15f, f.blink = 0.01f, f.look_rate = 0.01f, f.slow = 0.2f;
        if ((int)(t0 * 9) != (int)(t * 9)) s.squash.v += 1.6f; /* a bloated wobble */
    } else if (t < BURP_AT + 0.48f) { /* BUUERP */
        f = moods[PIP_AMAZED];
        f.s.glint = 0, f.gy = -0.55f, f.tremble = 1.8f, f.blink = 0.01f, f.look_rate = 0.01f;
        if (t0 < BURP_AT) {
            say(VOICE_BURP);
            s.squash.v -= 6, s.head_pitch.v -= 5; /* thrown back */
            part_t *p;
            for (int i = 0; i < N_PUFF && (p = free_part(puff, N_PUFF)); i++)
                launch(p, SCR / 2 + (i - 1) * 26 + sneeze_rand(-6, 6), 350, (i - 1) * 18 + sneeze_rand(-8, 8), sneeze_rand(-90, -60),
                       sneeze_rand(0.8f, 1.1f));
        }
    } else { /* "oops": embarrassed */
        f = moods[PIP_SHEEPISH];
        f.blush = 1;
        if (t0 < BURP_AT + 0.48f) blink();
    }
    if (t >= BURP_LEN) {
        s.burp_anim = -1;
        if (vary_to(0, 1, 0.3f)) show_react(PIP_ANGEL); /* who, me? */
    }
    return &f;
}

static void burp_step(float dt, bool awake)
{
    animate(puff, N_PUFF, dt, -40); /* the puffs rise a little faster as they go */
    if (s.burp_due > 0 && (s.burp_due -= dt) <= 0) pip_burp();
    float most = pet_tune.burp_min * 180; /* the knob turned down: the wait shortens to match at once */
    if (s.next_burp > most) s.next_burp = burp_rand(most / 3, most);
    if (s.burp_anim >= 0 || !awake || s.dark || s.washing || (s.next_burp -= dt) > 0) return;
    float g = grub(); /* grubby: on his own now and then, the dirtier the oftener; clean, never */
    if (g > 0 && s.react_mood >= 0) { s.next_burp = burp_rand(5, 20); return; } /* busy with something: a little later */
    s.next_burp = burp_rand(pet_tune.burp_min * 60, pet_tune.burp_min * 180);
    if (g > 0 && burp_rand(0, 1) < g) pip_burp();
}

/* ---------- hiccups: a run of four to eight "hic!"s a couple of seconds apart, each a jolt up and his eyes popping wide a
   moment (hic.pop: the eyes' size in update), his face otherwise his own. After a biscuit now and then, a long laugh, or
   out of nowhere (his own moments). A fright cures them (react: shocked, afraid, "boo"), and he is glad. Dark, asleep or
   away: they pass. ---------- */
static float hic_rand(float a, float b) { return xs_rand(&hic.rng, 0x41C0FFEEu, a, b); }
static void pip_hiccups(float in)
{
    if (unmet() || s.pet.asleep || s.dark || hic.left > 0 || s.dead) return;
    hic.left = (int)hic_rand(4, 9), hic.next = in, hic.cured_in = 0;
}
static void hic_step(float dt, bool awake)
{
    if (hic.pop > 0) hic.pop = fmaxf(0, hic.pop - dt);
    if (hic.cured_in > 0 && (hic.cured_in -= dt) <= 0 && awake && !s.dark && !s.pet.asleep) react(PIP_RELIEVED, 1.6f, "hiccups gone");
    if (hic.left <= 0) return;
    if (!awake || s.dark || s.pet.asleep) { hic.left = 0; return; } /* slept or the screen went off: gone */
    if (s.sneeze_anim >= 0 || s.burp_anim >= 0 || toot.anim >= 0 || s.washing || (hic.next -= dt) > 0) return;
    hic.left--, hic.next = hic_rand(1.5f, 3.2f), hic.pop = HIC_POP;
    s.squash.v += 5.5f, s.head_pitch.v -= 3, s.head_roll.v += hic_rand(-1, 1); /* a jolt up */
    say(VOICE_HIC);
    if (s.react_mood < 0 || !strcmp(s.react_name, "hic")) react(hic.left % 2 ? PIP_SURPRISED : PIP_PUZZLED, 0.5f, "hic");
    if (!hic.left && hic_rand(0, 1) < 0.6f) hic.cured_in = 1.8f; /* over: he waits for another, and none comes */
}

/* ---------- a toot: a moment of "uh-oh" (still, eyes a little wide), "pfrrt" with a hop and two pale green puffs
   drifting up from under him, eyes wide and down at it - then sheepish and blushing, or a sly snicker, or cheeky about it.
   After a biscuit now and then, or out of nowhere when he is well fed (his own moments). The puffs are the burp's. ---------- */
#define TOOT_AT 0.3f
#define TOOT_LEN 2.3f
static void pip_toot(void)
{
    if (unmet() || !FEAT(PIP_F_TOOT) || s.pet.asleep || s.dark || toot.anim >= 0 || s.burp_anim >= 0 || s.sneeze_anim >= 0 || s.intro >= 0 || s.hatch >= 0 || s.washing)
        return;
    float r = xs_rand(&toot.rng, 0x7007F00Du, 0, 1), v = pet_valence(&s.pet);
    toot.anim = 0, toot.due = 0;
    toot.after = r < (v > 0.3f ? 0.3f : 0.15f) ? PIP_SLY : r < 0.45f && s.pet.trust > 0.5f ? PIP_CHEEKY : PIP_SHEEPISH;
    react(PIP_SURPRISED, TOOT_LEN, "toot"); /* what the status says; the face is drawn by toot_face */
}
static const mood_def_t *toot_face(float dt)
{
    static mood_def_t f;
    float t0 = toot.anim, t = toot.anim += dt;
    if (t < TOOT_AT) { /* uh-oh: he goes still */
        f = moods[PIP_NEUTRAL];
        f.s.h = 132 + 14 * t / TOOT_AT, f.gy = 0.2f, f.blink = 0.01f, f.look_rate = 0.01f, f.tremble = 0.4f;
    } else if (t < TOOT_AT + 0.6f) { /* pfrrt */
        f = moods[PIP_AMAZED];
        f.s.glint = 0, f.gy = 0.8f, f.blink = 0.01f, f.look_rate = 0.01f;
        if (t0 < TOOT_AT) {
            say(VOICE_TOOT);
            s.squash.v += 4, s.head_pitch.v += 2;
            part_t *p;
            for (int i = 0; i < 2 && (p = free_part(puff, N_PUFF)); i++)
                launch(p, SCR / 2 + (i ? 34 : -34) + xs_rand(&toot.rng, 1, -8, 8), 440, (i ? 22 : -22), -xs_rand(&toot.rng, 1, 55, 80), 1.3f);
        }
    } else { /* and then: sheepish, sly or cheeky about it */
        f = moods[toot.after];
        if (toot.after == PIP_SHEEPISH) f.blush = 0.9f;
        if (t0 < TOOT_AT + 0.6f) say(toot.after == PIP_SHEEPISH ? VOICE_EHE : toot.after == PIP_SLY ? VOICE_SNICKER : VOICE_GIGGLE);
    }
    if (t >= TOOT_LEN) {
        toot.anim = -1;
        if (s.react_mood < 0 || !strcmp(s.react_name, "toot")) { /* the face it left him with, or: who, me? */
            if (vary_to(0, 1, 0.3f)) show_react(PIP_ANGEL);
            else react(toot.after, 1.6f, "toot");
        }
    }
    return &f;
}
/* ---------- tickled: a real laugh, not a glad face - the eyes squeezed to "^ ^", a bounce and a toss of the head on
   every "ha", the laugh again and again while it goes on; the longer, the more helpless: shaking, blushing, squeezed
   shut, tears of laughter (LAUGH_FULL_S). Let go after a good tickle, he giggles on a moment, getting his breath ---------- */
static void laugh_step(float dt)
{
    s.laugh_k = fminf(1, fmaxf(0, s.tickle_t - TICKLE_S) / LAUGH_FULL_S);
    float k = s.laugh_k;
    if ((s.laugh_beat -= dt) <= 0) { /* "ha!" */
        s.laugh_beat = vrand(0.17f, 0.26f) * (1 - 0.25f * k);
        s.laugh_side = s.laugh_side > 0 ? -1 : 1;
        s.squash.v -= 2 + 2.5f * k, s.head_roll.v += s.laugh_side * (0.5f + 0.7f * k);
    }
    if ((s.laugh_voice -= dt) <= 0) s.laugh_voice = vrand(1.1f, 1.5f) * (1 - 0.2f * k), say(VOICE_LAUGH); /* and again */
    part_t *p; /* laughing till he cries: drops from the eyes' outer corners, one side then the other (the sweat drops' pool) */
    if (k > 0.45f && (s.laugh_tear -= dt) <= 0 && (p = free_part(sweat, N_SWEAT))) {
        s.laugh_tear = vrand(0.35f, 0.6f);
        const eye_t *e = &s.face.eye[s.laugh_side > 0 ? 1 : 0];
        float side = e->x < SCR / 2 ? -1 : 1;
        launch(p, e->x + side * e->w * 0.5f, e->y + e->h * 0.1f, side * vrand(50, 90), -vrand(40, 80), 0.9f);
    }
}
static const mood_def_t *laugh_face(void)
{
    static mood_def_t f;
    float k = s.laugh_k;
    f = moods[PIP_LAUGHING];
    f.s.w = 126 + 6 * k, f.s.h = 112 - 8 * k, f.s.smile = fminf(1, 0.95f + 0.05f * k); /* squeezed tighter (the tears: laugh_step) */
    f.blush = 0.4f + 0.5f * k, f.tremble = 0.8f + 2.2f * k, f.look_rate = 0.05f, f.blink = 0.01f;
    return &f;
}

static void toot_step(float dt, bool awake)
{
    if (toot.due > 0 && (toot.due -= dt) <= 0 && awake) pip_toot();
}

static void sneeze_poked(void) /* each poke tickles his nose a little more: the next may set him off */
{
    s.itch += 1;
    if (s.t - s.last_sneeze < SNEEZE_REST) return; /* his nose has only just cleared */
    if (s.sneeze_anim < 0 && s.sneeze_due <= 0 && sneeze_rand(0, 1) < fminf(0.2f, pet_tune.sneeze_poke * s.itch)) s.sneeze_due = 0.45f;
}

static void step(float dt, const mood_def_t *m, bool dizzy)
{
    float slow = m->slow;
    for (int i = 0; i < 2; i++) {
        shape_t to = mood_shape(m);
        if (i) to.lid += m->lid_asym;
        const float *tf = (const float *)&to;
        for (int j = 0; j < SHAPE_N; j++) spring(&s.shape[i][j], tf[j], 220 * (1 - 0.6f * slow), 0.5f, dt);
    }
    spring(&s.open, 1 - m->closed, 30, 1.0f, dt);
    spring(&s.squash, 0, 300, 0.25f, dt);
    spring(&s.dizzy, dizzy, 20, 1.0f, dt);
    uint32_t tint = m->tint ? m->tint : 0xFFFFFF;
    for (int c = 0; c < 3; c++) spring(&s.tint[c], ((tint >> (16 - 8 * c)) & 255) / 255.0f, 20, 1.0f, dt);
    spring(&s.blush, m->blush, 20, 1.0f, dt);
    spring(&s.tremble, m->tremble, 20, 1.0f, dt);
    spring(&s.nod, m->nod, 20, 1.0f, dt);
    spring(&s.wag, m->wag, 20, 1.0f, dt);

    /* the eyes jump to the target, the head follows slowly and the eyes counter-rotate */
    float lean = s.eco ? 0 : 1; /* saving energy the eyes hold still instead of rolling with every tilt */
    float tx = s.tgx + m->gx + s.imu.tilt_x * 1.1f * lean, ty = s.tgy + m->gy + s.imu.tilt_y * 0.9f * lean; /* eyes roll downhill */
    tx = fmaxf(-1.2f, fminf(1.2f, tx));
    ty = fmaxf(-1.0f, fminf(1.0f, ty));
    float eye_k = 900 * (1 - 0.85f * slow), head_k = 45 * (1 - 0.7f * slow);
    float sway = s.t * 1.4f; /* nausea circles, respectful nods */
    spring(&s.look_x, tx * 0.9f + s.micro_x, eye_k, 0.65f, dt); /* micro-saccades move the pupils only */
    spring(&s.look_y, ty * 0.6f + s.micro_y, eye_k, 0.65f, dt);
    spring(&s.head_yaw, tx * 0.7f + m->sway_x * sinf(sway) + s.dodge_yaw, head_k, 0.7f, dt);
    spring(&s.head_pitch, ty * 0.3f + m->sway_y * cosf(sway) + s.lean_pitch, head_k, 0.7f, dt);
    spring(&s.head_roll, m->head_roll + s.idle_roll + s.lean_roll, 30, 0.6f, dt);
}

#define DEAD_DARK_S 5.0f /* dead: his screen goes dark this soon, whatever the sleep timer; only a button lights it */
static void power(float dt)
{
    if (s.dead) { /* no touch, no motion, nothing holds it: a button (pip_seen) lights it again for DEAD_DARK_S */
        pw.dim = false;
        if (!s.dark && (pw.idle += dt) > DEAD_DARK_S) s.dark = true;
        return;
    }
    if (lv_display_get_inactive_time(NULL) < 300) { /* a finger on the glass */
        if (pw.dozed) light_up();
        pw.idle = 0;
    }
    pw.idle += dt;
    if (s.hatch >= 0) pw.idle = 0; /* his birth: nothing puts the light out */
    if (pw.hold) pw.idle = 0;
    if (s.dark || pw.after <= 0) { pw.dim = false; return; }
    pw.dim = pw.idle > pw.after - fminf(DIM_BEFORE_S, pw.after * 0.5f);
    if (pw.idle > pw.after) {
        s.dark = true, pw.dozed = true, pw.dim = false;
        if (!unmet()) pet_event(&s.pet, PET_DOZE); /* left alone, the screen off: he naps (not a demanding one) */
        memcpy(pw.g, pw.lp, sizeof pw.g);
    }
}

/* the chase: eyes a little ahead of the finger (where it is going), a hop now and then while it runs, and when it
   stops a pounce - a crouch, then a leap onto it. Springs and the squash only: nothing new to draw */
static void chase_step(float dt, float speed)
{
    s.pounce_t += dt;
    bool crouch = speed <= 70 && s.pounce_t > 0.3f && s.pounce_t < 0.8f; /* stopped: he crouches, eyes narrowed, then pounces */
    if (crouch) react(PIP_DETERMINED, 0.5f, "crouch");
    else react(PIP_PLAYFUL, 0.5f, "chase"); /* "Ку-ку!" once as it starts, then just the face */
    if (speed > 70) {
        if (s.pounce_t > 0.42f) s.pounce_t = 0, s.squash.v -= 2.6f, s.head_roll.v += s.fvx > 0 ? 0.8f : -0.8f; /* hop, leaning into it */
    } else if (s.pounce_t > 0.35f && s.pounce_t - dt <= 0.35f) {
        s.squash.v += 3, blink(); /* stopped: he crouches... */
    } else if (s.pounce_t > 0.55f && s.pounce_t - dt <= 0.55f) {
        s.squash.v -= 7, s.head_pitch.v += 3; /* ...and pounces */
    }
}

/* the phone gets the looks: its notifications (pip_note) and its music while he is not dancing to it add up, a touch or a
   button for him clears it; past pet_tune.jealous_s, now and then, he sulks about it */
static void jealous_step(float dt)
{
    const pet_tune_t *k = &pet_tune;
    if (phone_playing() && !dn.on) s.phone_s += dt;
    if (s.phone_s < k->jealous_s || s.pet.asleep || unmet() || away) return; /* kept for when he is on screen again */
    s.phone_s = 0;
    if ((s.jealous_at > 0 && s.t - s.jealous_at < k->jealous_gap * 60) || frand(0, 1) >= k->jealous_chance) return;
    s.jealous_at = s.t;
    int r = pet_event(&s.pet, PET_JEALOUS);
    if (!s.dark && vary_to(0, 1, 0.4f)) show_react(PIP_SULKING);
    else if (!s.dark) react(r, 3.0f, "jealous");
}

/* ---------- feelings with weight: his face does not jump from glum to glad (or back) for no reason ----------
   Each mood has a place from miserable (-1) to delighted (1). s.affect is how he feels inside: it follows the face he
   shows, in a few seconds while something is happening to him (a reaction) and over a quarter of a minute otherwise.
   A base mood that came with a reason (asleep, the body, an aftertaste, just woken, a rare card: s.mood_reason) shows
   at once; one that only drifted there (the day's palette, a need, the night) is reached by degrees: while it is far
   from how he feels, a step between is shown (bored, dull, neutral, content...) and the affect walks him over. His own
   moments and the varied answers keep near how he feels (affect_fit). Nothing drawn: only which mood, and when. */
static float mood_val[PIP_MOOD_COUNT];
static void mood_vals(void)
{
    static const struct { const char *name; int8_t v; } t[] = { /* hundredths */
        {"happy", 70}, {"sad", -70}, {"angry", -70}, {"surprised", 10}, {"sleepy", -10}, {"skeptical", -20}, {"curious", 30},
        {"love", 90}, {"dizzy", -20}, {"ouch", -40}, {"stoned", 20}, {"pinkeye", -40}, {"nauseous", -60}, {"sick", -60},
        {"tired", -30}, {"exhausted", -50}, {"annoyed", -50}, {"furious", -85}, {"rage", -90}, {"swearing", -90},
        {"berserk", -100}, {"indignant", -50}, {"belligerent", -60}, {"hurt", -60}, {"disgusted", -50}, {"suspicious", -30},
        {"cynical", -30}, {"afraid", -60}, {"nervous", -40}, {"despondent", -80}, {"lonely", -60}, {"gloomy", -50},
        {"dull", -20}, {"shy", 30}, {"smug", 40}, {"bored", -30}, {"shocked", -30}, {"dismayed", -50}, {"amazed", 50},
        {"cheeky", 50}, {"content", 60}, {"comfy", 50}, {"yes", 50}, {"no", -30}, {"allergic", -40}, {"frozen", -40},
        {"overheated", -40}, {"smitten", 90}, {"hyper", 50}, {"zombie", -40}, {"excited", 80}, {"dreamy", 50},
        {"pouty", -30}, {"proud", 70}, {"focused", 20}, {"touched", 80}, {"playful", 70}, {"relieved", 40},
        {"laughing", 80}, {"grateful", 80}, {"dancing", 80}, {"tumbling", 70}, {"whirled", 50}, {"woozy", -10},
        {"impatient", -15}, {"wistful", 10}, {"blissful", 85}, {"adoring", 90}, {"sly", 40}, {"sheepish", -10},
        {"determined", 20}, {"eureka", 60}, {"eyeroll", -35}, {"flustered", 40},
        {"squished", -15}, {"stretched", 10}, {"overwhelmed", -30}, {"pinched", -10}, {"beaming", 85}, {"cuddly", 85}, {"eager", 55},
        {"yummy", 75}, {"triumphant", 80}, {"deflated", -55}, {"sparkling", 75}, {"groggy", -20},
        {"starstruck", 85}, {"singing", 65}, {"kiss", 90}, {"flirty", 60}, {"rainbow", 80}, {"peeking", 55}, {"zen", 50}, {"sweetdream", 50},
        {"melting", 88}, {"applause", 75}, {"cool", 55}, {"puppy", 20}, {"butterfly", 50}, {"bubbles", 60}, {"robot", 40}, {"kitty", 75},
        {"silly", 65}, {"snicker", 70}, {"campfire", 55}, {"stargazing", 40}, {"zapped", 60}, {"sipping", 55},
        {"xd", 85}, {"rofl", 85}, {"bouncy", 80}, {"blooming", 85}, {"hearteyes", 92}, {"fireworks", 85}, {"medal", 75}, {"disco", 80},
        {"popeyed", 30}, {"lightbulb", 60}, {"paws", 35}, {"hello", 70}, {"king", 65}, {"angel", 50}, {"imp", 45}, {"juggler", 70},
        {"balloon", 70}, {"icecream", 75}, {"umbrella", 50}, {"strongman", 65}, {"magician", 65}, {"sunrise", 70}, {"sheep", -5}, {"ninja", 40},
        {"sprout", 75}, {"raincloud", -60}, {"sobbing", -90}, {"heartbroken", -85}, {"fuming", -75}, {"burning", -90}, {"sulking", -45}, {"spooked", -60},
        {"tumbleweed", -35}, {"lowbatt", -40}, {"hungry", -35}, {"knockout", -40}, {"wilted", -70}, {"sniffles", -50}, {"charged", 75}, {"cutoff", -35}, {"empty", -55}, {"queasy", -60},
        {"newtree", 80}, {"wreath", 80},
    }; /* the rest (neutral, asleep, puzzled, hypnotized) sit at 0 */
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        int m = pip_find_mood(t[i].name);
        if (m >= 0) mood_val[m] = t[i].v / 100.0f;
    }
}
#define AFFECT_NEAR 0.45f /* a base mood this near how he feels is shown as it is */
static float affect_fit(int mood, float spread) /* 1 right at how he feels, falling off with the distance */
{
    float d = (mood_val[mood] - s.affect) / spread;
    return 1 / (1 + d * d * d * d);
}
/* the base mood as his face should show it now: itself, or a step towards it */
static int calm_base(int target)
{
    if (!s.affect_set || !s.auto_mood || s.mood_reason || pet_tune.calm <= 0 || fabsf(mood_val[target] - s.affect) <= AFFECT_NEAR) return s.bridge = -1, target;
    float to = mood_val[target], want = s.affect + (to > s.affect ? 0.3f : -0.3f);
    /* a step shown holds a while, as long as it still lies on the way */
    if (s.bridge >= 0 && s.t - s.bridge_at < 10 && (mood_val[s.bridge] - s.affect) * (to - s.affect) >= -0.02f) return s.bridge;
    static const uint8_t steps[] = {PIP_NEUTRAL, PIP_CONTENT, PIP_COMFY, PIP_CURIOUS, PIP_DREAMY, PIP_SHY, PIP_RELIEVED, PIP_FOCUSED,
                                    PIP_BORED, PIP_DULL, PIP_GLOOMY, PIP_POUTY, PIP_SKEPTICAL, PIP_TIRED, PIP_SAD};
    int best = PIP_NEUTRAL;
    float bd = 9;
    for (unsigned i = 0; i < sizeof steps; i++) {
        float d = fabsf(mood_val[steps[i]] - want) + (steps[i] == s.bridge ? 0.15f : 0); /* a new step, not the same one */
        if (d < bd) bd = d, best = steps[i];
    }
    s.bridge = best, s.bridge_at = s.t;
    return best;
}
static void affect_step(int shown, float dt)
{
    if (!s.affect_set) { s.affect = mood_val[shown], s.affect_set = true; return; }
    float tau = s.react_mood >= 0 ? 3.0f : 15.0f; /* moved by what happens to him; settling slowly on his own */
    s.affect += (mood_val[shown] - s.affect) * (1 - expf(-dt / tau));
}

/* ---------- being alive: what he does of his own accord, and answers that are never quite the same ----------
   Everything here draws on his own dice (life_rand: no one else's random numbers shift) and his state: valence, the
   needs, trust, the hour. pet_tune.vary 0 gives the classic answer every time, life_gap 0 no moments of his own (the
   checks pin both). Only springs, the moods' own faces and a few drops: nothing new for the renderer. */
#define LIFE_IDLE_S 6.0f  /* untouched this long before he does something of his own */
#define HEAD_Y 120.0f     /* a stroke above this (the top of his head, over the eyes) is a pat */
#define N_DRIP 4
static part_t drip[N_DRIP];
static float life_rand(float a, float b) { return xs_rand(&s.life_rng, 0x11FE11FEu, a, b); }
static bool night_now(void) { int h = pet_hour(s.pet.last, s.tz); return h >= 22 || h < 7; }

/* a plain poke (not an eye, not one too many): ouch, as a rule - or a "huh?", a giggle, a glare, a cheeky "again?",
   pointedly not noticing, shy - by how he is */
/* talking to himself (or to you): a babble by how he feels - glad chatter (a sentence, a question, giggles in it), a cross
   grumble, a glum mumble, or (cheeky, sure of you) a mocking "be-be-be"; now and then instead he tells about something
   that happened lately (tell). His face stays as it is, a bob with each syllable. Only into a quiet (speak, class 0):
   false if he kept it to himself */
static float chattiness(void);
static bool tell(int i);
static int chat_lvl;
static const float chat_mul[3];
/* his talk never says the same kind of sentence twice running (the "be-be-be" tease is one fixed phrase, and two
   questions or two grumbles in a row sound like a repeat): the last one said, and what to say instead of it */
static int last_talk = -1;
static int talk_other(int voice, float v)
{
    switch (voice) {
    case VOICE_MUTTER: return VB(BABBLE_COMPLAIN);
    case VB(BABBLE_COMPLAIN): return VOICE_MUTTER;
    case VOICE_MUMBLE: return VB(BABBLE_SAY);
    case VB(BABBLE_SAY): return v < -0.25f ? VOICE_MUMBLE : VB(BABBLE_ASK);
    case VOICE_TEASE: return s.pet.fun > 0.5f ? VB(BABBLE_GIGGLY) : VOICE_CHAT;
    case VOICE_CHAT: return VB(BABBLE_SAY);
    case VB(BABBLE_ASK): return VB(BABBLE_SAY);
    case VB(BABBLE_GIGGLY): return VOICE_CHAT;
    case VB(BABBLE_EXCLAIM): return s.pet.fun > 0.5f ? VB(BABBLE_GIGGLY) : VB(BABBLE_SAY);
    case VB(BABBLE_SOB): return VB(BABBLE_SAY);
    case VB(BABBLE_WOOZY): return VB(BABBLE_SAY);
    }
    return voice;
}
static bool talk(int face, float secs)
{
    /* something on his mind: the strongest recent memory not told too often */
    int best = -1;
    float bw = 0.12f;
    for (int i = 0; i < MEM_N; i++)
        if (mem[i].told < 3 && mem_w(i) > bw) best = i, bw = mem_w(i);
    if (best >= 0 && pet_tune.talk > 0 && vrand(0, 1) < 0.55f && tell(best)) return true;
    float v = s.affect, r = vrand(0, 1);
    bool cross = mood_val[face] < -0.4f && (face == PIP_ANNOYED || face == PIP_ANGRY || face == PIP_FURIOUS || face == PIP_INDIGNANT ||
                                            face == PIP_BELLIGERENT || face == PIP_SKEPTICAL || face == PIP_CYNICAL || face == PIP_POUTY);
    int voice = cross ? (r < 0.5f ? VOICE_MUTTER : VB(BABBLE_COMPLAIN))
              : v < -0.25f ? (r < 0.5f ? VOICE_MUMBLE : VB(BABBLE_SAY))
              : (face == PIP_CHEEKY || face == PIP_PLAYFUL || (s.pet.trust > 0.5f && v > 0.3f && r < 0.2f)) ? VOICE_TEASE
              : r < 0.3f ? VOICE_CHAT : r < 0.6f ? VB(BABBLE_SAY) : r < 0.8f ? VB(BABBLE_ASK) : s.pet.fun > 0.5f ? VB(BABBLE_GIGGLY) : VB(BABBLE_SAY);
    if (voice == last_talk) voice = talk_other(voice, v);
    if (!speak(voice, 0, face)) return false;
    last_talk = voice;
    react(face, secs, "chats");
    s.talk_t = voice == VOICE_TEASE ? 0.8f : voice == VOICE_MUMBLE ? 1.4f : 1.3f, s.talk_beat = 0, s.babble_at = s.t;
    return true;
}

/* telling about it: the face it left him with (calmed down a little), the babble in the memory's tone blended with how he
   feels now - cross things grumbled (or a sorry tale, if it hurt and he is not angry), glad and lively ones exclaimed (or
   giggled over), the giddy ones in a swimming voice, the rest a plain sentence or a question */
static int tell_face(int m)
{
    bool coin = vrand(0, 1) < 0.5f;
    switch (m) {
    case PIP_SHOCKED: case PIP_AFRAID: case PIP_OUCH: case PIP_DISMAYED: return coin ? PIP_POUTY : PIP_NERVOUS;
    case PIP_FURIOUS: case PIP_RAGE: case PIP_SWEARING: case PIP_BERSERK: case PIP_ANGRY: case PIP_BELLIGERENT: return coin ? PIP_ANNOYED : PIP_INDIGNANT;
    case PIP_HURT: case PIP_SAD: return coin ? PIP_POUTY : PIP_SAD;
    case PIP_EXCITED: case PIP_LAUGHING: case PIP_HYPER: case PIP_DANCING: case PIP_PLAYFUL: return coin ? PIP_HAPPY : PIP_CHEEKY;
    case PIP_WHIRLED: case PIP_TUMBLING: case PIP_HYPNOTIZED: return coin ? PIP_WOOZY : PIP_DIZZY;
    case PIP_LOVE: case PIP_TOUCHED: case PIP_SMITTEN: return coin ? PIP_CONTENT : PIP_SHY;
    }
    return m;
}
static bool tell(int i)
{
    if (i < 0 || i >= MEM_N || mem[i].w <= 0) return false;
    memory_t *m = &mem[i];
    float v = 0.65f * m->v + 0.35f * s.affect, a = 0.7f * m->a + 0.3f * s.pet.energy;
    int kind = m->wobbly ? BABBLE_WOOZY
             : v < -0.3f ? (a > 0.5f || s.pet.grudge > 0.3f ? BABBLE_COMPLAIN : BABBLE_SOB)
             : v > 0.45f && a > 0.55f ? (s.pet.fun > 0.5f && vrand(0, 1) < 0.4f ? BABBLE_GIGGLY : BABBLE_EXCLAIM)
             : vrand(0, 1) < 0.3f ? BABBLE_ASK : BABBLE_SAY;
    int face = tell_face(m->mood);
    if (VB(kind) == last_talk) kind = kind == BABBLE_SAY ? BABBLE_ASK : BABBLE_SAY; /* not the same kind of sentence twice running */
    snd.bv = v, snd.ba = a, snd.bset = true; /* the babble in the memory's tone, not the face's */
    bool said = speak(VB(kind), 0, face);
    snd.bset = false;
    if (!said) return false;
    last_talk = VB(kind);
    float len = (kind == BABBLE_SOB ? 1.3f : 1.0f) * (1.15f - 0.25f * a);
    react(face, len + 0.7f, "tells");
    s.talk_t = len, s.talk_beat = 0, s.babble_at = s.t;
    m->told++, m->w *= 0.55f;
    float c = chattiness() * pet_tune.talk * chat_mul[chat_lvl]; /* a chatterbox goes on: a second sentence after a breath */
    if (c > 1.1f && vrand(0, 1) < 0.4f * (c - 0.9f)) s.chat_again = len + vrand(0.4f, 0.9f);
    return true;
}
/* a reaction to what you did has run its course: now and then (the stronger it was and the chattier he is, the likelier)
   he has something to say about it, a moment later, once nothing else is going on */
static void tell_after(int i)
{
    if (pet_tune.talk <= 0 || i < 0 || i >= MEM_N || mem[i].w <= 0 || s.pet.asleep || unmet()) return;
    float p = pet_tune.talk * chat_mul[chat_lvl] * (0.45f + 0.3f * (chattiness() - 1) + 0.5f * mem[i].w); /* more to say than he had */
    if (s.t - s.babble_at < 5 || vrand(0, 1) >= fminf(0.85f, p)) return;
    s.tell_in = vrand(0.35f, 1.1f), s.tell_mem = i, s.tell_until = s.tell_in + 3;
}

/* in_eye: the poke landed on an eye - never funny (a giggle there read as tickling, seen on the board: `touchtrace`) */
static void poke_answer(bool in_eye)
{
    static int last;
    float v = pet_valence(&s.pet), f = s.pet.fun, tr = s.pet.trust;
    const answer_t a[] = {
        {PIP_OUCH, 3, "poke", 0.45f},
        {PIP_SURPRISED, 1.2f, "poke: huh", 0.8f},
        {PIP_SKEPTICAL, 1, "poke: really", 1.0f},
        {PIP_LAUGHING, in_eye ? 0 : f > 0.5f && v > 0.2f ? 1.3f : 0.2f, "poke: giggle", 0.9f},
        {PIP_ANNOYED, irk() > 0.5f ? 1.6f : 0.05f, "poke: hey", 1.0f},
        {PIP_PLAYFUL, tr > 0.55f && v > 0 ? 1 : 0.2f, "poke: again", 1.2f},
        {PIP_SMUG, tr > 0.5f ? 0.7f : 0.1f, "poke: ignore", 1.4f},
        {PIP_SHY, s.pet.love > 0.7f ? 0.8f : 0.1f, "poke: shy", 1.0f},
        {PIP_EYEROLL, irk() > 0.3f ? 0.9f : 0.1f, "rolls eyes", 1.6f},
        {PIP_SILLY, in_eye ? 0 : f > 0.55f && v > 0.2f ? 0.9f : 0.05f, "clowns", 3.2f},
    };
    if (answer(a, 10, &last) == 6) pip_glance(s.fx < SCR / 2 ? 400 : 66, 200, 1.2f); /* nose in the air, looking elsewhere */
}

/* Blown on. Nobody blows without breathing in: breaths with pauses under BREATH_GAP between them are one go - his face
   holds through the pause, and the pause counts with the rest (blow_t: s since the go began). As it goes on, it builds up in steps:
   first an answer to the breath (he screws his eyes up, or it surprises, soothes, tickles, annoys him), after a few
   seconds he has had enough of it or it frightens him (by trust and how cross he is), then he starts to feel chilly
   (shivers, cringes, "brr") and at cold_s he is cold: frozen, shivering, "brrr". Wet after a shower, a go begun on him
   dries him first (content, snorts of pleasure, a little fun), then he gets chilly, and cold sooner (0.6 of it); a chill
   while wet may make him ill. The chill wears off in a minute or so, faster stroked warm. */
#define BREATH_GAP 2.5f /* s: a pause shorter than this is drawing breath, not the end of it */
#define BANG_GONE 10.0f /* dB: a bang's meter fell this far under its peak in the wait (a breath or a noise go on) */
void pip_blow_info(float *t, int *stage, int *go, float *wet) { *t = s.blow_t, *stage = s.blow_stage, *go = s.blowing, *wet = s.wet; }
#define DRY_BLOW_S 1.5f /* s of breath on him that blow a soaked Pip dry (pauses to draw breath count half: on the board a
                           breath comes and goes, and his own snorts deafen the mic a moment) */
static void blow_step(float dt, bool on)
{
    /* breath in the eyes: they screw up at once (~0.1 s) and open slowly once it stops (in a pause to draw breath too) -
       whatever he makes of it, a breeze he likes included; a sleeper or a dark screen feels none */
    float wind = on && !s.dark && !s.pet.asleep ? 1 : 0;
    s.wind += (wind - s.wind) * fminf(1, dt / (wind > s.wind ? 0.05f : 0.3f));
    if (s.wind < 0.01f) s.wind = 0;
    if (on) s.input_at = s.t; /* a breath on him is you */
    if (!on) {
        if (!s.blowing) { s.blow_t = fmaxf(0, s.blow_t - dt * 0.5f); return; }
        if ((s.blow_gap += dt) < BREATH_GAP) { /* drawing breath: the go goes on (it counts: he is still out in the
                                                  wind), his face holds */
            s.blow_t += dt;
            if (s.blow_wet && s.wet > 0) s.wet = fmaxf(0, s.wet - dt * 0.5f / DRY_BLOW_S); /* still in the wind, nearly */
            if (s.react_mood >= 0 && s.react_name == s.blow_name) s.react_left = fmaxf(s.react_left, 0.3f);
            return;
        }
        s.blowing = false, s.blow_stage = -1; /* stopped: it fades now */
        return;
    }
    if (!s.blowing) {
        if (s.blow_t < 0.5f) s.blow_wet = s.wet > 0.02f; /* a fresh go: on a wet Pip or a dry one */
        s.blow_stage = -1;
    }
    s.blowing = true, s.blow_gap = 0, s.blow_t += dt, pw.idle = 0;
    /* the wind dries him whatever he makes of it: DRY_BLOW_S of it takes him from soaked to dry (it used to dry only while
       he was enjoying it, which ended before he was dry: he went on dripping for minutes) */
    if (s.blow_wet && s.wet > 0) s.wet = fmaxf(0, s.wet - dt / DRY_BLOW_S);
    float lim = pet_tune.cold_s * (s.blow_wet ? 0.6f : 1), k = s.blow_t / lim;
    int stage = k >= 1 ? 3 : k >= (s.blow_wet ? 0.55f : 0.65f) ? 2 : !s.blow_wet && k >= 0.3f ? 1 : 0;
    if (stage == 3) { /* too much of it: cold */
        if (s.chill < 0.05f) s.squash.v -= 3;
        s.chill = fminf(1, s.chill + dt * 0.5f);
        if (s.wet > 0.3f && !s.chill_ill) s.chill_ill = true, pet_ill = fminf(1, pet_ill + 0.3f), s.sniffle_in = 4; /* wet and cold: a chill */
        s.blow_name = "cold", s.warm_in = 2;
        react(PIP_FROZEN, 1.5f, s.blow_name);
        return;
    }
    if (stage == 0 && s.blow_wet) { /* drying him: lovely */
        if (!s.drying_paid) s.drying_paid = true, pet_event(&s.pet, PET_TICKLE);
        s.blow_name = s.wet > 0 ? "drying" : "dry";
        react(s.wet > 0 ? PIP_CONTENT : PIP_HAPPY, 0.8f, s.blow_name);
        if ((s.snort_next -= dt) <= 0) say(VOICE_SNORT), s.snort_next = life_rand(1.2f, 2.2f), s.squash.v -= 1.5f;
        return;
    }
    if (stage != s.blow_stage) { /* a new step: its answer, kept while the step lasts */
        static int last[3];
        float v = pet_valence(&s.pet), f = s.pet.fun, tr = s.pet.trust, an = s.pet.annoy;
        const answer_t first[] = {{PIP_OUCH, 3, "blown", 0.6f}, {PIP_SURPRISED, 1, "blown: whoa", 0.6f}, {PIP_DREAMY, v > 0.2f ? 1.2f : 0.3f, "blown: breeze", 0.6f},
                                  {PIP_LAUGHING, f > 0.5f ? 1 : 0.2f, "blown: tickles", 0.6f}, {PIP_ANNOYED, an > pet_patience() ? 1.5f : 0.1f, "blown: stop", 0.6f},
                                  {PIP_SKEPTICAL, 0.7f, "blown: hm", 0.6f}};
        const answer_t enough[] = {{PIP_ANNOYED, 2 + an, "blown: enough", 0.6f}, {PIP_AFRAID, tr < 0.5f ? 1.5f : 0.5f, "blown: scared", 0.6f},
                                   {PIP_POUTY, 1, "blown: enough", 0.6f}, {PIP_NERVOUS, tr < 0.5f ? 1 : 0.4f, "blown: scared", 0.6f},
                                   {PIP_SKEPTICAL, 0.7f, "blown: enough", 0.6f}};
        const answer_t chilly[] = {{PIP_NERVOUS, 2, "blown: chilly", 0.6f}, {PIP_POUTY, 1, "blown: chilly", 0.6f}, {PIP_SAD, 1, "blown: chilly", 0.6f}};
        const answer_t *a = stage == 0 ? first : stage == 1 ? enough : chilly;
        int n = stage == 0 ? 6 : stage == 1 ? 5 : 3, i = pick(a, n, &last[stage]);
        s.blow_mood = a[i].mood, s.blow_name = a[i].name, s.blow_stage = stage;
    }
    if (stage == 2 && (s.snort_next -= dt) <= 0) s.squash.v -= 1.2f, s.head_roll.v += frand(-0.6f, 0.6f), s.snort_next = life_rand(0.5f, 1.1f); /* shivers */
    react(s.blow_mood, 0.6f, s.blow_name);
}

/* Rocked: tilted gently from side to side, slowly (half a swing 0.7..3 s, no jolts) - a lullaby, and he grows drowsy:
   the more worn out (and at night) the sooner he is off, in about rock_s for a tired one. Not tired, it does not
   work: comfy a while, then bored of it, or he turns it into a game, or tells you he is not sleepy. Faster swinging
   (under 0.9 s a side) is the "whee" (swing_step). */
#define ROCK_TILT 0.12f
#define ROCK_MIN 0.7f        /* s a side: quicker is a swing ... */
#define ROCK_MIN_TIRED 0.4f  /* ... for a tired one only quicker than this */
#define ROCK_TIRED 0.5f
static struct { int side, halves; float last; bool on; } rk;
static bool rocking(void) { return rk.halves >= 2; }
static void rock_feel(void)
{
    float tx = s.imu.tilt_x;
    int side = tx > ROCK_TILT ? 1 : tx < -ROCK_TILT ? -1 : 0;
    bool smooth = s.imu.energy < 0.3f && !s.imu.shaking && !s.imu.face_down;
    if (!smooth) rk.halves = 0, rk.on = false;
    if (side && side != rk.side) {
        float half = s.t - rk.last;
        /* a hand rocks at about half a second a side (measured): for a tired one that is a lullaby already, if it is smooth;
           a lively one takes it for a swing. Once it is a lullaby, a quicker half does not end it */
        float least = s.pet.energy < ROCK_TIRED ? ROCK_MIN_TIRED : rk.on ? 0.55f : ROCK_MIN;
        rk.halves = rk.side && smooth && half >= least && half <= 3.0f ? rk.halves + 1 : 0;
        rk.side = side, rk.last = s.t;
        if (rk.halves >= 3) rk.on = true;
    }
    if (rk.on && s.t - rk.last > 3.2f) rk.on = false, rk.halves = 0; /* stopped */
}
static void rock_step(float dt)
{
    if (!rk.on || s.pet.asleep || s.dark || away || unmet()) {
        s.lull = fmaxf(0, s.lull - dt * 0.05f), s.rock_bored_t = 0, s.rock_told = false;
        return;
    }
    pw.idle = 0; /* held and rocked: nobody is gone */
    bool can = s.pet.energy < 0.75f; /* not tired: no lullaby puts him off */
    if ((s.lullaby_next -= dt) <= 0) say(VOICE_LULLABY), s.lullaby_next = 4.0f;
    if (can) {
        s.lull += dt * (0.3f + 1.4f * (1 - s.pet.energy) + (night_now() ? 0.4f : 0)) / pet_tune.rock_s;
        react(s.lull > 0.45f ? PIP_SLEEPY : PIP_COMFY, 1.0f, "rocked");
        if (s.lull >= 1) {
            s.lull = 0;
            pet_event(&s.pet, PET_LULLED), pet_dirty = true;
            react(PIP_SLEEPY, 1.0f, "lulled");
        }
        return;
    }
    if ((s.rock_bored_t += dt) < 6) { react(PIP_COMFY, 1.0f, "rocked"); return; }
    if (!s.rock_told) {
        static int last;
        s.rock_told = true;
        const answer_t a[] = {{PIP_BORED, 2, "rocked: bored", 2.5f}, {PIP_PLAYFUL, s.pet.fun > 0.4f ? 1.5f : 0.4f, "rocked: whee", 2.5f},
                              {PIP_ANNOYED, 1, "rocked: not sleepy", 2.0f}};
        answer(a, 3, &last);
    }
}

/* A boop on the nose (a tap right between the eyes): he looks down at it cross-eyed, then snorts - or giggles, or it sets
   off a sneeze, or (booped a lot) a cross snort. Like the sneeze, a face over the mood for a moment, its own springs */
#define BOOP_SNORT 0.55f
#define BOOP_LEN 1.15f
static void boop(void)
{
    if (s.boop_anim >= 0 || s.sneeze_anim >= 0 || s.burp_anim >= 0) return;
    static int last;
    const answer_t a[] = {{PIP_SURPRISED, 3, "boop", BOOP_LEN}, {PIP_HAPPY, s.pet.fun > 0.4f ? 1.5f : 0.3f, "boop", BOOP_LEN},
                          /* a sneeze: rarer than it was (0.3 / 0.8: booped a few times he sneezed nearly every other
                             time), none while his nose is still clear from the last (SNEEZE_REST, as pokes) */
                          {PIP_SURPRISED, !FEAT(PIP_F_SNEEZE) || s.t - s.last_sneeze < SNEEZE_REST ? 0 : s.itch > 1 ? 0.5f : 0.2f, "boop", BOOP_LEN},{PIP_ANNOYED, irk() > 0.5f ? 1.5f : 0.05f, "boop", BOOP_LEN}};
    s.boop_kind = pick(a, 4, &last);
    s.boop_anim = 0, s.annoy += 0.5f, s.itch += 0.5f; /* booped and booped it gets old; it tickles his nose */
    s.tgx = s.tgy = 0, s.next_look = BOOP_LEN + 0.3f;
    if (s.t - s.boop_at > 10) s.boop_at = s.t, pet_event(&s.pet, PET_TICKLE); /* a little fun, once in a while */
    if (s.boop_kind < 2 && irk() < 0.4f && s.pet.love > 0.6f && vary_to(0, 1, 0.2f)) { /* a kiss for it, or he plays the fool */
        s.boop_anim = -1;
        if (s.pet.fun > 0.6f && life_rand(0, 1) < 0.4f) react(PIP_SILLY, 3.2f, "clowns");
        else react(PIP_KISS, 2.8f, "kisses");
        return;
    }
    react(a[s.boop_kind].mood, BOOP_LEN, "boop");
}
static const mood_def_t *boop_face(float dt, const mood_def_t *base)
{
    static mood_def_t f;
    float t0 = s.boop_anim, t = s.boop_anim += dt;
    f = *base;
    float in = fminf(1, t / 0.15f), out = t < BOOP_SNORT ? 1 : fmaxf(0, 1 - (t - BOOP_SNORT) / (BOOP_LEN - BOOP_SNORT));
    f.cross = 0.6f * in * out, f.gx = 0, f.gy = 0.55f * in * out, f.look_amp = 0.01f, f.look_rate = 0.01f; /* at the end of his nose */
    if (t0 < BOOP_SNORT && t >= BOOP_SNORT) {
        if (s.boop_kind == 1) say(VOICE_GIGGLE), react(PIP_LAUGHING, 1.0f, "boop");
        else if (s.boop_kind == 2) { s.boop_anim = -1, pip_sneeze(); return base; } /* the tickle wins: a sneeze */
        else say(VOICE_SNORT), s.squash.v -= 3, blink();
    }
    if (t >= BOOP_LEN) s.boop_anim = -1;
    return &f;
}

/* the finger comes down: found out pretending to sleep he jumps up "boo!"; playful and sure of you, now and then he
   ducks a finger aimed at an eye and laughs at the miss */
static void life_press(float x, float y)
{
    s.dodged = false;
    if (s.fake_on) {
        s.fake_on = false;
        if (vary_to(0, 1, 0.35f)) show_react(PIP_MAGICIAN), pet_event(&s.pet, PET_TICKLE); /* ta-da: he was up to this all along */
        else react(PIP_PLAYFUL, 2.0f, "boo"), s.squash.v -= 6, pet_event(&s.pet, PET_TICKLE);
        return;
    }
    if (pet_tune.vary <= 0 || s.pet.asleep || s.dark || eye_at(x, y) < 0 || s.t - s.tease_at < 25) return;
    if (pet_valence(&s.pet) > 0.1f && s.pet.fun > 0.35f && s.pet.trust > 0.45f && life_rand(0, 1) < 0.22f) {
        s.dodged = true, s.tease_at = s.t, s.squash.v -= 3;
        s.dodge_yaw = (x < SCR / 2 ? 1 : -1) * 0.5f;
    }
}

/* where on him a touch lands, beside the eyes, the nose and the top of his head (HEAD_Y): his cheeks (under each eye),
   his chin (under both, in the middle) and his sides (the rim left and right of the eyes) */
enum { Z_NONE, Z_CHEEK_L, Z_CHEEK_R, Z_CHIN, Z_SIDE_L, Z_SIDE_R };
#define CHEEK_Y 338.0f /* under the eyes (and under their bottom edge, whatever their shape just now) */
#define CHIN_W 58.0f   /* half the chin's width */
#define SIDE_R 178.0f  /* the rim: this far from the middle, beside the eyes */
static int zone_at(float x, float y)
{
    if (pet_tune.zones <= 0) return Z_NONE; /* off: a plain poke and stroke anywhere, as before */
    float dx = x - SCR / 2, dy = y - SCR / 2;
    /* the rim, but not an eye: a wide eye's outer third reaches past SIDE_R (centres 118 px out, ~85 px half wide), and a
       poke there was a ticklish "side poke" - a laugh for a finger in the eye */
    if (dx * dx + dy * dy > SIDE_R * SIDE_R && fabsf(dy) < 115) return eye_at(x, y) >= 0 ? Z_NONE : dx < 0 ? Z_SIDE_L : Z_SIDE_R;
    float bottom = fmaxf(s.face.eye[0].y + s.face.eye[0].h * 0.5f, s.face.eye[1].y + s.face.eye[1].h * 0.5f) + 4;
    if (y < fmaxf(CHEEK_Y, bottom)) return Z_NONE;
    return fabsf(dx) < CHIN_W ? Z_CHIN : dx < 0 ? Z_CHEEK_L : Z_CHEEK_R;
}
static bool zone_lean(void) /* stroked where he leans into the hand: the top of his head, his chin or a cheek */
{
    int z = zone_at(s.fx, s.fy);
    return s.petting && s.touching && (s.fy < HEAD_Y || z == Z_CHIN || z == Z_CHEEK_L || z == Z_CHEEK_R);
}
/* a tap on a cheek (flustered, shy), on the chin (chin up, glad) or in his side (ticklish: he jumps away); kind ones give a
   little fun now and then, and all of them wear thin like pokes (s.annoy): past that a plain poke takes over */
static bool zone_tap(int z)
{
    if (z == Z_NONE || s.greet_t > 0 || s.sneeze_anim >= 0 || pet_drowsy(&s.pet, s.tz) || s.annoy + 1 >= POKE_FED_UP) return false;
    static int last[3];
    float l = s.pet.love, f = s.pet.fun, ir = irk();
    s.annoy += 0.6f;
    if (z == Z_SIDE_L || z == Z_SIDE_R) {
        const answer_t a[] = {{PIP_LAUGHING, f > 0.4f ? 2 : 0.5f, "side poke", 1.2f}, {PIP_FLUSTERED, 1, "side poke", 1.2f},
                              {PIP_SURPRISED, 1.5f, "side poke", 0.9f}, {PIP_ANNOYED, ir > 0.5f ? 1.5f : 0.05f, "side poke", 1.0f},
                              {PIP_SNICKER, f > 0.4f ? 0.9f : 0.1f, "snickers", 4.2f}};
        answer(a, 5, &last[0]);
        s.head_yaw.v += (z == Z_SIDE_L ? 1 : -1) * 7, s.squash.v -= 3; /* away from it, with a jump */
    } else if (z == Z_CHIN) {
        const answer_t a[] = {{PIP_HAPPY, 2, "chin", 1.2f}, {PIP_CHEEKY, 1, "chin", 1.2f}, {PIP_BLISSFUL, l > 0.6f ? 1 : 0.2f, "chin", 1.4f},
                              {PIP_ANNOYED, ir > 0.5f ? 1.5f : 0.05f, "chin", 1.0f}, {PIP_FLIRTY, l > 0.5f ? 0.9f : 0.1f, "flirts", 3.0f}};
        answer(a, 5, &last[1]);
        s.head_pitch.v -= 3.5f, s.squash.v -= 1.5f; /* chin up */
    } else {
        const answer_t a[] = {{PIP_FLUSTERED, 3, "cheek", 1.4f}, {PIP_SHY, l > 0.6f ? 1.2f : 0.3f, "cheek", 1.4f},
                              {PIP_BLISSFUL, l > 0.7f ? 0.8f : 0.1f, "cheek", 1.4f}, {PIP_ANNOYED, ir > 0.5f ? 1.5f : 0.05f, "cheek", 1.0f},
                              {PIP_FLIRTY, l > 0.5f ? 1.2f : 0.1f, "flirts", 3.0f}, {PIP_PAWS, l > 0.4f ? 0.8f : 0.2f, "hides in paws", 5.2f}};
        answer(a, 6, &last[2]);
        s.head_roll.v += (z == Z_CHEEK_L ? 1 : -1) * 1.2f;
    }
    if (s.t - s.zone_paid > 10) s.zone_paid = s.t, pet_event(&s.pet, PET_TICKLE); /* a little fun, once in a while */
    return true;
}

/* a finger held still on an eye: that eye screwed shut under it, his head pulling away, cross (crosser if it stays) */
#define EYE_HOLD_S 0.6f
static void eye_held(float dt, float held)
{
    s.eye_pressed = true;
    s.squint_t[s.press_eye] = SQUINT_IN; /* shut, as long as the finger stays */
    s.dodge_yaw += ((s.press_x < SCR / 2 ? 1 : -1) * 0.22f - s.dodge_yaw) * fminf(1, dt * 3);
    if (s.eye_hold_mood < 0) {
        static int last;
        const answer_t a[] = {{PIP_ANNOYED, 2.5f, "finger in eye", 0.8f}, {PIP_INDIGNANT, 1, "finger in eye", 0.8f},
                              {PIP_OUCH, 1, "finger in eye", 0.8f}, {PIP_SKEPTICAL, s.pet.trust > 0.6f ? 0.6f : 0.1f, "finger in eye", 0.8f}};
        s.eye_hold_mood = a[pick(a, 4, &last)].mood;
    }
    if (held > 3 && !s.eye_hold_cross) s.eye_hold_cross = true, s.eye_hold_mood = PIP_ANGRY, pet_event(&s.pet, PET_POKE);
    react(s.eye_hold_mood, 0.8f, "finger in eye");
}

/* the ninja is a rare one: a merry Pip, and not again within NINJA_GAP_S */
#define NINJA_GAP_S 180.0f
static float ninja_at = -NINJA_GAP_S;
static bool ninja_ok(void) { return s.pet.fun > 0.4f && s.t - ninja_at >= NINJA_GAP_S; }
/* a tap: dodged (missed me!), a boop on the nose, a cheek, the chin, a side; anything else is a poke (false) */
static bool life_tap(float x, float y)
{
    if (s.dodged) {
        static int last;
        const answer_t a[] = {{PIP_CHEEKY, 2, "dodged", 1.6f}, {PIP_SNICKER, 1.1f, "snickers", 4.2f}, {PIP_IMP, 0.7f, "imp", 3.4f},
                              {PIP_NINJA, ninja_ok() ? 0.15f : 0, "ninja", 4.2f}};
        if (answer(a, 4, &last) == 3) ninja_at = s.t;
        return true;
    }
    if (s.pet.asleep) return false;
    /* his nose: the narrow gap between the eyes (an eye's own slack reaches into it: the nose comes first) */
    const eye_t *a = &s.face.eye[0], *b = &s.face.eye[1];
    float mx = (a->x + b->x) / 2, my = (a->y + b->y) / 2;
    if (fabsf(x - mx) < 30 && fabsf(y - my) < 60) { boop(); return true; }
    return zone_tap(zone_at(x, y));
}

/* a finger drawn slowly down over his eyes closes them: sleepy (worn out, at night, rocked a while) he lets them and is
   off to sleep; awake he pops them open again, or peeks, or tells you he is not sleepy */
static void eyes_down(void)
{
    if (s.pet.asleep) return;
    float p = (1 - s.pet.energy) * 1.3f - 0.25f + (pet_drowsy(&s.pet, s.tz) ? 0.35f : 0) + (night_now() ? 0.25f : 0) + s.lull * 0.3f;
    bool sleeps = pet_tune.vary > 0 ? life_rand(0, 1) < fminf(0.97f, p) : p >= 0.5f;
    if (sleeps) {
        blink();
        pet_event(&s.pet, PET_LULLED), pet_dirty = true;
        react(PIP_SLEEPY, 1.2f, "eyes closed");
        return;
    }
    static int last;
    const answer_t a[] = {{PIP_SURPRISED, 2, "eyes: pop open", 1.0f}, {PIP_CHEEKY, s.pet.fun > 0.4f ? 1.5f : 0.3f, "eyes: peek", 1.5f},
                          {PIP_ANNOYED, s.pet.energy > 0.7f ? 1.2f : 0.4f, "eyes: not sleepy", 1.2f}, {PIP_SKEPTICAL, 1, "eyes: hm", 1.0f}};
    answer(a, 4, &last);
}

/* drawn up over them: asleep, he is woken - he prises them open with an effort and grumbles; drowsy, a grumble; awake,
   wide eyes (or a giggle) */
static void eyes_up(void)
{
    if (s.pet.asleep) {
        int r = pet_event(&s.pet, PET_WAKE);
        light_up(), pet_dirty = true;
        react(r >= 0 ? r : PIP_SLEEPY, 2.5f, "eyes pried open");
        return;
    }
    if (pet_drowsy(&s.pet, s.tz) || s.pet.energy < 0.35f) { react(PIP_ANNOYED, 1.6f, "eyes pried open"); return; }
    static int last;
    const answer_t a[] = {{PIP_SURPRISED, 2, "eyes: wide", 1.0f}, {PIP_AMAZED, 1, "eyes: wide", 1.2f}, {PIP_LAUGHING, s.pet.fun > 0.5f ? 1 : 0.2f, "eyes: tickles", 0.9f}};
    answer(a, 3, &last);
}

/* a quick flick across him: his eyes dart after it - startled, or it is a game, or it makes him giddy, or cross */
static void flick(float dx, float dy)
{
    static int last;
    pip_glance(s.fx + dx * 0.6f, s.fy + dy * 0.6f, 0.5f);
    s.head_yaw.v += dx * 0.02f, s.annoy += 0.3f;
    const answer_t a[] = {{PIP_SURPRISED, 2, "flick", 0.8f}, {PIP_PLAYFUL, s.pet.fun > 0.4f ? 1.5f : 0.3f, "flick: catch", 1.2f},
                          {PIP_DIZZY, 0.5f, "flick: whoosh", 1.0f}, {PIP_ANNOYED, irk() > 0.4f ? 1.2f : 0.05f, "flick: hey", 1.0f},
                          {PIP_CURIOUS, 1, "flick: what", 1.0f}, {PIP_NINJA, ninja_ok() ? 0.12f : 0, "ninja", 4.2f}};
    if (answer(a, 6, &last) == 5) ninja_at = s.t;
}

/* the finger lifted after more than a tap: a slow stroke down or up over the eyes, or a quick flick */
static void life_swipe(uint32_t ms)
{
    if (s.petting || s.chasing) return;
    float dx = s.fx - s.press_x, dy = s.fy - s.press_y;
    const eye_t *a = &s.face.eye[0], *b = &s.face.eye[1];
    float ey = (a->y + b->y) / 2, eh = fmaxf(a->h, b->h) * 0.5f * 0.3f;
    bool upright = fabsf(dy) > 100 && fabsf(dx) < 0.6f * fabsf(dy) && s.path < 1.6f * fabsf(dy);
    if (upright && ms > 350 && ms < 3000) {
        if (dy > 0 && s.press_y < ey - eh && s.fy > ey + eh) { eyes_down(); return; }
        if (dy < 0 && s.press_y > ey + eh && s.fy < ey - eh) { eyes_up(); return; }
    }
    if (ms < 300 && s.path > 60 && !s.pet.asleep) flick(dx, dy);
}

/* A spiral drawn on him with a finger: the finger keeps turning the same way while its loops grow (or shrink). Cheap: the
   finger is sampled every SPIRAL_STEP px of travel (not every frame), the turn between two samples' segments is one
   atan2f of their cross and dot products; each half turn's loop size is its arc over its angle (a drifting centre does
   not matter). SPIRAL_HALVES half turns in a row, each loop bigger (or smaller) than the last, the last SPIRAL_GROW
   times the first: a spiral. Round and round in loops of one size counts too, from CIRCLE_HALVES half turns (two turns:
   a short round stroke stays a stroke) and loops of CIRCLE_R px (a smaller one rubbed is tickling). A stroke back and
   forth or a rubbed patch turns back on itself (that starts it over), a chase zigzags. Drawn inwards he is hypnotized (or
   it makes him drowsy), outwards giddy or amazed, round and round dizzy or hypnotized; or he laughs, is puzzled, follows
   it intently, is cross, nervous if he does not trust you - by how he is */
#define SPIRAL_STEP 14.0f  /* px of finger travel between samples: longer segments, less of a finger's jitter in the angle */
#define SPIRAL_HALVES 3    /* half turns of growing loops: one and a half turns */
#define SPIRAL_GROW 1.45f  /* the last loop against the first (or 1/that, drawn inwards) */
#define SPIRAL_SLACK 0.85f /* a loop may come out this much smaller than the last and still be growing */
#define SPIRAL_BACK 1.0f   /* rad: turns the other way adding up past this (a wobble's come and go) start it over */
#define SPIRAL_LEAD 3.2f   /* rad: the finger this far round one way is not stroking (half of it: no stroke starts) */
#define CIRCLE_HALVES 4    /* half turns round and round in loops of one size: two turns */
#define CIRCLE_R 20.0f     /* px: the smallest loop that counts (smaller is rubbing one patch) */
static struct {
    float x, y, dx, dy;   /* the last sample, the segment into it */
    bool seg;             /* there is a segment to turn from */
    float turn, arc, ang; /* signed turn so far; arc and angle of the half turn under way */
    float back;           /* turning the other way lately (wobbles fade, a real reversal adds up) */
    float r[SPIRAL_HALVES + 1];
    int n;                /* half turns' loop sizes kept, oldest first */
    int halves;           /* half turns the same way since it last started over */
    float rmin;           /* the smallest loop of those */
    int samples, resets, best; /* for the debug link: this touch's samples, start-overs, most half turns in a row */
    float maxa, rs[6];         /* the sharpest turn, the last loop sizes */
    int nrs;
    int on_halves;             /* half turns since he noticed: how much he was spun */
    float half_at, ang_v;      /* when the last half turn ended; the finger's turning speed, rad/s */
    float wz_phase, wz_w;      /* woozy after it: where his gaze is on its way round, and how fast (slowing) */
    bool on;              /* this touch is a spiral: no stroking, no chase */
    int mood;             /* his answer, held while the finger goes on */
    const char *name;
} sp;
static void spiral_reset(float x, float y)
{
    sp.x = x, sp.y = y, sp.seg = false, sp.turn = sp.arc = sp.ang = sp.back = 0, sp.n = sp.halves = 0;
    sp.samples = sp.resets = sp.best = sp.nrs = 0, sp.maxa = 0;
}
/* the last touch as the spiral reader saw it (debug link @S "sp") */
const char *pip_spiral_note(void)
{
    static char b[96];
    int k = snprintf(b, sizeof b, "%d %d %d %.1f %d", sp.samples, sp.resets, sp.best, sp.maxa, sp.on);
    for (int i = 0; i < sp.nrs && k < (int)sizeof b - 8; i++) k += snprintf(b + k, sizeof b - k, " %.0f", sp.rs[i]);
    return b;
}
static int spiral_kind; /* 1 outwards, -1 inwards, 0 round and round */
static void spiral_answer(int kind)
{
    bool inward = kind < 0;
    static int last;
    const pet_t *p = &s.pet;
    float tired = 1 - p->energy, v = pet_valence(p);
    spiral_kind = kind;
    /* while the finger goes round: mostly his eyes ride round with it; drawn inwards it may spellbind him */
    const answer_t a[] = {{PIP_WHIRLED, 3, "spiral", 2.0f},
                          {PIP_HYPNOTIZED, inward ? 2.5f : 0.5f, "spiral: spellbound", 2.5f},
                          {PIP_SLEEPY, inward && tired > 0.5f ? 1.5f : 0.1f, "spiral: drowsy", 2.2f},
                          {PIP_LAUGHING, p->fun > 0.6f ? 0.4f : 0.1f, "spiral: tickles", 1.4f},
                          {PIP_PUZZLED, 0.6f, "spiral: what", 1.6f},
                          {PIP_ANNOYED, irk() > 0.5f || v < -0.3f ? 1.4f : 0.05f, "spiral: stop", 1.4f},
                          {PIP_NERVOUS, p->trust < 0.35f ? 1.2f : 0.05f, "spiral: uneasy", 1.6f}};
    int k = answer(a, 7, &last);
    sp.mood = a[k].mood, sp.name = a[k].name;
    sp.on_halves = 0, sp.half_at = s.t, sp.ang_v = 0;
    s.annoy += 0.3f;
    if (sp.mood == PIP_WHIRLED) s.squash.v -= 2.5f;
    if (s.t - spiral_at > 30 && sp.mood != PIP_ANNOYED && sp.mood != PIP_NERVOUS) spiral_at = s.t, pet_event(&s.pet, PET_TICKLE);
}
/* a finger sample while pressed (touch_cb) */
static void spiral_feed(float x, float y)
{
    float dx = x - sp.x, dy = y - sp.y, d2 = dx * dx + dy * dy;
    if (d2 < SPIRAL_STEP * SPIRAL_STEP) return;
    float len = sqrtf(d2);
    sp.x = x, sp.y = y;
    sp.samples++;
    if (!sp.seg) { sp.dx = dx, sp.dy = dy, sp.seg = true; return; }
    float a = atan2f(sp.dx * dy - sp.dy * dx, sp.dx * dx + sp.dy * dy); /* the turn from the last segment to this one */
    sp.dx = dx, sp.dy = dy;
    sp.maxa = fmaxf(sp.maxa, fabsf(a));
    bool against = sp.turn != 0 && a * sp.turn < 0;
    sp.back = against ? sp.back + fabsf(a) : fmaxf(0, sp.back - fabsf(a) * 0.5f);
    if (fabsf(a) > 2.0f || sp.back > SPIRAL_BACK) { /* turned back on itself */
        sp.turn = sp.arc = sp.ang = sp.back = 0, sp.n = sp.halves = 0, sp.resets++;
        return;
    }
    sp.turn += a, sp.arc += len, sp.ang = fmaxf(0, sp.ang + (sp.turn < 0 ? -a : a));
    if (sp.ang < 3.14159265f) return;
    /* a half turn done: its loop size; kept while they keep growing (or shrinking) the same way */
    float r = sp.arc / sp.ang;
    sp.arc = sp.ang = 0;
    if (sp.on) { /* he sways round with it */
        s.head_roll.v += sp.turn > 0 ? 0.35f : -0.35f, sp.on_halves++;
        if (s.t > sp.half_at) sp.ang_v = 3.14159265f / (s.t - sp.half_at);
        sp.half_at = s.t;
        return;
    }
    if (sp.n == SPIRAL_HALVES + 1) memmove(sp.r, sp.r + 1, sizeof(float) * SPIRAL_HALVES), sp.n--;
    sp.r[sp.n++] = r;
    sp.rmin = sp.halves++ ? fminf(sp.rmin, r) : r;
    if (sp.halves > sp.best) sp.best = sp.halves;
    if (sp.nrs == 6) memmove(sp.rs, sp.rs + 1, sizeof(float) * 5), sp.nrs--;
    sp.rs[sp.nrs++] = r;
    /* the run of same-way loops ending with this one */
    int i = sp.n - 1;
    bool up = sp.n > 1 && sp.r[i] > sp.r[i - 1];
    while (i > 0 && (up ? sp.r[i] > sp.r[i - 1] * SPIRAL_SLACK : sp.r[i - 1] > sp.r[i] * SPIRAL_SLACK)) i--;
    if (i > 0) memmove(sp.r, sp.r + i, sizeof(float) * (sp.n - i)), sp.n -= i;
    float grow = sp.n > 1 ? sp.r[sp.n - 1] / sp.r[0] : 1;
    bool spiral = sp.n >= SPIRAL_HALVES && sp.r[0] >= 10 && (grow >= SPIRAL_GROW || grow <= 1 / SPIRAL_GROW);
    bool circle = sp.halves >= CIRCLE_HALVES && sp.rmin >= CIRCLE_R;
    if ((!spiral && !circle) || s.pet.asleep || s.dark) return;
    sp.on = true, s.petting = false, s.chase_t = 0, s.chasing = false, sp.half_at = s.t;
    spiral_answer(!spiral ? 0 : grow < 1 ? -1 : 1);
}
/* where the finger is on its loop, as his gaze: from the loop's centre (it lies on the side the finger turns to) */
static void spiral_gaze(float *x, float *y)
{
    float l = sqrtf(sp.dx * sp.dx + sp.dy * sp.dy) + 1e-3f, sg = sp.turn < 0 ? -1 : 1;
    *x = sg * sp.dy / l, *y = -sg * sp.dx / l;
}
/* the finger lifted after he was spun round a while: the after-effect - mostly woozy (the gaze goes on round by itself,
   slowing, the eyes out of step), spellbound if it was drawn inwards, or dizzy, or he wants it again, or it was enough */
static void spiral_release(void)
{
    if (!sp.on || sp.on_halves < 2 || s.pet.asleep) return;
    static int last;
    const pet_t *p = &s.pet;
    float spun = fminf(3, sp.on_halves / 4.0f), v = pet_valence(p);
    const answer_t a[] = {{PIP_WOOZY, 3 + spun, "spun", 3.2f},
                          {PIP_HYPNOTIZED, spiral_kind < 0 ? 2.5f : 0.5f, "spun: spellbound", 3.0f},
                          {PIP_DIZZY, 0.6f + spun * 0.4f, "spun: dizzy", 2.5f},
                          {PIP_LAUGHING, p->fun > 0.6f ? 0.5f : 0.1f, "spun: again", 1.6f},
                          {PIP_RELIEVED, 0.6f, "spun: phew", 1.6f},
                          {PIP_ANNOYED, irk() > 0.6f || v < -0.3f ? 1.2f : 0.05f, "spun: enough", 1.5f}};
    answer(a, 6, &last);
    float x, y;
    spiral_gaze(&x, &y);
    sp.wz_phase = atan2f(y, x);
    sp.wz_w = (sp.turn < 0 ? -1 : 1) * fmaxf(3, fminf(10, sp.ang_v));
}

/* something of his own, now and then, when nobody is doing anything to him: by how he is - he looks round, yawns,
   hums, sighs, giggles to himself, ponders, gives you a shy look or a wink, hops for joy, pretends to sleep (to catch
   you out), glances about nervously, stares at something, zones out, shivers when chilled, coughs when ill, misses you */
#define COMPANY_S 45.0f /* s since you last did something to him that he still counts as company */
enum { M_LOOK, M_YAWN, M_HUM, M_SIGH, M_GIGGLE, M_SMUG, M_THINK, M_SHY, M_HOP, M_FAKE, M_NERVOUS, M_AMAZED, M_WINK, M_DULL,
       M_SHIVER, M_ILL, M_LONELY, M_CHAT, M_SCHEME, M_DREAM, M_BLISS, M_ADORE, M_WAIT, M_EYEROLL, M_HIC, M_TOOT,
       /* the little scenes (show_face) */
       M_STARS, M_SING, M_FLIRT, M_RAINBOW, M_HIDE, M_COOL, M_PUPPY, M_BUTTERFLY, M_BUBBLES, M_ROBOT, M_KITTY, M_CLOWN, M_SNICKER, M_FIRE, M_SKY,
       /* the 38 of Nov 2026 */
       M_BLOOM, M_POP, M_KING, M_JUGGLE, M_BALLOON, M_UMBRELLA, M_STRONG, M_MAGIC, M_SHEEP, M_SPROUT, M_RAIN, M_SULK, M_SPOOK, M_LOWBATT,
       M_HUNGRY, M_WILT, M_N };
/* the face of each moment, for how near it is to how he feels (-1: it has its own reason, or keeps his face) */
static const int16_t moment_mood[M_N] = {PIP_CURIOUS, PIP_SLEEPY, PIP_DREAMY, PIP_BORED, PIP_LAUGHING, PIP_SMUG, PIP_FOCUSED, PIP_SHY,
                                        PIP_HAPPY, PIP_CHEEKY, PIP_NERVOUS, PIP_AMAZED, PIP_CHEEKY, PIP_DULL, -1, -1, PIP_LONELY, -1,
                                        PIP_SLY, PIP_WISTFUL, PIP_BLISSFUL, PIP_ADORING, PIP_IMPATIENT, PIP_EYEROLL, -1, -1,
                                         PIP_STARSTRUCK, PIP_SINGING, PIP_FLIRTY, PIP_RAINBOW, PIP_PEEKING, PIP_COOL, PIP_PUPPY, PIP_BUTTERFLY,
                                         PIP_BUBBLES, PIP_ROBOT, PIP_KITTY, PIP_SILLY, PIP_SNICKER, PIP_CAMPFIRE, PIP_STARGAZING,
                                         PIP_BLOOMING, PIP_POPEYED, PIP_KING, PIP_JUGGLER, PIP_BALLOON, PIP_UMBRELLA, PIP_STRONGMAN, PIP_MAGICIAN,
                                         PIP_SHEEP, PIP_SPROUT, PIP_RAINCLOUD, PIP_SULKING, PIP_SPOOKED, PIP_LOWBATT, PIP_HUNGRY, PIP_WILTED};
/* how much he talks, the panel's "Болтливость" (pip_chat): quiet, as he is, a chatterbox */
static int chat_lvl = 1;
static const float chat_mul[3] = {0.45f, 1, 1.7f};
void pip_chat(int level) { chat_lvl = level < 0 ? 0 : level > 2 ? 2 : level; }
int pip_chat_level(void) { return chat_lvl; }
/* how much he talks just now: glad, lively and sure of you more, glum or worn out less (a cross Pip mutters a bit
   more), quieter at night - x0.3..x2.2 on his chats and how soon his next moment of his own comes */
static float chattiness(void)
{
    float c = 1 + 0.9f * s.affect + 0.6f * (s.pet.energy - 0.5f) + 0.6f * (s.pet.trust - 0.5f) + 0.15f * fminf(s.pet.annoy, 2);
    if (night_now()) c *= 0.6f;
    return c < 0.3f ? 0.3f : c > 2.2f ? 2.2f : c;
}

/* out walking, or just back: steps lately, or the walk need still full; with half the day's walk done today */
static bool walked_lately(void)
{
    return (s.pet.stride > pet_tune.walk_now || s.pet.walk > 0.8f) && s.pet.steps_today >= pet_walk_goal() / 2;
}
static void life_moment(void)
{
    static int last = -1;
    float v = pet_valence(&s.pet), e = s.pet.energy, f = s.pet.fun, l = s.pet.love, tr = s.pet.trust;
    bool company = s.t - s.input_at < COMPANY_S;
    int hour = pet_hour(s.pet.last, s.tz), month = pet_month(s.pet.last, s.tz);
    bool vy = pet_tune.vary > 0, eve = hour >= 20 || hour < 6, walking = walked_lately();
    bool full = s.pet.food > 0.5f && e > 0.4f && f > 0.5f && l > 0.5f && s.pet.clean > 0.5f; /* nothing he is short of */
    float w[M_N] = {
        [M_LOOK] = 2.5f, [M_YAWN] = (1 - e) * 3 + (night_now() ? 1 : 0), [M_HUM] = v > 0.1f ? 1.4f : 0.3f, [M_SIGH] = (1 - f) * 2,
        [M_GIGGLE] = v > 0.4f ? 1 : 0.1f, [M_SMUG] = 0.7f, [M_THINK] = 1, [M_SHY] = l > 0.65f ? 1 : 0.2f, [M_HOP] = v > 0.5f ? 1 : 0.1f,
        [M_FAKE] = tr > 0.5f && f > 0.4f && e > 0.3f ? 0.8f : 0, [M_NERVOUS] = tr < 0.35f ? 1.5f : 0.1f, [M_AMAZED] = 0.7f,
        [M_WINK] = v > 0 ? 0.7f : 0.1f, [M_DULL] = f < 0.3f ? 1 : 0.1f, [M_SHIVER] = s.chill > 0.3f ? 3 : 0,
        [M_ILL] = pet_ill > 0.5f ? 3 : 0, [M_LONELY] = l < 0.3f ? 1.5f : 0.1f,
        [M_CHAT] = 6.0f * chat_mul[chat_lvl] * chattiness() * (company ? 2.2f : 1), /* about twice as often as he used to */
        [M_SCHEME] = v > 0.2f && f > 0.4f ? 1 : 0.15f, [M_DREAM] = night_now() ? 1.2f : 0.7f, [M_BLISS] = v > 0.5f && e > 0.3f ? 1.1f : 0.1f,
        [M_ADORE] = company && l > 0.6f && tr > 0.5f ? 1.6f : l > 0.7f ? 0.5f : 0.05f,
        [M_WAIT] = company && (f < 0.6f || l < 0.6f) ? 1.6f : 0.15f, [M_EYEROLL] = v < 0 || s.pet.annoy > 0.5f ? 1 : 0.2f,
        [M_HIC] = pet_tune.vary > 0 ? 0.3f : 0, [M_TOOT] = pet_tune.vary > 0 && FEAT(PIP_F_TOOT) ? (s.pet.food > 0.6f ? 0.3f : 0.08f) : 0,
        [M_STARS] = v > 0.6f ? 0.6f : 0.03f, [M_SING] = night_now() ? 0.1f : v > 0.2f ? 1 : 0.1f, [M_FLIRT] = l > 0.6f && v > 0.2f ? 0.7f : 0.05f,
        [M_RAINBOW] = bday_today() ? 2 : full && v > 0.4f ? 0.8f : 0, [M_HIDE] = tr > 0.4f && f > 0.3f ? 0.9f : 0.1f, [M_COOL] = v > 0.4f ? 0.8f : 0.05f,
        [M_PUPPY] = s.pet.food < 0.5f || l < 0.5f ? 1.2f : company ? 0.3f : 0.1f, [M_BUTTERFLY] = hour >= 8 && hour < 20 ? 0.8f : 0,
        [M_BUBBLES] = s.wet > 0.1f ? 2 : s.pet.clean > 0.85f ? 0.4f : 0,
        [M_ROBOT] = f < 0.4f ? 0.1f : s.pet.charging && s.pet.last - s.pet.charge_since > 600 ? 1.2f : 0.6f,
        [M_KITTY] = l > 0.5f ? 0.7f : 0.1f, [M_CLOWN] = f > 0.5f && v > 0.3f ? 0.8f : 0.05f, [M_SNICKER] = v > 0.4f ? 0.5f : 0.05f,
        [M_FIRE] = s.chill > 0.15f ? 3 : hour >= 19 || hour < 6 ? 1 : s.pet.charging ? 0.6f : 0, [M_SKY] = hour >= 21 || hour < 5 ? 1.4f : 0,
        /* the 38 (never at vary 0) */
        [M_BLOOM] = !vy ? 0 : v > 0.3f ? (month >= 3 && month <= 8 ? 0.8f : 0.25f) : 0, [M_POP] = vy ? 0.35f : 0,
        [M_KING] = vy && v > 0.65f ? 0.6f : 0, [M_JUGGLE] = !vy ? 0 : f > 0.6f && v > 0.4f ? 0.6f : 0.03f,
        [M_BALLOON] = vy && hour >= 9 && hour < 20 ? 0.4f : 0, [M_UMBRELLA] = !vy ? 0 : s.wet > 0.1f ? 1.5f : month >= 9 && month <= 11 ? 0.6f : 0,
        [M_STRONG] = !vy ? 0 : s.pet.stride > pet_tune.walk_now ? 1.6f : walking ? 0.8f : 0,
        [M_MAGIC] = !vy ? 0 : f > 0.5f && v > 0.3f ? 0.6f : 0.03f, [M_SHEEP] = !vy || !eve ? 0 : e < 0.5f ? 1.6f : 0.5f,
        [M_SPROUT] = vy && month >= 3 && month <= 5 ? 0.8f : 0,
        [M_RAIN] = !vy ? 0 : s.pet.day_mood < -0.3f || l < 0.35f ? 1.2f : 0.05f, [M_SULK] = vy && s.pet.grudge > pet_tune.grudge_show ? 1.5f : 0,
        [M_SPOOK] = vy && (hour >= 22 || hour < 5) && tr < 0.4f ? 1 : 0,
        [M_LOWBATT] = vy && ((s.bat_pct >= 0 && s.bat_pct < 10 && !s.bat_charging) || e < 0.12f) ? 2 : 0,
        [M_HUNGRY] = vy && s.pet.food < 0.3f ? 1.5f : 0, [M_WILT] = !vy ? 0 : l < 0.3f ? 1 : hour >= 18 && v < -0.2f ? 0.8f : 0,
    };
    for (int i = 0; i < M_N; i++) /* near how he feels: no giggle from a glum Pip, no sigh from a glad one */
        if (moment_mood[i] >= 0) w[i] *= affect_fit(moment_mood[i], 0.35f);
    if (last >= 0) w[last] *= 0.2f;
    float tot = 0;
    for (int i = 0; i < M_N; i++) tot += w[i];
    float x = life_rand(0, tot);
    int k = 0;
    for (; k < M_N - 1 && x >= w[k]; k++) x -= w[k];
    last = k;
    switch (k) {
    case M_CHAT: { /* in the mood, he may go on: a second sentence after a breath */
        float c = chattiness();
        if (talk(s.shown, 2.8f) && c > 1.2f && life_rand(0, 1) < 0.45f * (c - 1) && s.chat_again <= 0)
            s.chat_again = s.talk_t + life_rand(0.4f, 0.9f);
        break;
    }
    case M_LOOK: react(PIP_CURIOUS, 2.4f, "looks around"), pip_glance(70, 215, 0.7f), s.glance2_t = 0.9f; break;
    case M_YAWN: react(PIP_SLEEPY, 3.0f, "yawns"); break;
    case M_HUM: react(PIP_DREAMY, 4.0f, "hums"); break;
    case M_SIGH: if (vary_to(0, 1, 0.4f)) show_react(PIP_TUMBLEWEED); else react(PIP_BORED, 3.2f, "sighs"); break; /* nothing happens. at all */
    case M_GIGGLE: react(PIP_LAUGHING, 1.6f, "giggles"); break;
    case M_SMUG: react(PIP_SMUG, 3.0f, "smug"); break;
    case M_THINK: /* and now and then it comes to him: "aha!" */
        react(life_rand(0, 1) < 0.5f ? PIP_PUZZLED : PIP_FOCUSED, 3.2f, "ponders");
        if (life_rand(0, 1) < 0.5f) s.eureka_in = 2.6f;
        break;
    case M_SCHEME: if (vary_to(0, 1, 0.35f)) { show_react(PIP_IMP); break; } /* horns: he is up to something */
        react(PIP_SLY, 2.8f, "schemes"), pip_glance(life_rand(0, 1) < 0.5f ? 90 : 376, 230, 1.4f); break;
    case M_DREAM: react(PIP_WISTFUL, 4.0f, "daydreams"); break;
    case M_BLISS: react(PIP_BLISSFUL, 3.2f, "bliss"); break;
    case M_ADORE: react(PIP_ADORING, 2.8f, "adores you"); break;
    case M_WAIT: react(PIP_IMPATIENT, 2.6f, "waits"); break;
    case M_EYEROLL: react(PIP_EYEROLL, 1.8f, "rolls eyes"); break;
    case M_HIC: pip_hiccups(0.2f); break;
    case M_TOOT: pip_toot(); break;
    case M_SHY: react(PIP_SHY, 3.0f, "shy look"); break;
    case M_HOP: react(PIP_HAPPY, 1.2f, "hop"), s.squash.v -= 5; break;
    case M_FAKE: s.fake_on = true, s.fake_t = life_rand(3, 6), react(PIP_ASLEEP, s.fake_t + 1, "fake sleep"); break;
    case M_NERVOUS: react(PIP_NERVOUS, 1.6f, "nervous look"), pip_glance(life_rand(0, 1) < 0.5f ? 90 : 376, 200, 0.5f); break;
    case M_AMAZED: react(PIP_AMAZED, 1.6f, "sees something"), pip_glance(life_rand(110, 356), life_rand(130, 330), 1.2f); break;
    case M_WINK: react(PIP_CHEEKY, 2.0f, "winks"); break;
    case M_DULL: react(PIP_DULL, 3.0f, "zones out"); break;
    case M_SHIVER: react(PIP_FROZEN, 1.4f, "shiver"), s.squash.v -= 2; break;
    case M_ILL:
        if (vary_to(0, 1, 0.35f)) show_react(PIP_SNIFFLES); /* a red nose, a scarf */
        else if (life_rand(0, 1) < 0.5f && FEAT(PIP_F_SNEEZE)) pip_sneeze();
        else react(PIP_SICK, 2.0f, "coughs"), s.squash.v -= 3, say(VOICE_HMPH);
        break;
    case M_LONELY: react(PIP_LONELY, 3.0f, "misses you"); break;
    case M_STARS: react(PIP_STARSTRUCK, 3.4f, "starry-eyed"); break;
    case M_SING: react(PIP_SINGING, 4.5f, "sings"); break;
    case M_FLIRT: react(PIP_FLIRTY, 4.0f, "flirts"); break;
    case M_RAINBOW: react(PIP_RAINBOW, 5.0f, "rainbow"); break;
    case M_HIDE: react(PIP_PEEKING, 4.5f, "hides"); break;
    case M_COOL: react(PIP_COOL, 5.2f, "cool"); break;
    case M_PUPPY: react(PIP_PUPPY, 4.0f, "puppy eyes"); break;
    case M_BUTTERFLY: react(PIP_BUTTERFLY, 6.0f, "butterfly"); break;
    case M_BUBBLES: react(PIP_BUBBLES, 7.0f, "bubbles"); break;
    case M_ROBOT: react(PIP_ROBOT, 4.0f, "robot"); break;
    case M_KITTY: react(PIP_KITTY, 3.6f, "purrs"); break;
    case M_CLOWN: react(PIP_SILLY, 3.2f, "clowns"); break;
    case M_SNICKER: react(PIP_SNICKER, 4.2f, "snickers"); break;
    case M_FIRE: react(PIP_CAMPFIRE, 6.0f, "campfire"); break;
    case M_SKY: react(PIP_STARGAZING, 5.2f, "stargazes"); break;
    case M_BLOOM: show_react(PIP_BLOOMING); break;
    case M_POP: show_react(PIP_POPEYED); break;
    case M_KING: show_react(PIP_KING); break;
    case M_JUGGLE: show_react(PIP_JUGGLER); break;
    case M_BALLOON: show_react(PIP_BALLOON); break;
    case M_UMBRELLA: show_react(PIP_UMBRELLA); break;
    case M_STRONG: show_react(PIP_STRONGMAN); break;
    case M_MAGIC: show_react(PIP_MAGICIAN); break;
    case M_SHEEP: show_react(PIP_SHEEP); break;
    case M_SPROUT: show_react(PIP_SPROUT); break;
    case M_RAIN: show_react(PIP_RAINCLOUD); break;
    case M_SULK: show_react(PIP_SULKING); break;
    case M_SPOOK: show_react(PIP_SPOOKED); break;
    case M_LOWBATT: show_react(PIP_LOWBATT); break;
    case M_HUNGRY: show_react(PIP_HUNGRY); break;
    case M_WILT: show_react(PIP_WILTED); break;
    }
}

static void life_step(float dt)
{
    animate(drip, N_DRIP, dt, 600);
    /* the head goes back to itself after leaning into a hand or ducking a finger */
    if (!zone_lean()) s.lean_roll *= expf(-dt * 3), s.lean_pitch *= expf(-dt * 3);
    if (!s.dodged && !s.eye_pressed) s.dodge_yaw *= expf(-dt * 4);
    bool free_now = !s.touching && !away && !s.dark && !s.pet.asleep && !unmet();
    /* "aha!": what he was pondering came to him (unless something else came up meanwhile) */
    if (s.eureka_in > 0 && (s.eureka_in -= dt) <= 0 && free_now && (s.react_mood < 0 || !strcmp(s.react_name, "ponders"))) {
        if (vary_to(0, 1, 0.5f)) show_react(PIP_LIGHTBULB); /* by turns: a light bulb over him */
        else react(PIP_EUREKA, 1.7f, "aha"), s.squash.v -= 5;
    }
    /* the sponge flung: a moment later a little sheepish about it */
    if (s.sorry_in > 0 && (s.sorry_in -= dt) <= 0 && free_now) react(PIP_SHEEPISH, 2.2f, "sorry");
    /* back after a while: a hello in words (his face stays as it is) */
    if (s.hello_in > 0 && (s.hello_in -= dt) <= 0 && !away && !s.dark && !s.pet.asleep && !unmet())
        speak(VB(BABBLE_EXCLAIM), 2, pet_valence(&s.pet) > 0 ? PIP_HAPPY : PIP_NEUTRAL), s.talk_t = 0.8f, s.talk_beat = 0, s.babble_at = s.t;
    if (s.glance2_t > 0 && (s.glance2_t -= dt) <= 0) pip_glance(396, 215, 0.7f); /* looking round: the other way */
    rock_step(dt);
    /* wet: he drips, now and then shakes the water off, and dries on his own in dry_s */
    if (s.wet > 0) {
        if (!s.blowing) s.wet = fmaxf(0, s.wet - dt / pet_tune.dry_s);
        if (s.wet > 0.08f && !s.blowing && !s.dark && !away && !s.pet.asleep && (s.drip_next -= dt) <= 0) { /* blown on: the wind takes them */
            part_t *p = free_part(drip, N_DRIP);
            const eye_t *e = &s.face.eye[life_rand(0, 1) < 0.5f];
            if (p) launch(p, e->x + life_rand(-0.35f, 0.35f) * e->w * 0.5f, e->y + e->h * 0.42f, life_rand(-15, 15), 40, 1.2f);
            s.drip_next = life_rand(0.3f, 0.9f) / (0.3f + s.wet);
        }
        if (s.wet > 0.35f && s.react_mood < 0 && !s.touching && (s.shake_off_t -= dt) <= 0) { /* a shake like a wet dog */
            s.shake_off_t = life_rand(7, 12);
            react(PIP_HAPPY, 1.0f, "shakes off"), s.head_yaw.v += 9, s.head_roll.v -= 4, s.wet = fmaxf(0, s.wet - 0.08f);
            for (int i = 0; i < 3; i++) {
                part_t *p = free_part(drip, N_DRIP);
                const eye_t *e = &s.face.eye[i & 1];
                if (p) launch(p, e->x, e->y, (i & 1 ? 1 : -1) * life_rand(120, 200), -life_rand(60, 140), 0.8f);
            }
        }
    }
    /* chilled: it wears off (stroking warms him faster), with a shiver now and then */
    if (s.chill > 0 && !s.blowing) s.chill = fmaxf(0, s.chill - dt / (s.petting && s.touching ? 8.0f : 60.0f));
    /* frozen, and left in peace: to the fire */
    if (s.warm_in > 0 && !s.blowing && (s.warm_in -= dt) <= 0 && free_now && s.react_mood < 0 && vary_to(0, 1, 0.6f)) react(PIP_CAMPFIRE, 6.0f, "campfire");
    if (s.sniffle_in > 0 && !s.blowing && (s.sniffle_in -= dt) <= 0 && free_now && s.react_mood < 0 && vary_to(0, 1, 0.6f)) show_react(PIP_SNIFFLES);
    /* ill: he sneezes more */
    if (pet_ill > 0.5f && s.next_sneeze > 60) s.next_sneeze = life_rand(20, 60);
    /* pretending to sleep: found out by a finger (life_press), or he peeks and laughs at you */
    if (s.fake_on) {
        if (s.react_mood != PIP_ASLEEP || strcmp(s.react_name, "fake sleep")) s.fake_on = false; /* something else came up */
        else if ((s.fake_t -= dt) <= 0) s.fake_on = false, react(PIP_CHEEKY, 1.6f, "peeks");
    }
    /* his own moments */
    s.life_idle = s.touching ? 0 : s.life_idle + dt;
    if (pet_tune.life_gap <= 0) return;
    if (s.life_next <= 0) s.life_next = life_rand(0.6f, 1.6f) * pet_tune.life_gap / ((0.75f + 0.35f * chattiness()) * (0.7f + 0.3f * chat_mul[chat_lvl]));
    if (s.chat_again > 0 && (s.chat_again -= dt) <= 0 && !s.touching && !away && !s.dark && !s.pet.asleep &&
        (s.react_mood < 0 || !strcmp(s.react_name, "chats") || !strcmp(s.react_name, "tells")))
        talk(s.shown, 2.4f); /* the second sentence: only if nothing else came up meanwhile */
    bool calm = !s.touching && s.react_mood < 0 && s.auto_mood && !s.washing && s.sneeze_anim < 0 && s.burp_anim < 0 && s.boop_anim < 0 &&
                s.intro < 0 && s.hatch < 0 && !unmet() && !s.dark && !away && !covered && !s.pet.asleep && !dn.on && feed_t < 0 &&
                tp.n == 0 && s.glance_left <= 0 && !rk.on && !s.blowing;
    bool company = s.t - s.input_at < COMPANY_S; /* someone with him lately: he is livelier, he talks to them */
    if (!calm || s.life_idle < LIFE_IDLE_S || s.t - s.shown_at < (company ? 7 : 12)) return; /* his face has held a while first */
    if ((s.life_next -= dt * (company ? 1.8f : 1)) > 0) return;
    s.life_next = life_rand(0.6f, 1.6f) * pet_tune.life_gap / ((0.75f + 0.35f * chattiness()) * (0.7f + 0.3f * chat_mul[chat_lvl])); /* chatty: sooner */
    life_moment();
}

/* ---- hands: two fingers, a palm (pip_fingers, from every read of the touch panel; this panel reports two points at most
   and flags a palm) - and the little scenes the newer faces play (scene_face: each a face that moves through time, built
   from the same springs and features as the rest) ---------- */
#define MT_HOLD_MS 160   /* fingers held this long together before they are a gesture (two landing a moment apart) */
#define MT_SPREAD_PX 45  /* two fingers on his eyes drawn this much closer: pinched; a finger moved this far any other way: stretched */
#define MT_PAIR_MS 450   /* two fingers on him elsewhere held this long: too many fingers */
#define MT_LOST_MS 220   /* a finger missing this long mid-gesture is lifted; less, the panel lost it a moment (measured) */
enum { MT_NONE, MT_TWO, MT_STRETCH, MT_PINCH, MT_PAIR, MT_PALM };
static struct {
    int n, most, kind;   /* fingers now, most this go, what the go was taken for */
    uint32_t start;      /* lv_tick when the second finger came */
    float x[5], y[5], d0, d; /* where they are; two: their distance at first and now */
    float sx[2], sy[2];  /* two on his eyes: where each finger came down */
    int eye_of[2];       /* ...and the eye it is on (the eye follows its finger) */
    uint32_t gone;       /* lv_tick a finger went missing mid-gesture (lost): the panel drops one for a read or two */
    bool lost;
    bool on, eyes, seen, palm; /* on: a go of fingers under way; two: one on each eye; seen: more than one finger since this press (touch_cb takes none of it) */
    float palm_at;       /* the last palm that paid (as peekaboo: PEEK_GAP_S) */
} mt = {.palm_at = -1000};
static struct {
    int mood; float t;   /* the face whose scene plays, and how long it has */
    spring_t spread, sink, scale; /* the eyes drawn apart (a fraction of the spacing), sunk (px), their size (1: as drawn) */
    spring_t ox[2], oy[2]; /* each eye pulled by its finger, px (stretched, pinched): it goes where it is drawn */
    float beat, puff, look, let_t; /* the next kick, particle, glance; s since the fingers let go (stretched, squished) */
    int k;               /* the finger the overwhelmed eyes are on */
    bool burst;          /* the scene's one-off (sparkles) done */
    float spin;          /* triumphant: the head turned right round, rad (added to its yaw) */
} sc = {.mood = -1, .scale = {1, 0}, .let_t = 100};

static void mt_end(void)
{
    if (mt.kind == MT_PALM && s.react_mood == PIP_SQUISHED) { /* the hand lifts: he pops back up */
        sc.let_t = 0;
        react(PIP_SQUISHED, 1.5f, "palm off");
        if (s.t - mt.palm_at > PEEK_GAP_S) mt.palm_at = s.t, pet_event(&s.pet, PET_PEEKABOO); /* a game: hidden under a hand */
    } else if (mt.kind == MT_STRETCH && s.react_mood == PIP_STRETCHED) {
        sc.let_t = 0;
        react(PIP_STRETCHED, 1.4f, "let go");
    } else if (mt.kind == MT_PINCH && s.react_mood == PIP_PINCHED) {
        sc.let_t = 0;
        react(PIP_PINCHED, 1.3f, "popped");
    } else if (mt.kind == MT_PAIR && s.react_mood == PIP_OVERWHELMED) react(PIP_RELIEVED, 1.6f, "fingers gone");
    else if (mt.kind == MT_PAIR && s.react_name && !strcmp(s.react_name, "two fingers")) s.react_left = fminf(s.react_left, 0.6f);
    mt.most = mt.n = 0, mt.kind = MT_NONE, mt.palm = mt.on = mt.lost = false;
}
void pip_fingers(int n, const int16_t *x, const int16_t *y, bool palm)
{
    n = n < 0 ? 0 : n > 5 ? 5 : n;
    if (n <= 1 && !palm) { /* one finger or none: touch_cb's, as ever */
        if (mt.on && n == 1 && mt.kind != MT_NONE) { /* mid-gesture the panel loses a finger now and then */
            if (!mt.lost) mt.lost = true, mt.gone = lv_tick_get();
            if (lv_tick_elaps(mt.gone) < MT_LOST_MS) return; /* held as it was: no "let go", no jump */
        }
        if (mt.on) mt_end();
        mt.n = n, mt.lost = false;
        return;
    }
    mt.lost = false;
    if (n == 2 && mt.on && mt.kind >= MT_TWO && mt.kind <= MT_PINCH) { /* the panel may hand them over the other way round */
        float keep = hypotf(x[0] - mt.x[0], y[0] - mt.y[0]) + hypotf(x[1] - mt.x[1], y[1] - mt.y[1]);
        float swap = hypotf(x[0] - mt.x[1], y[0] - mt.y[1]) + hypotf(x[1] - mt.x[0], y[1] - mt.y[0]);
        if (swap < keep) {
            static int16_t sx[2], sy[2];
            sx[0] = x[1], sx[1] = x[0], sy[0] = y[1], sy[1] = y[0], x = sx, y = sy;
        }
    }
    if (unmet() || s.dead || away || covered || s.dark) { mt.most = 0, mt.kind = MT_NONE, mt.on = false; return; } /* not his to feel */
    if (!mt.on) mt.on = true, mt.start = lv_tick_get(), mt.kind = MT_NONE;
    mt.seen = true, mt.n = n, mt.palm = mt.palm || palm, mt.most = n > mt.most ? n : mt.most;
    for (int i = 0; i < n; i++) mt.x[i] = x[i], mt.y[i] = y[i];
    s.input_at = s.t, pw.idle = 0, s.petting = false;
    if (s.pet.asleep) return; /* a sleeper feels a hand, and sleeps on */
    uint32_t held = lv_tick_elaps(mt.start);
    if (mt.palm) { /* a palm (the panel's flag): squashed flat under it */
        if (mt.kind != MT_PALM && held >= MT_HOLD_MS) mt.kind = MT_PALM, react(PIP_SQUISHED, 60, "palm"), s.squash.v += 4;
        return;
    }
    if (mt.kind == MT_PALM) return; /* the hand lifting finger by finger: still the palm */
    if (n != 2 || mt.kind == MT_PAIR) return;
    mt.d = hypotf(mt.x[1] - mt.x[0], mt.y[1] - mt.y[0]);
    if (mt.kind == MT_NONE) { /* the second finger has come: are they on his two eyes? */
        int e0 = eye_at(mt.x[0], mt.y[0]), e1 = eye_at(mt.x[1], mt.y[1]);
        mt.kind = MT_TWO, mt.d0 = mt.d, mt.eyes = e0 >= 0 && e1 >= 0 && e0 != e1;
        mt.eye_of[0] = e0, mt.eye_of[1] = e1, mt.sx[0] = mt.x[0], mt.sy[0] = mt.y[0], mt.sx[1] = mt.x[1], mt.sy[1] = mt.y[1];
    }
    float moved = fmaxf(hypotf(mt.x[0] - mt.sx[0], mt.y[0] - mt.sy[0]), hypotf(mt.x[1] - mt.sx[1], mt.y[1] - mt.sy[1]));
    if (mt.kind == MT_TWO && mt.eyes && mt.d0 - mt.d > MT_SPREAD_PX) mt.kind = MT_PINCH, react(PIP_PINCHED, 60, "pinched"); /* together */
    else if (mt.kind == MT_TWO && mt.eyes && moved > MT_SPREAD_PX) mt.kind = MT_STRETCH, react(PIP_STRETCHED, 60, "stretched"); /* any way */
    else if (mt.kind == MT_TWO && !mt.eyes && held >= MT_PAIR_MS) { /* two fingers on him, not his eyes: too many to watch */
        mt.kind = MT_PAIR;
        static int last;
        float tr = s.pet.trust, f = s.pet.fun;
        const answer_t a[] = {{PIP_OVERWHELMED, 3, "two fingers", 60}, {PIP_NERVOUS, tr < 0.4f ? 1.5f : 0.2f, "two fingers", 60},
                              {PIP_LAUGHING, f > 0.6f ? 0.6f : 0.1f, "two fingers", 60}};
        answer(a, 3, &last);
    }
}
const char *pip_fingers_trace(char *out, int n)
{
    static const char *const kinds[] = {"-", "two", "stretch", "pinch", "pair", "palm"};
    snprintf(out, n, "%d %d %s %.0f %d", mt.n, mt.most, kinds[mt.kind], mt.kind >= MT_TWO && mt.kind <= MT_PINCH ? mt.d - mt.d0 : 0, mt.eyes);
    return out;
}

/* the scene's little particles: round dots from the confetti's layer (one object; a few at once) */
static void dot(float x, float y, float vx, float vy, float life, int size, uint32_t color)
{
    for (int i = 0; i < N_CONFETTI; i++) {
        part_t *p = &confetti[i];
        if (p->life > 0) continue;
        confl.round = true, confl.w[i] = confl.h[i] = (uint8_t)size, confl.color[i] = color;
        launch(p, x, y, vx, vy, life);
        return;
    }
}
static float eye_cx(int i) { return s.face.eye[i].x; }
static float eye_cy(int i) { return s.face.eye[i].y; }

/* ---- the little scenes with pictures (moods.h after PIP_GROGGY): the same springs and features as the scenes below, and
   sprites (sprite.c) - hearts, stars, a butterfly, a fire: small flat ready-made pictures, a few thousand pixels a frame.
   Each loops on its own clock (t, s since the face came), so it can be shown for as long as you like ---------- */
typedef struct { float spread, sink, scale, stiff, zeta, (*pull)[2]; } scene_out_t;
static struct {
    bool live;            /* one plays this frame */
    bool out, gaze;       /* it may leave the glass (peeking); it says where he looks: gx, gy */
    float gx, gy, next[4]; /* every(): s to the next one of each kind */
} show;
static bool hits(float t, float dt, float x) { return t >= x && t - dt < x; } /* the moment x passes */
static bool every(int k, float secs, float dt) { if ((show.next[k] -= dt) > 0) return false; show.next[k] = secs; return true; }
static float ease(float a, float b, float x) { x = fmaxf(0, fminf(1, (x - a) / (b - a))); return x * x * (3 - 2 * x); }
static uint32_t pastel(float h, float sat) /* the colour wheel, 0..1 round it */
{
    h -= floorf(h);
    float c[3] = {fabsf(h * 6 - 3) - 1, 2 - fabsf(h * 6 - 2), 2 - fabsf(h * 6 - 4)};
    uint32_t v = 0;
    for (int i = 0; i < 3; i++) v = v << 8 | (uint32_t)(255 * (1 - sat + sat * fmaxf(0, fminf(1, c[i]))));
    return v;
}
/* where an eye's pupil is on the screen (face.c's projection) */
static void pupil_xy(const eye_t *e, float *X, float *Y)
{
    float cy = cosf(e->yaw), sy = sinf(e->yaw), cp = cosf(e->pitch), sp_ = sinf(e->pitch), cr = cosf(e->roll), sr = sinf(e->roll);
    float gx = sinf(e->look_x) * cosf(e->look_y) * e->w / 2, gy = sinf(e->look_y) * e->h / 2, gz = cosf(e->look_x) * cosf(e->look_y) * e->depth / 2;
    float lx = cy * gx - sy * sp_ * gy + sy * cp * gz, ly = cp * gy + sp_ * gz;
    *X = e->x + lx * cr - ly * sr, *Y = e->y + lx * sr + ly * cr;
}
static uint32_t mix(uint32_t a, uint32_t b, float k)
{
    k = fmaxf(0, fminf(1, k));
    uint32_t v = 0;
    for (int i = 16; i >= 0; i -= 8) v |= (uint32_t)(((a >> i) & 255) * (1 - k) + ((b >> i) & 255) * k) << i;
    return v;
}
static void tear_arc(int e, float v) /* a tear of laughter thrown off the eye's outer corner */
{
    float sd = e ? 1 : -1;
    sprite_t *p = sprite_go(SP_DROP, 1, eye_cx(e) + sd * 62, eye_cy(e) - 6, sd * v, -150, .85f, 0x9ED8FF);
    if (p) p->ay = 420;
}
static void show_face(int mood, float t, float dt, mood_def_t *f, scene_out_t *o)
{
    float cx = SCR / 2, ex0 = eye_cx(0), ex1 = eye_cx(1), ey0 = eye_cy(0), ey1 = eye_cy(1);
    sprite_t *p;
    switch (mood) {
    case PIP_STARSTRUCK: {
        static const uint32_t gold[3] = {0xFFB800, 0xFFD23F, 0xFFE88A};
        if (every(0, 0.42f, dt)) s.squash.v -= 2.2f;
        if (every(1, 0.2f, dt)) {
            float a = frand(0, 6.28f);
            int e = frand(0, 1) < .5f;
            sprite_go(SP_SPARK, (int)frand(0, 2.99f), eye_cx(e) + cosf(a) * 105, eye_cy(e) + sinf(a) * 105, 0, -30, 0.6f, gold[(int)frand(0, 2.99f)]);
        }
        break;
    }
    case PIP_SINGING: {
        static const uint32_t c[4] = {0xFFD66B, 0x8FE3FF, 0xFF9AD5, 0xB7F59A};
        float b = fmodf(t, 0.56f), lp = fmodf(t, 4.48f);
        f->head_roll = 0.2f * sinf(t * 2.8f), f->gx = 0.3f * sinf(t * 2.8f);
        o->pull[0][1] = -8 * fabsf(sinf(t * 5.6f)), o->pull[1][1] = -8 * fabsf(cosf(t * 5.6f));
        if (lp > 3.0f) f->s.smile = .25f, f->s.h = 128, f->s.lid = .2f; /* a long note: eyes open a little */
        if (b < dt) {
            sc.k++;
            s.squash.v -= 1.8f;
            p = sprite_go(sc.k & 1 ? SP_NOTE : SP_NOTE2, 2, cx + (sc.k & 1 ? -20 : 20), 360, sc.k & 1 ? -48 : 48, -85, 2.0f, c[sc.k & 3]);
            if (p) p->sway = 40, p->ph = sc.k;
        }
        break;
    }
    case PIP_KISS: {
        float u = fmodf(t, 2.8f);
        o->scale = 1 + 0.08f * ease(0.2f, 0.8f, u) - 0.08f * ease(1.3f, 1.8f, u), o->spread = -0.05f * ease(0.2f, 0.8f, u) + 0.05f * ease(1.3f, 1.7f, u);
        if (u > 0.65f && u < 1.25f) f->s.smile = .95f, f->s.h = 96, f->gy = -.3f; /* eyes shut on the kiss */
        if (hits(u, dt, 0.85f)) {
            s.squash.v += 4;
            p = sprite_go(SP_HEART, 3, cx, 345, 0, -75, 2.0f, 0xFF4479);
            if (p) p->sway = 26;
            for (int i = 0; i < 3; i++) {
                p = sprite_go(SP_HEART, i == 1 ? 1 : 0, cx + (i == 1 ? 62 : i ? -48 : -82), 362 + i * 8, i == 1 ? 52 : i ? -24 : -60, -frand(80, 120), 1.6f, i == 1 ? 0xFF8FB5 : 0xFF6F9F);
                if (p) p->sway = 20, p->ph = i;
            }
        }
        break;
    }
    case PIP_FLIRTY: {
        float u = fmodf(t, 2.0f);
        f->gx = u < 0.6f ? .7f : 0, f->head_roll = .22f - .1f * ease(0.6f, 0.9f, u);
        if (hits(u, dt, 0.9f)) s.wink_t = 0;
        if (u > .8f && u < 1.4f) f->s.smile = .05f, f->s.lid = .05f, f->s.tilt = 0;
        if (hits(u, dt, 0.98f)) {
            s.squash.v -= 2.5f;
            sprite_go(SP_STAR, 2, fminf(395, ex1 + 60), ey1 - 35, 45, -95, 1.0f, 0xFFD23F);
            for (int i = 0; i < 4; i++) sprite_go(SP_SPARK, i & 1, fminf(390, ex1 + 55), ey1 - 25, cosf(-.2f - i * .6f) * 130, sinf(-.2f - i * .6f) * 130, .6f, 0xFFF0B0);
        }
        break;
    }
    case PIP_RAINBOW: {
        f->tint = pastel(t * 0.22f, 0.42f);
        f->head_roll = 0.1f * sinf(t * 1.6f);
        if (every(0, 0.7f, dt)) s.squash.v -= 2.0f;
        for (int i = 0; i < 10; i++) {
            float a = t * 0.7f + i * 0.6283f, r = 211 + 4 * sinf(t * 3 + i);
            sprite_pin(i, SP_DOT, i & 1, cx + cosf(a) * r, cx + sinf(a) * r, pastel(i / 10.0f - t * 0.1f, 0.75f));
        }
        break;
    }
    case PIP_PEEKING: {
        float u = fmodf(t, 4.6f);
        show.out = true, show.gaze = true;
        o->stiff = 70, o->zeta = 0.9f;
        if (u < 0.7f) o->sink = 345, o->stiff = 200, show.gx = 0, show.gy = 0;
        else if (u < 2.3f) o->sink = 165, show.gx = u < 1.0f ? 0 : u < 1.6f ? -.8f : .8f, show.gy = -.55f, f->s.smile = 0;
        else if (u < 2.9f) o->sink = 345, o->stiff = 200, show.gx = 0, show.gy = 0;
        else {
            o->sink = 0, o->stiff = 120, o->zeta = .45f, show.gx = 0, show.gy = -.2f, f->s.smile = .5f, f->s.h = 136;
            if (u > 4.3f) o->sink = 345, o->stiff = 120;
        }
        if (hits(u, dt, 2.95f)) {
            s.squash.v -= 5;
            for (int i = 0; i < 8; i++) {
                float a = -0.35f - i * 0.35f;
                sprite_go(i & 1 ? SP_STAR : SP_SPARK, 1, cx, 320, cosf(a) * 230, sinf(a) * 260, 0.9f, i & 1 ? 0xFFD23F : 0x8FE3FF);
            }
        }
        break;
    }
    case PIP_ZEN: {
        static const uint32_t c[3] = {0xC9B6FF, 0x9FE8FF, 0xFFD9F0};
        o->sink = -6 + 11 * sinf(t * 1.25f), o->scale = 1 + 0.025f * sinf(t * 1.25f);
        for (int i = 0; i < 3; i++) {
            float a = t * 0.9f + i * 2.0944f;
            sprite_pin(i, SP_DOT, sinf(a) > 0 ? 1 : 0, cx + cosf(a) * 195, cx + sinf(a) * 88 + 14 * cosf(a), c[i]);
        }
        break;
    }
    case PIP_SWEETDREAM: {
        float u = fmodf(t, 3.2f);
        o->sink = 22 + 9 * sinf(u / 3.2f * 6.283f - 1.2f), o->stiff = 40;
        if (u < dt && (p = sprite_go(SP_ZED, 1, 285, 150, 18, -44, 2.4f, 0xBFD4FF))) p->sway = 22;
        if (hits(u, dt, 1.1f) && (p = sprite_go(SP_ZED, 2, 300, 150, 20, -44, 2.4f, 0x9FBEFF))) p->sway = 22;
        if (hits(u, dt, 2.2f) && (p = sprite_go(SP_ZED, 0, 280, 155, 18, -44, 2.2f, 0xDDE8FF))) p->sway = 22;
        float k = u < 2.6f ? u / 2.6f : 0;
        int z = k <= 0.05f ? -1 : k < 0.3f ? 0 : k < 0.55f ? 1 : k < 0.8f ? 2 : 3;
        if (z >= 0) sprite_pin(0, SP_RING, z, cx - 4, 330 + sprite_px[z] * .4f, 0xA8D8FF);
        if (hits(u, dt, 2.6f)) {
            s.squash.v -= 1.5f;
            for (int i = 0; i < 6; i++) sprite_go(SP_DOT, 0, cx, 350, cosf(i * 1.047f) * 90, sinf(i * 1.047f) * 90, 0.35f, 0xA8D8FF);
        }
        break;
    }
    case PIP_MELTING: {
        float u = fmodf(t, 4.4f), k = ease(0, 2.4f, u) * (1 - ease(3.5f, 3.9f, u));
        f->s.taper = -0.08f - 0.34f * k, f->s.w = 116 + 12 * k, f->s.h = 130 - 26 * k, f->s.lid = .05f + .06f * k, f->s.smile = .15f + .5f * k, f->blush = .3f + .45f * k;
        f->s.roll = 0, f->head_roll = .1f * k * sinf(t * 2.2f), f->gy = .05f - .4f * k;
        o->sink = 50 * k, o->stiff = 90, o->spread = -.05f * k;
        if (u > 0.8f && u < 3.3f && every(0, 0.5f, dt)) {
            sc.k ^= 1;
            p = sprite_go(SP_HEART, sc.k ? 1 : 0, eye_cx(sc.k) + frand(-40, 40), eye_cy(sc.k) - 70, 0, -55, 1.4f, sc.k ? 0xFF7FA8 : 0xFFA8C6);
            if (p) p->sway = 24, p->ph = u * 3;
        }
        if (u > 0.9f && u < 3.3f && every(1, 0.75f, dt)) {
            int e = frand(0, 1) < .5f;
            p = sprite_go(SP_DOT, 0, eye_cx(e) + frand(-30, 30), eye_cy(e) + s.face.eye[e].h * .3f, 0, 20, 1.0f, 0xFFFFFF);
            if (p) p->ay = 160;
        }
        if (hits(u, dt, 3.6f)) s.squash.v -= 5;
        break;
    }
    case PIP_APPLAUSE: {
        static const uint32_t c[4] = {0xFFD23F, 0xFFFFFF, 0xFF9AD5, 0x8FE3FF};
        float u = fmodf(t, 3.2f), b = fmodf(u, 0.4f);
        bool on = u < 2.4f;
        o->spread = on ? (b < 0.17f ? -0.4f : 0.1f) : 0, o->stiff = 700, o->zeta = .6f;
        f->head_roll = on ? .06f * sinf(t * 4) : .14f;
        if (on && hits(b, dt, 0.11f)) {
            s.squash.v += 1.2f;
            sc.k++;
            for (int i = 0; i < 4; i++) {
                float a = -1.57f + (i - 1.5f) * 0.55f;
                sprite_go(i & 1 ? SP_SPARK : SP_STAR, 1 + (i & 1), cx, 165, cosf(a) * 190, sinf(a) * 170, 0.5f, c[(i + sc.k) & 3]);
            }
        }
        if (hits(u, dt, 2.45f) || hits(u, dt, 2.8f)) s.squash.v -= 4;
        break;
    }
    case PIP_COOL: {
        float u = fmodf(t, 2.6f);
        f->head_roll = .03f * sinf(t * 4.833f), f->gx = 0;
        show.gaze = true, show.gx = .1f * sinf(t * 2.417f), show.gy = 0;
        o->sink = 6 * sinf(t * 4.833f);
        sprite_bar(0, (ex0 + ex1) / 2, (ey0 + ey1) / 2 - 22, 78, 12, 0xFFC53D);
        sprite_bar(2, ex0 - s.face.eye[0].w * .5f - 8, ey0 - 14, 40, 12, 0xFFC53D);
        sprite_bar(3, ex1 + s.face.eye[1].w * .5f + 8, ey1 - 14, 40, 12, 0xFFC53D);
        if (u > 0.5f && u < 1.1f) {
            float v = (u - .5f) / .6f;
            sprite_pin(1, SP_SPARK, v < .2f ? 0 : v < .4f ? 1 : v < .65f ? 3 : v < .82f ? 1 : 0, ex1 + 62, ey1 - 30, 0xFFFFFF);
        }
        break;
    }
    case PIP_PUPPY: {
        float u = fmodf(t, 3.0f), k = .5f + .5f * sinf(t * 2.2f);
        f->head_roll = u < 1.5f ? .2f : -.2f;
        f->s.pw = .42f + .03f * k, f->s.ph = .46f + .03f * k;
        if (hits(u, dt, 0.4f) || hits(u, dt, 0.65f) || hits(u, dt, 2.0f)) s.squash.v -= 1.6f;
        break;
    }
    case PIP_BUTTERFLY: {
        float u = fmodf(t, 6.0f), bx, by;
        bool land = u > 2.6f && u < 4.6f;
        if (u < 2.6f) {
            float k = u / 2.6f;
            bx = -20 + 253 * ease(0, 1, k) + 60 * sinf(k * 8.5f) * (1 - k), by = 110 + 60 * sinf(k * 10) * (1 - k) + 18 * k;
        } else if (land) bx = cx, by = 128 + 2 * sinf(u * 2);
        else {
            float k = (u - 4.6f) / 1.4f;
            bx = cx + 270 * k * k + 30 * sinf(k * 9), by = 128 - 70 * k + 30 * sinf(k * 11);
        }
        bool shut = land ? fmodf(u, 0.9f) < 0.35f : fmodf(u, 0.18f) < 0.09f;
        sprite_pin(0, shut ? SP_BFLY1 : SP_BFLY0, 3, bx, by, 0xFFA63A);
        show.gaze = true, show.gx = (bx - cx) / 190, show.gy = (by - cx) / 190;
        o->sink = 34, f->cross = land ? .55f : 0;
        if (land) f->s.smile = .22f, f->s.lid = .05f, f->blink = 1.5f, show.gx = 0, show.gy = -.75f;
        if (hits(u, dt, 2.65f)) s.squash.v += 3;
        if (hits(u, dt, 4.6f)) s.squash.v -= 4;
        break;
    }
    case PIP_BUBBLES: {
        static const uint8_t z[4] = {2, 1, 3, 2};
        static const int16_t bx[4] = {150, 320, 233, 290};
        static const uint32_t c[4] = {0x9FE0FF, 0xFFC0EA, 0xC8F7FF, 0xD8C8FF};
        if (every(0, 0.95f, dt)) {
            sc.k = (sc.k + 1) % 4;
            p = sprite_go(SP_RING, z[sc.k], bx[sc.k], 470, 0, -82, 4.75f, c[sc.k]);
            if (p) p->sway = 45, p->ph = sc.k * 2;
        }
        sprite_t *tg = sprite_oldest(SP_RING);
        show.gaze = true, show.gx = show.gy = 0;
        if (tg) show.gx = (tg->x - cx) / 170, show.gy = (tg->y - cx) / 190;
        if (tg && tg->y < 105) { /* pop */
            for (int i = 0; i < 6; i++) sprite_go(SP_DOT, 0, tg->x + cosf(i * 1.047f) * 16, tg->y + sinf(i * 1.047f) * 16, cosf(i * 1.047f) * 110, sinf(i * 1.047f) * 110, 0.35f, tg->color);
            tg->life = 0, s.squash.v -= 3, show.next[2] = 0.5f, blink();
        }
        if ((show.next[2] -= dt) > 0) f->s.smile = .45f, f->s.h = 144;
        break;
    }
    case PIP_ROBOT: {
        static const float yx[8] = {0, -.8f, -.8f, 0, .8f, .8f, 0, 0}, yy[8] = {0, 0, -.5f, -.5f, 0, .4f, 0, 0}, rl[8] = {0, 0, .14f, .14f, 0, -.14f, -.14f, 0};
        float u = fmodf(t, 4.0f);
        int st = (int)(u / 0.5f);
        show.gaze = true, show.gx = yx[st], show.gy = yy[st], f->head_roll = rl[st];
        o->sink = 26;
        if (st >= 6) f->s.lid = fmodf(u, .25f) < .12f ? .55f : .1f; /* a glitchy double blink */
        float hr = s.head_roll.x, ax = cx + s.head_yaw.x * 60 + sinf(hr) * 120, top = fminf(ey0, ey1) - 112;
        sprite_bar(0, ax, top + 2, 7, 34, 0x8FA3A8);
        sprite_pin(1, SP_DOT, 1, ax, top - 20, fmodf(t, 0.5f) < 0.25f ? 0xFF4D4D : 0x6A1F1F);
        break;
    }
    case PIP_KITTY: {
        float u = fmodf(t, 3.6f), shut = ease(0.8f, 1.4f, u) * (1 - ease(2.0f, 2.7f, u)); /* a slow blink: a cat's "I love you" */
        f->s.lid = .14f + .64f * shut, f->s.smile = .12f * (1 - shut), f->s.roll = .12f - .2f * shut, f->s.tilt = .14f * (1 - shut), f->s.taper = .08f * (1 - shut);
        f->head_roll = .07f * sinf(t * 1.745f);
        o->sink = 46;
        o->pull[0][1] = 6 * sinf(t * 3.49f), o->pull[1][1] = -6 * sinf(t * 3.49f);
        for (int e = 0; e < 2; e++) {
            float sd = e ? 1 : -1, hr = s.head_roll.x, dx = sd * 108, dy = -138;
            float X = cx + s.head_yaw.x * 70 + dx * cosf(hr) - dy * sinf(hr), Y = cx + sc.sink.x + dx * sinf(hr) + dy * cosf(hr) + (fmodf(t + e * .9f, 2.4f) < .12f ? -5 : 0);
            sprite_pin(e * 2, e ? SP_EARR : SP_EARL, 4, X, Y, 0xF2E6A8);
            sprite_pin(e * 2 + 1, e ? SP_EARR : SP_EARL, 2, X - sd * 5, Y + 11, 0xFF9AB0);
        }
        break;
    }
    case PIP_SILLY: {
        float b = fmodf(t, 1.6f), h = fmodf(t, .8f);
        int k = b < .8f;
        f->size_asym = k ? .26f : -.26f, f->lid_asym = k ? -.1f : .2f;
        f->cross = k ? .75f : -.55f;
        f->head_roll = k ? .24f : -.24f;
        o->pull[0][1] = k ? 16 : -16, o->pull[1][1] = k ? -16 : 16;
        if (h < dt) s.squash.v -= 3;
        break;
    }
    case PIP_SNICKER: {
        float u = fmodf(t, 4.4f), k = ease(0, 2.4f, u);
        if (u < 2.5f) { /* holding it in */
            f->s.lid = .2f + .16f * k, f->s.smile = .3f + .2f * k, f->tremble = .5f + 2.6f * k, f->blush = .2f + .6f * k;
            o->scale = 1 + .06f * k;
            if (hits(u, dt, 1.0f) || hits(u, dt, 1.9f)) {
                s.squash.v -= 3;
                for (int i = 0; i < 4; i++) sprite_go(SP_DOT, 0, cx + (i - 1.5f) * 14, 350, (i - 1.5f) * 70, -40 - frand(0, 60), .45f, 0xD8D8E4);
            }
        } else if (u < 4.0f) { /* bursts out */
            f->s.smile = .97f, f->s.w = 130, f->s.h = 112, f->s.lid = 0, f->gx = 0, f->gy = -.4f, f->blush = .6f, f->tremble = 1.5f, f->head_roll = .1f;
            if (every(0, .17f, dt)) s.squash.v -= 2.4f, s.head_roll.v += frand(-1.4f, 1.4f);
            if (every(1, .4f, dt)) {
                sc.k ^= 1;
                float sd = sc.k ? 1 : -1;
                p = sprite_go(SP_DOT, 0, eye_cx(sc.k) + sd * 60, eye_cy(sc.k), sd * 60, -110, .9f, 0x9ED8FF);
                if (p) p->ay = 330;
            }
        }
        break;
    }
    case PIP_CAMPFIRE: {
        static const uint8_t fr[4] = {SP_FLAME0, SP_FLAME1, SP_FLAME2, SP_FLAME1};
        static const uint32_t em[3] = {0xFF8A2A, 0xFFC04A, 0xFF5A1F};
        float fl = .5f + .5f * sinf(t * 7.3f) * sinf(t * 4.1f + 1);
        f->tint = (uint32_t)0xFF << 16 | (uint32_t)(0xC4 + 26 * fl) << 8 | (uint32_t)(0x80 + 44 * fl);
        o->sink = -34;
        int k = (int)(t * 7) & 3, k2 = (int)(t * 9 + 1) & 3;
        sprite_bar(0, cx, 432, 78, 13, 0x7A4A2A);
        sprite_pin(1, fr[k2], 2, cx - 24, 404, 0xFF6A1F);
        sprite_pin(2, fr[(k2 + 2) & 3], 2, cx + 24, 404, 0xFF6A1F);
        sprite_pin(3, fr[k], 3, cx, 392, 0xFF8A2A);
        sprite_pin(4, fr[k2], 1, cx, 410, 0xFFE27A);
        if (every(0, .38f, dt)) {
            p = sprite_go(SP_DOT, 0, cx + frand(-30, 30), 370, frand(-20, 20), -frand(70, 120), 1.5f, em[(int)frand(0, 2.99f)]);
            if (p) p->sway = 40, p->ph = t;
        }
        break;
    }
    case PIP_ZAPPED: { /* the cable in: a bolt strikes, he is lit up, wide and shaking, sparks fly - then full of beans */
        static const uint32_t c[3] = {0xFFE14A, 0xFFFFFF, 0x7FE9FF};
        float u = fmodf(t, 3.6f);
        o->sink = 24;
        if (u < 0.35f) f->gy = -.7f, f->s.pw = .3f, f->s.ph = .32f; /* what is that? */
        else if (u < 1.45f) {
            f->tint = fmodf(u - .35f, 0.5f) < 0.25f ? 0xFFDC3A : 0x7FE9FF;
            f->s.w = 126, f->s.h = 158, f->s.pw = .13f, f->s.ph = .14f, f->s.smile = 0, f->tremble = 3.2f, f->gy = 0, o->spread = .04f;
            if (u < 0.95f && fmodf(u - .35f, 0.2f) < 0.14f) sprite_pin(0, SP_BOLT, 4, cx, 66, 0xFFD60A);
            if (every(0, .09f, dt)) {
                int e = frand(0, 1) < .5f;
                float a = frand(0, 6.28f);
                sprite_go(frand(0, 1) < .5f ? SP_SPARK : SP_BOLT, 1 + (int)frand(0, 1.5f), eye_cx(e) + cosf(a) * 95, eye_cy(e) + sinf(a) * 110, cosf(a) * 150, sinf(a) * 150, .4f, c[(int)frand(0, 2.99f)]);
            }
        } else { /* charged up: grinning, bouncing, a bolt winking at each side */
            f->tint = 0xFFF6D0, f->s.smile = .42f, f->s.glint = .4f, f->gy = -.25f, f->head_roll = .1f * sinf(t * 7);
            if (u < 3.0f && every(1, .32f, dt)) s.squash.v -= 3.4f;
            for (int i = 0; i < 2; i++)
                if (fmodf(u + i * .3f, .6f) < .34f) sprite_pin(1 + i, SP_BOLT, 3, cx + (i ? 1 : -1) * 92, i ? 86 : 78, 0xFFD60A);
        }
        if (hits(u, dt, 0.35f)) s.squash.v -= 6;
        break;
    }
    case PIP_SIPPING: { /* the cable in, thirsty: the plug comes up from below, beads of charge run up it, a gulp each, and he
                           turns from pale to a warm green glow; then "aah" */
        float u = fmodf(t, 5.0f), k = ease(0.9f, 3.6f, u), in = ease(0.0f, 0.55f, u) * (1 - ease(4.5f, 4.9f, u));
        float py = 520 - 112 * in; /* the plug's middle */
        uint32_t r = (uint32_t)(0xC6 - 0x1A * k), g = (uint32_t)(0xD2 + 0x2A * k), b = (uint32_t)(0xDC - 0x3C * k);
        f->tint = r << 16 | g << 8 | b;
        o->sink = -26;
        sprite_bar(0, cx, py + 62, 12, 80, 0x5A5F66);
        sprite_bar(1, cx, py + 8, 46, 34, 0xE8EAED);
        sprite_bar(2, cx, py - 17, 28, 18, 0xA9AFB8);
        if (u > 0.8f && u < 3.9f) {
            float b1 = fmodf(u - 0.8f, 0.62f) / 0.62f; /* a bead going up to him, and the gulp as it arrives */
            for (int i = 0; i < 3; i++) {
                float q = b1 + i / 3.0f;
                q -= floorf(q);
                sprite_pin(3 + i, SP_DOT, 0, cx + 6 * sinf(q * 6.28f), py - 34 - q * 92, i & 1 ? 0xB6FF6A : 0x5CE65C);
            }
            if (fmodf(u - 0.8f, 0.62f) < dt) s.squash.v += 2.6f;
            f->s.lid = .34f - .1f * k, f->s.smile = .2f + .25f * k, f->gy = .45f, f->blush = .25f * k;
        } else if (u >= 3.9f) f->s.smile = .9f, f->s.lid = 0, f->s.h = 112, f->gy = -.25f, f->blush = .4f, f->head_roll = .12f; /* aah */
        else f->s.lid = .4f, f->gy = .6f;
        if (hits(u, dt, 0.55f)) s.squash.v -= 3;
        if (hits(u, dt, 3.95f)) {
            s.squash.v -= 3;
            p = sprite_go(SP_BOLT, 3, cx, 112, 0, -34, 1.1f, 0x7CF06B);
            (void)p;
        }
        break;
    }
    case PIP_STARGAZING: {
        static const int16_t st[7][2] = {{92, 108}, {150, 62}, {215, 92}, {262, 44}, {318, 88}, {376, 120}, {196, 34}};
        static const uint32_t c[3] = {0xFFFFFF, 0xBFD8FF, 0xFFF0C0};
        float u = fmodf(t, 5.2f);
        for (int i = 0; i < 7; i++) {
            float tw = sinf(t * (1.3f + .31f * i) + i * 1.7f);
            if (tw > -.55f) sprite_pin(i, SP_SPARK, tw > .35f ? 1 : 0, st[i][0], st[i][1], c[i % 3]);
        }
        o->sink = 50, show.gaze = true, show.gx = .15f * sinf(t * .7f), show.gy = -.8f;
        if (u > 1.6f && u < 2.5f) { /* a shooting star: the eyes after it */
            float k = (u - 1.6f) / .9f, X = 60 + 350 * k, Y = 60 + 90 * k;
            sprite_pin(7, SP_STAR, 1, X, Y, 0xFFF3C0);
            if (every(0, .07f, dt)) sprite_go(SP_DOT, 0, X - 10, Y - 3, 0, 0, .4f, 0xFFE9A8);
            show.gx = (X - cx) / 160, show.gy = -.8f;
        } else if (u > 2.7f && u < 4.2f) f->s.smile = .92f, f->s.h = 112, f->s.glint = 0, f->head_roll = .12f, show.gy = -.3f; /* a wish */
        if (hits(u, dt, 4.25f)) s.squash.v -= 3;
        break;
    }
    case PIP_XD: { /* helpless with laughter: the eyes squeezed to thin crescents, shaking, tears flying; a snort pops them
                      open round for a moment - a breath - and off again */
        float u = fmodf(t, 3.2f);
        if (u < 2.3f) { /* ha-ha-ha: a bounce and a toss of the head on every "ha" */
            o->spread = -.06f, o->stiff = 400, o->scale = .9f;
            o->sink = 24 - 16 * fabsf(sinf(u * 11)), f->head_roll = .16f * sinf(u * 5.5f);
            f->s.h = 96 + 12 * fabsf(sinf(u * 11)), f->lid_asym = .12f * sinf(u * 5.5f);
            if (every(0, .2f, dt)) s.head_roll.v += frand(-.5f, .5f);
            if (every(1, .12f, dt)) /* laughing till he cries: tears spill from the corners and run down */
                for (int e = 0; e < 2; e++) {
                    float sd = e ? 1 : -1;
                    if ((p = sprite_go(SP_DROP, (sc.k ^= 1) & 1, eye_cx(e) + sd * 38, eye_cy(e) + 18, sd * frand(20, 60), -frand(60, 120), .8f, 0x9ED8FF))) p->ay = 520; /* 12 at most in the air */
                }
        } else if (u < 2.6f) { /* a snort: wide open, startled at himself */
            f->s.w = 114, f->s.h = 134, f->s.smile = 0, f->s.taper = -.04f, f->s.pw = .26f, f->s.ph = .28f, f->gy = -.2f, f->tremble = 0;
            o->stiff = 600;
            if (hits(u, dt, 2.3f)) s.squash.v += 5, s.head_pitch.v -= 3;
        } else { /* a breath, softer, and he is off again */
            f->s.w = 124, f->s.h = 104, f->s.smile = .9f, f->gy = -.5f, f->tremble = .5f;
            if (hits(u, dt, 2.95f)) s.squash.v -= 4;
        }
        break;
    }
    case PIP_ROFL: { /* rolling on the floor: the head over to one side, to the other, shaking */
        float u = fmodf(t, 2.0f);
        f->head_roll = .95f * sinf(t * 3.1416f), o->sink = 34 + 14 * cosf(t * 6.283f), o->scale = .84f;
        f->gx = .35f * sinf(t * 3.1416f);
        if (every(0, .16f, dt)) s.squash.v -= 2.2f;
        if (every(1, .33f, dt)) tear_arc(sinf(t * 3.1416f) > 0, 150);
        if (hits(u, dt, .5f) || hits(u, dt, 1.5f)) /* thump: dust at the side he rolled onto */
            for (int i = 0; i < 4; i++) sprite_go(SP_DOT, 0, cx + (u < 1 ? 130 : -130), 398, (u < 1 ? 1 : -1) * (30 + 40 * i), -90 + 25 * i, .4f, 0xC9C4BC);
        break;
    }
    case PIP_BOUNCY: { /* bounces like a ball; every fourth hop a somersault */
        float h = fmodf(t, .6f) / .6f, u = fmodf(t, 2.4f);
        o->sink = 62 - 120 * 4 * h * (1 - h), o->stiff = 1500, o->scale = .86f;
        f->head_roll = .12f * sinf(t * 5.236f);
        sc.spin = u > 1.8f ? 6.2832f * ease(1.88f, 2.32f, u) : 0;
        f->s.smile = u > 1.8f ? .6f : .45f;
        if (fmodf(t, .6f) < dt) {
            s.squash.v += 4.5f;
            for (int i = 0; i < 4; i++) {
                float sd = i & 1 ? 1 : -1;
                sprite_go(SP_DOT, i > 1 ? 0 : 1, cx + sd * 70, 414, sd * frand(60, 130), -frand(20, 70), .4f, 0xFFFFFF - 0x202020 * (i & 1));
            }
            if (u < dt) for (int i = 0; i < 5; i++) sprite_go(SP_STAR, i & 1, cx, 330, cosf(-.4f - i * .58f) * 200, sinf(-.4f - i * .58f) * 240, .8f, i & 1 ? 0xFFD23F : 0xFF9AD5);
        }
        break;
    }
    case PIP_BLOOMING: { /* flowers open one after another above and below him; then their petals fly */
        static const float an[6] = {-2.27f, -1.5708f, -.87f, .87f, 1.5708f, 2.27f};
        static const uint32_t c[6] = {0xFF8FB5, 0xFFD23F, 0x8FD8FF, 0xC9A6FF, 0xFF9A6B, 0xA8F0C0};
        float u = fmodf(t, 5.0f);
        o->scale = .8f, o->sink = u > 2.9f && u < 4.5f ? 34 : 8, f->head_roll = .1f * sinf(t * 2.5f);
        for (int i = 0; i < 6; i++) {
            float a = u - .25f - .42f * (i * 5 % 6), out = 4.45f - u;
            if (a < 0 || out < 0) continue;
            int z = a < .08f ? 1 : a < .16f ? 2 : 3;
            if (out < .12f) z = 1;
            else if (out < .24f) z = 2;
            float x = cx + 194 * cosf(an[i]), y = cx + 194 * sinf(an[i]) + 2 * sinf(t * 3 + i);
            sprite_pin(i * 2, SP_FLOWER, z, x, y, c[i]);
            if (z > 1) sprite_pin(i * 2 + 1, SP_DOT, 0, x, y, i == 1 ? 0xFF8A2A : 0xFFE27A);
            if (a < dt) s.squash.v -= 2.2f;
        }
        if (hits(u, dt, 4.45f))
            for (int i = 0; i < 6; i++)
                if ((p = sprite_go(SP_FLOWER, 1, cx + 180 * cosf(an[i]), cx + 180 * sinf(an[i]), 50 * cosf(an[i]), -60, 1.1f, c[i]))) p->sway = 60, p->ph = i;
        if (u > 3.0f && u < 4.4f) f->s.smile = .9f, f->s.h = 110, f->s.glint = 0, f->gy = -.3f;
        else {
            int k = (int)((u - .25f) / .42f);
            static const uint8_t at[6] = {0, 5, 4, 3, 2, 1}; /* which flower opens k-th */
            if (k >= 0 && k < 6) show.gaze = true, show.gx = .8f * cosf(an[at[k]]), show.gy = .8f * sinf(an[at[k]]);
        }
        break;
    }
    case PIP_HEARTEYES: { /* in love: a heart grows over him and beats - lub-dub - he melts a little at every beat; then it
                             bursts into little hearts, his eyes after them, blissful */
        static const uint32_t pinks[3] = {0xFF5C8A, 0xFF8FB5, 0xFFC0D4};
        float u = fmodf(t, 4.0f), hy = 104 + 6 * sinf(t * 2.1f);
        o->sink = 34, o->scale = .9f, f->head_roll = .08f * sinf(t * 1.6f);
        show.gaze = true, show.gx = .08f * sinf(t * 1.3f), show.gy = -.35f;
        if (u < .5f) { /* a spark that grows into a heart, rising */
            int z = u < .12f ? 0 : u < .25f ? 1 : u < .38f ? 3 : 4;
            sprite_pin(0, SP_HEART, z, cx, hy + 40 * (1 - u / .5f), 0xFF3F74);
        } else if (u < 2.9f) { /* lub-dub */
            float b = fmodf(u - .5f, .8f);
            bool big = b < .12f || (b > .2f && b < .32f);
            sprite_pin(0, big ? SP_HEART : SP_HEART2, SP_OWN, cx, hy, 0xFF3F74);
            sprite_pin(1, SP_DOT, 1, cx - (big ? 34 : 28), hy - (big ? 26 : 21), 0xFF9AB8); /* its shine */
            float melt = ease(0, .12f, b) * (1 - ease(.3f, .6f, b));
            f->s.smile = .15f + .6f * melt, f->s.h = 128 - 16 * melt, f->s.pw = .36f + .04f * melt, f->s.ph = .38f + .04f * melt;
            if (b < dt) {
                s.squash.v -= 2.2f;
                sc.k ^= 1; /* a little one off its side */
                if ((p = sprite_go(SP_HEART, 0, cx + (sc.k ? 60 : -60), hy, sc.k ? 70 : -70, -40, 1.2f, pinks[1 + sc.k]))) p->sway = 25, p->ph = t;
            }
        } else { /* pop: little hearts everywhere, and bliss */
            if (hits(u, dt, 2.9f)) {
                s.squash.v += 5;
                for (int i = 0; i < 9; i++) {
                    float a = -1.5708f + (i - 4) * .38f, v = frand(150, 220);
                    if ((p = sprite_go(SP_HEART, i % 3 == 1 ? 1 : 0, cx + 30 * cosf(a), hy + 30 * sinf(a), v * cosf(a), v * sinf(a) + 40, 1.1f, pinks[i % 3])))
                        p->ay = 160;
                }
            }
            f->s.smile = .85f, f->s.h = 110, f->s.lid = 0, f->gy = -.3f;
            show.gy = -.35f, show.gx = .3f * sinf(t * 2);
        }
        break;
    }
    case PIP_FIREWORKS: { /* a rocket, the eyes after it, a burst - three times */
        static const int16_t tx[3] = {140, 322, 233}, ty[3] = {92, 84, 66};
        static const uint32_t c[3][2] = {{0xFFD23F, 0xFF8A2A}, {0x8FE3FF, 0xC9A6FF}, {0xFF6F9F, 0xFFE27A}};
        float u = fmodf(t, 4.5f);
        int k = (int)(u / 1.5f);
        float v = fmodf(u, 1.5f);
        o->sink = 62, o->scale = .88f, show.gaze = true;
        show.gx = (tx[k] - cx) / 150.0f, show.gy = -.85f;
        if (v < .55f) {
            float q = v / .55f, y = 440 + (ty[k] - 440) * (1 - (1 - q) * (1 - q)), x = cx + (tx[k] - cx) * q;
            sprite_pin(0, SP_DOT, 0, x, y, 0xFFF3C0);
            show.gy = fminf(.6f, (y - cx) / 180), show.gx = (x - cx) / 150;
            if (every(0, .06f, dt)) sprite_go(SP_SPARK, 0, x, y + 8, frand(-12, 12), 40, .35f, c[k][1]);
        } else if (v < .95f) f->s.pw = .3f, f->s.ph = .32f, f->s.h = 140, f->s.smile = .1f, f->tint = mix(0xFFFFFF, c[k][0], .38f);
        else f->s.smile = .5f;
        if (hits(v, dt, .55f)) {
            s.squash.v -= 4;
            for (int i = 0; i < 10; i++) {
                float a = i * .6283f, r = i & 1 ? 150 : 105;
                p = sprite_go(i & 1 ? SP_STAR : SP_SPARK, i & 1 ? 0 : 1, tx[k], ty[k], cosf(a) * r, sinf(a) * r - 20, .85f, c[k][i & 1]);
                if (p) p->ay = 90;
            }
        }
        break;
    }
    case PIP_MEDAL: { /* a gold medal on him: a look down at it, chin up, it gleams */
        float u = fmodf(t, 4.0f), sw = 9 * sinf(t * 3.14f), y = 398;
        bool down = u > .4f && u < 1.3f;
        o->sink = -42, o->scale = .88f;
        sprite_pin(0, SP_RIBBON, 3, cx + sw * .4f, y - 62, 0x3D7BFF);
        sprite_pin(1, SP_DOT, 4, cx + sw, y, 0xE0A100);
        sprite_pin(2, SP_DOT, 3, cx + sw, y, 0xFFD23F);
        sprite_pin(3, SP_STAR, 2, cx + sw, y - 1, 0xFFF3C0);
        if (u > 2.0f && u < 2.7f) {
            float v = (u - 2.0f) / .7f;
            sprite_pin(4, SP_SPARK, v < .2f ? 0 : v < .4f ? 1 : v < .65f ? 3 : v < .82f ? 1 : 0, cx + sw + 30, y - 28, 0xFFFFFF);
        }
        if (down) f->gy = .85f, f->gx = 0, f->s.lid = .2f, f->s.pw = .36f, f->s.ph = .4f;
        else if (u > 1.9f) f->gy = -.75f, f->s.smile = .85f, f->s.lid = 0, f->s.h = 110, f->s.glint = 0, f->head_roll = -.08f;
        if (hits(u, dt, 1.9f)) s.squash.v -= 4;
        if (hits(u, dt, 2.6f) || hits(u, dt, 3.0f)) s.squash.v -= 2;
        break;
    }
    case PIP_DISCO: { /* a mirror ball, spots of light sweeping the floor, and he is dancing */
        float b = fmodf(t, .5f), be = t / .5f;
        int k = (int)be & 1;
        o->sink = 46, o->scale = .84f;
        f->head_roll = k ? .2f : -.2f, f->gx = k ? .4f : -.4f;
        o->pull[0][1] = k ? -20 : 8, o->pull[1][1] = k ? 8 : -20;
        if (b < dt) s.squash.v += 2.6f;
        f->tint = pastel(floorf(be) * .23f, .3f);
        sprite_bar(0, cx, 8, 4, 30, 0x8A8F9A);
        sprite_pin(1, SP_DISCO, 4, cx, 64, (int)(t * 4) & 1 ? 0xDCE4F4 : 0xC4D0EA);
        float g = fmodf(t, .7f) / .7f;
        sprite_pin(2, SP_SPARK, g < .3f ? 1 : g < .6f ? 2 : 0, cx + (((int)(t / .7f) & 1) ? 20 : -22), 50 + 14 * g, 0xFFFFFF);
        for (int i = 0; i < 8; i++) { /* the spots go round on the floor and the walls */
            float a = t * .9f + i * .7854f, x = cx + 205 * cosf(a), y = 250 + 170 * sinf(a);
            if (y > 150 || fabsf(x - cx) > 120) sprite_pin(3 + i, SP_DOT, i & 1, x, y, pastel(i * .125f + floorf(be) * .1f, .7f));
        }
        break;
    }
    case PIP_POPEYED: { /* looks, looks again - and his eyes pop out of his head */
        float u = fmodf(t, 3.8f);
        if (u < .5f) f->gx = -.5f, show.gaze = false;
        else if (u < .9f) f->s.lid = .25f, f->gx = .2f; /* hm? */
        else if (u < 2.6f) {
            float w = u - .9f;
            f->s.w = 112, f->s.h = 176, f->s.pw = .13f, f->s.ph = .1f, f->s.taper = -.02f, f->tremble = 1.2f;
            o->pull[0][0] = -8, o->pull[1][0] = 8;
            o->scale = 1.02f + .1f * expf(-w * 2.2f) * cosf(w * 16), o->stiff = 600, o->zeta = .5f;
            if (w > .9f) f->s.pw = .2f + .16f * ease(.9f, 1.5f, w), f->s.ph = .16f + .16f * ease(.9f, 1.5f, w);
        } else f->wag = u < 3.2f ? .28f : 0, f->s.smile = .2f, f->s.h = 128;
        if (hits(u, dt, .9f)) {
            s.squash.v -= 6;
            for (int i = 0; i < 8; i++) {
                float a = -2.75f + i * .334f;
                sprite_go(SP_SPARK, 1 + (i & 1), cx + cosf(a) * 70, 130 + sinf(a) * 20, cosf(a) * 230, sinf(a) * 170, .5f, i & 1 ? 0xFFFFFF : 0xFFE27A);
            }
        }
        if (hits(u, dt, 2.6f)) s.squash.v += 4, blink();
        break;
    }
    case PIP_LIGHTBULB: { /* hm... hm... three dots - and a bulb lights up over him */
        float u = fmodf(t, 4.6f);
        o->sink = 56, o->scale = .88f;
        if (u < 2.0f) {
            f->s.lid = .22f, f->lid_asym = .16f, f->s.tilt = .08f, f->s.pw = .32f;
            show.gaze = true, show.gx = u < 1.0f ? -.65f : .65f, show.gy = -.7f;
            f->head_roll = u < 1.0f ? -.12f : .12f;
            for (int i = 0; i < 3; i++)
                if (u > .25f + i * .5f) sprite_pin(i, SP_DOT, 0, cx + (i - 1) * 34, 72 - 5 * fabsf(sinf(u * 5 - i)), 0xC9CCD6);
        } else if (u < 4.2f) {
            float w = u - 2.0f;
            bool on = w > .3f || fmodf(w, .14f) < .07f; /* a flicker first */
            float pop = w < .12f ? 10 * (1 - w / .12f) : 0;
            sprite_pin(0, SP_BULB, SP_OWN, cx, 76 + pop, on ? 0xFFDD33 : 0x6A6350);
            if (on) sprite_pin(1, SP_SPARK, 2, cx, 66 + pop, 0xFFF8D8);
            sprite_pin(2, SP_SCREW, SP_OWN, cx, 129 + pop, 0x9AA3AE);
            if (w > .35f) {
                float r = 70 + 5 * sinf(w * 9);
                for (int i = 0; i < 5; i++) sprite_pin(3 + i, SP_DOT, 0, cx + r * cosf(-3.1416f + i * .7854f), 76 + r * .82f * sinf(-3.1416f + i * .7854f), 0xFFE98A);
            }
            f->s.h = 146, f->s.pw = .28f, f->s.ph = .3f, show.gaze = true, show.gx = 0, show.gy = -.75f, f->s.smile = w > .9f ? .4f : .05f;
            if (w > .4f) f->tint = 0xFFF6CF;
            if (hits(u, dt, 2.95f) || hits(u, dt, 3.35f) || hits(u, dt, 3.75f)) s.squash.v -= 3.4f;
        } else f->s.smile = .3f;
        if (hits(u, dt, 2.0f)) s.squash.v -= 5;
        break;
    }
    case PIP_PAWS: { /* hides his eyes in his paws, peeks round one, hides, peeks again - and comes out, blushing */
        float u = fmodf(t, 5.2f);
        float in = ease(.1f, .5f, u) * (1 - ease(4.0f, 4.4f, u)); /* paws up */
        float pk = ease(1.2f, 1.45f, u) * (1 - ease(2.05f, 2.2f, u)), pk2 = ease(2.75f, 3.0f, u) * (1 - ease(3.6f, 3.75f, u));
        o->scale = .64f, o->spread = -.1f, o->sink = -4;
        f->blush = .4f, f->s.smile = u > 4.0f ? .45f : .15f, f->gy = u > 4.0f ? .3f : 0, f->gx = u > 4.0f ? -.4f : 0;
        f->head_roll = u > 4.0f ? -.14f : .06f * sinf(t * 3);
        for (int e = 0; e < 2; e++) {
            float q = e ? pk : pk2, sd = e ? 1 : -1;
            float x = cx + sd * (106 + 12 * q), y = 226 + (1 - in) * 270 + 76 * q + 3 * sinf(t * 5 + e);
            sprite_pin(e * 2, SP_PAW, SP_OWN, x, y, 0xFFE0B8);
            sprite_pin(e * 2 + 1, SP_PADS, SP_OWN, x, y + 2, 0xFF8FA8);
        }
        if (hits(u, dt, 2.1f) || hits(u, dt, 3.65f)) s.squash.v += 3;
        if (hits(u, dt, 4.3f)) s.squash.v -= 2, blink();
        break;
    }
    case PIP_HELLO: { /* a paw waves, the head to one side; a hop and a wink */
        float u = fmodf(t, 3.0f), w = ease(0.1f, .4f, u) * (1 - ease(2.4f, 2.75f, u));
        o->sink = -52, o->scale = .82f;
        f->head_roll = .16f * w;
        float a = u < 2.3f ? sinf(t * 10.5f) : 0;
        float x = 322 + 24 * a, y = 376 + 130 * (1 - w) + 7 * fabsf(a);
        sprite_pin(0, SP_PAW, SP_OWN, x, y, 0xFFE0B8);
        sprite_pin(1, SP_PADS, SP_OWN, x, y + 2, 0xFF8FA8);
        for (int i = 0; i < 2; i++) /* little lines of the waving */
            if (w > .9f && u < 2.3f && every(i, .6f, dt)) sprite_go(SP_DOT, 0, 322 + (i ? 88 : -100), 336, (i ? 30 : -50), -30, .35f, 0xFFD23F);
        f->s.smile = u < .4f ? .2f : .6f;
        if (hits(u, dt, .35f)) s.squash.v -= 4;
        if (hits(u, dt, 2.45f)) s.wink_t = 0;
        if (u > 2.4f) f->s.smile = .05f, f->s.lid = 0, f->s.tilt = 0;
        break;
    }
    case PIP_KING: { /* a crown: haughty looks; it slips down over his eyes, he tosses it back */
        float u = fmodf(t, 5.0f), y = 66;
        o->sink = 58, o->scale = .88f;
        if (u > 2.6f && u < 3.5f) y += 74 * (u < 2.85f ? (u - 2.6f) / .25f * (u - 2.6f) / .25f : 1);
        else if (u >= 3.5f && u < 4.1f) { float v = (u - 3.5f) / .6f; y += 74 * (1 - ease(0, .4f, v)) - 60 * 4 * v * (1 - v) * (v > .1f); if (y < 34) y = 34; }
        float x = cx + s.head_yaw.x * (u < 2.6f ? 60 : 20);
        sprite_pin(0, SP_CROWN, SP_OWN, x, y, 0xFFC93A);
        sprite_bar(1, x, y + 34, 122, 12, 0xE09A00);
        sprite_pin(2, SP_DOT, 0, x, y + 12, 0xFF4D6A);
        sprite_pin(3, SP_DOT, 0, x - 40, y + 16, 0x4DA6FF);
        sprite_pin(4, SP_DOT, 0, x + 40, y + 16, 0x4DD98A);
        if (u > .8f && u < 1.4f) { float v = (u - .8f) / .6f; sprite_pin(5, SP_SPARK, v < .3f ? 1 : v < .65f ? 2 : 0, x + 62, y - 38, 0xFFFFFF); }
        if (u < 2.6f) show.gaze = true, show.gx = u < 1.3f ? -.6f : .6f, show.gy = -.25f, f->head_roll = u < 1.3f ? .08f : -.08f;
        else if (u < 3.5f) show.gaze = true, show.gx = 0, show.gy = -.2f, f->s.lid = .06f, f->s.smile = 0, f->s.h = 120, f->s.pw = .3f, f->s.ph = .32f, f->cross = .35f, f->gy = -.4f, f->tremble = .4f;
        else show.gaze = true, show.gx = 0, show.gy = -.1f, f->s.smile = .55f, f->s.lid = .05f, f->gy = -.4f;
        if (hits(u, dt, 2.85f)) s.squash.v += 5;
        if (hits(u, dt, 3.5f)) s.squash.v -= 6;
        if (hits(u, dt, 4.1f)) s.squash.v += 3;
        break;
    }
    case PIP_ANGEL: { /* a halo, the most innocent eyes there are; a glance aside, whistling: who, me? */
        float u = fmodf(t, 5.0f);
        o->sink = 34, o->scale = .9f;
        sprite_pin(0, SP_HALO, SP_OWN, cx + s.head_yaw.x * 40, 70 + 4 * sinf(t * 2.5f), 0xFFE27A);
        if (every(0, .45f, dt)) {
            float a = frand(0, 6.28f);
            sprite_go(SP_SPARK, (int)frand(0, 1.99f), cx + cosf(a) * 90, 68 + sinf(a) * 30, 0, -16, .7f, 0xFFF3C0);
        }
        if (u < 2.4f) {
            f->gy = -.35f, f->head_roll = .14f;
            if (hits(u, dt, .7f) || hits(u, dt, 1.0f) || hits(u, dt, 1.8f)) blink();
        } else if (u < 4.2f) { /* whistling */
            show.gaze = true, show.gx = u < 3.3f ? .8f : -.8f, show.gy = -.55f;
            f->head_roll = -.1f, f->s.lid = .1f, f->s.smile = .05f;
            if (every(1, .55f, dt)) {
                int k = sc.k++;
                if ((p = sprite_go(k & 1 ? SP_NOTE : SP_NOTE2, 2, cx, 365, (k + 1) & 1 ? 40 : -40, -50, 1.2f, 0xBFD8FF))) p->sway = 30;
            }
        } else f->s.smile = .6f, f->s.h = 120, f->gy = -.3f;
        if (hits(u, dt, 4.2f)) s.squash.v -= 3;
        break;
    }
    case PIP_IMP: { /* little horns, a grin: he is up to something - rubs along side to side and snickers */
        float u = fmodf(t, 3.4f);
        o->sink = 34, o->scale = .9f;
        float hx = s.head_yaw.x * 70, hr = s.head_roll.x;
        for (int e = 0; e < 2; e++) {
            float sd = e ? 1 : -1, dx = sd * 88, dy = -128, X = cx + hx + dx * cosf(hr) - dy * sinf(hr), Y = cx + sc.sink.x + dx * sinf(hr) + dy * cosf(hr);
            sprite_pin(e, e ? SP_HORNR : SP_HORNL, SP_OWN, X, Y, 0xE5383B);
        }
        if (u < 1.7f) f->gx = u < .85f ? -.55f : .55f, f->head_roll = u < .85f ? -.1f : .1f;
        else { /* he-he-he */
            f->s.smile = .6f, f->s.lid = .18f, f->gx = 0, f->gy = -.2f;
            f->lid_asym = -.16f;
            if (u < 2.9f && every(0, .18f, dt)) s.squash.v -= 2.2f;
        }
        for (int i = 0; i < 9; i++) { /* a tail, wagging */
            float q = i / 8.0f, wg = sinf(t * 5.5f) * 26 * q * q;
            float X = 286 + 92 * q + wg, Y = 446 - 90 * q * q - 8 * sinf(q * 3.14f);
            if (i < 8) sprite_pin(2 + i, SP_DOT, 0, X, Y, 0xC92A2D);
            else sprite_pin(10, SP_HEART, 1, X + 2, Y - 8, 0xE5383B);
        }
        break;
    }
    case PIP_JUGGLER: { /* three balls thrown from eye to eye; each catch squashes it, the look follows the highest */
        static const uint32_t c[3] = {0xFF5C5C, 0xFFD23F, 0x5CB8FF};
        o->sink = 66, o->scale = .8f;
        f->head_roll = .05f * sinf(t * 4.19f);
        show.gaze = true, show.gx = 0, show.gy = -.8f;
        float best = 999;
        for (int i = 0; i < 3; i++) {
            float q = fmodf(t / .8f + i * 2 / 3.0f, 2), v = fmodf(q, 1), x0 = q < 1 ? ex0 : ex1, x1 = q < 1 ? ex1 : ex0;
            float y0 = fminf(ey0, ey1) - s.face.eye[0].h * .5f - 14, x = x0 + (x1 - x0) * v, y = y0 - 112 * 4 * v * (1 - v);
            sprite_pin(i, SP_DOT, 2, x, y, c[i]);
            sprite_pin(3 + i, SP_DOT, 0, x - 7, y - 8, mix(c[i], 0xFFFFFF, .55f));
            if (y < best) best = y, show.gx = (x - cx) / 130;
            if (v < .18f) o->pull[q < 1 ? 0 : 1][1] = 16;
        }
        break;
    }
    case PIP_BALLOON: { /* a balloon on a string lifts him, swaying; pop! he drops, blinks - and laughs */
        float u = fmodf(t, 5.4f);
        float up = ease(.2f, 2.2f, u), sw = sinf(u * 2.4f);
        o->scale = .84f;
        if (u < 3.7f) {
            float x = cx + 26 * sw, y = 110 - 34 * up;
            o->sink = 84 - 34 * up, o->pull[0][0] = o->pull[1][0] = 14 * sw, f->head_roll = -.08f * cosf(u * 2.4f);
            float mx = (ex0 + ex1) / 2, sx = x + (mx - x) * .5f;
            sprite_bar(0, sx, y + 90, 3, 64, 0xA9AFB8);
            sprite_pin(1, SP_BALLOON, SP_OWN, x, y, 0xFF5470);
            sprite_pin(2, SP_DOT, 0, x - 18, y - 30, 0xFFC0CC);
            show.gaze = true, show.gx = .3f * sw, show.gy = -.85f;
        } else if (u < 4.3f) f->s.h = 150, f->s.w = 118, f->s.pw = .18f, f->s.ph = .2f, f->s.smile = 0, o->sink = 84, o->stiff = 500, f->tremble = .8f;
        else f->s.smile = .9f, f->s.h = 112, f->gy = -.3f, o->sink = 60, f->blush = .3f;
        if (hits(u, dt, 3.7f)) {
            s.squash.v -= 6;
            for (int i = 0; i < 9; i++) {
                float a = i * .698f;
                if ((p = sprite_go(i % 3 ? SP_SPARK : SP_DROP, i % 3 ? 1 : 0, cx + 10, 76, cosf(a) * 190, sinf(a) * 150 - 30, .6f, i & 1 ? 0xFF5470 : 0xFF9AAA))) p->ay = 260;
            }
        }
        if (u > 4.3f && u < 5.1f && every(0, .17f, dt)) s.squash.v -= 2;
        break;
    }
    case PIP_ICECREAM: { /* two scoops: a lick, a lick - brain freeze! - and bliss */
        float u = fmodf(t, 6.0f);
        bool l1 = u > .9f && u < 1.2f, l2 = u > 2.0f && u < 2.3f;
        o->sink = l1 || l2 ? -30 : -52, o->stiff = 320, o->scale = .84f;
        f->gy = .75f;
        float in = ease(0, .5f, u) * (1 - ease(5.5f, 5.95f, u)), y = 418 + 150 * (1 - in);
        sprite_pin(0, SP_CONE, SP_OWN, cx, y, 0xE0A55A);
        if (u < 2.15f) sprite_pin(1, SP_DOT, 3, cx, y - 54, 0xFF9AC0);
        else sprite_pin(1, SP_DOT, 2, cx, y - 48, 0xFF9AC0);
        if (u < 1.05f) sprite_pin(2, SP_DOT, 2, cx, y - 92, 0x9AE8C8), sprite_pin(3, SP_DOT, 0, cx + 6, y - 116, 0xFF4D5E);
        if (hits(u, dt, 1.05f) || hits(u, dt, 2.15f)) s.squash.v += 3.4f;
        if (l1 || l2) f->s.smile = .85f, f->s.h = 110;
        else if (u > 1.2f && u < 2.0f) f->s.smile = .55f;
        else if (u > 2.7f && u < 4.0f) { /* brain freeze */
            f->s.w = 126, f->s.h = 40, f->s.roll = .24f, f->s.smile = 0, f->s.lid = 0, f->tint = 0xA9DCFF, f->tremble = 2.6f, f->gy = 0;
            if (every(0, .2f, dt)) sprite_go(SP_SPARK, (int)frand(0, 1.99f), cx + frand(-120, 120), frand(50, 110), 0, 24, .6f, 0xCFEFFF);
        } else if (u > 4.0f) f->s.smile = .9f, f->s.h = 112, f->gy = -.2f, f->blush = .35f, f->head_roll = .12f;
        if (hits(u, dt, 2.7f)) s.squash.v += 4;
        if (hits(u, dt, 4.0f)) s.squash.v -= 4;
        break;
    }
    case PIP_UMBRELLA: { /* rain all round and he is dry under an umbrella, swaying, pleased */
        o->sink = 60, o->scale = .7f, o->spread = -.2f;
        f->head_roll = .06f * sinf(t * 1.8f), f->gx = .25f * sinf(t * .9f);
        float x = cx + 4 * sinf(t * 1.8f), y = 92;
        sprite_bar(0, cx, 246, 8, 200, 0xC9A27A);
        sprite_pin(1, SP_HOOK, 2, cx + 13, 352, 0xC9A27A);
        sprite_pin(2, SP_UMBRELLA, SP_OWN, x, y, 0xFF6B5E);
        if (every(0, .17f, dt)) { /* outside, and onto it */
            sc.k = (sc.k + 1) % 4;
            if (sc.k == 1) for (int i = 0; i < 2; i++) sprite_go(SP_DOT, 0, x + frand(-70, 70), 46, (i ? 60 : -60), -50, .28f, 0xBFE4FF);
            else if ((p = sprite_go(SP_DROP, 0, sc.k & 1 ? frand(384, 424) : frand(42, 84), 120, 0, 250, 1.15f, 0x7FC4FF))) p->ay = 140;
        }
        if (fmodf(t, 3.6f) > 1.8f && fmodf(t, 3.6f) < 2.8f) f->s.smile = .85f, f->s.h = 110, f->s.lid = 0;
        else f->gy = -.4f;
        break;
    }
    case PIP_STRONGMAN: { /* a barbell: up... it won't... up over his head! holds it shaking, drops it, proud */
        float u = fmodf(t, 5.0f), y = 400;
        o->sink = -18, o->scale = .86f;
        if (u < .6f) f->gy = .8f;
        else if (u < 1.9f) { /* straining */
            float v = (u - .6f) / 1.3f;
            y = 400 - 26 * v + frand(-2, 2);
            f->s.h = 84, f->s.lid = .2f, f->s.smile = .3f, f->s.tilt = .12f, f->tint = mix(0xFFFFFF, 0xFFA08A, v), f->tremble = 1 + 2 * v, f->gy = .2f, o->sink = -18 + 22 * v;
        } else if (u < 3.2f) { /* up! */
            float v = fminf(1, (u - 1.9f) / .18f);
            y = 374 - 318 * v + frand(-2, 2) * (v >= 1);
            o->sink = 26, f->s.h = 104, f->s.w = 122, f->s.lid = .12f, f->s.smile = .35f, f->tint = 0xFFC4B0, f->tremble = 1.8f, f->gy = -.7f;
        } else if (u < 3.4f) y = 56 + 344 * ((u - 3.2f) / .2f) * ((u - 3.2f) / .2f), o->sink = 10;
        else f->s.smile = .6f, f->s.lid = .05f, f->gy = -.6f, f->head_roll = -.1f, f->s.glint = .3f;
        for (int i = 0; i < 2; i++) {
            float sd = i ? 1 : -1;
            sprite_bar(1 + i, cx + sd * 78, y, 22, 74, 0x4D5BD6);
            sprite_bar(3 + i, cx + sd * 100, y, 16, 52, 0x7A86F0);
        }
        sprite_bar(0, cx, y, 240, 10, 0xB9BEC8);
        if (hits(u, dt, 3.4f)) {
            s.squash.v += 6;
            for (int i = 0; i < 6; i++) sprite_go(SP_DOT, i & 1, cx + (i - 2.5f) * 50, 428, (i - 2.5f) * 40, -50, .45f, 0xC9C4BC);
        }
        if (hits(u, dt, 1.9f)) s.squash.v -= 6;
        if (u > .9f && u < 1.9f && every(0, .45f, dt)) sc.k ^= 1, tear_arc(sc.k, 80);
        break;
    }
    case PIP_MAGICIAN: { /* looks into a top hat, abracadabra - puff! bunny ears come up out of it */
        float u = fmodf(t, 5.2f), up = ease(1.9f, 2.2f, u) * (1 - ease(4.2f, 4.5f, u));
        o->sink = -64, o->scale = .78f;
        float tw = u > 2.6f && fmodf(u, .5f) < .1f ? 5 : 0;
        if (up > .02f) {
            for (int i = 0; i < 2; i++) {
                float x = cx + (i ? 26 : -26), y = 398 - 74 * up + (i ? tw : 0);
                sprite_pin(i * 2, SP_BUNNY, SP_OWN, x, y, 0xF2F2F5);
                sprite_bar(i * 2 + 1, x, y + 4, 12, 58, 0xFFB0C4);
            }
        }
        sprite_pin(4, SP_TOPHAT, SP_OWN, cx, 408, 0x5A4BCF);
        sprite_bar(5, cx, 392, 124, 14, 0xFFD23F);
        if (u < 1.9f) {
            show.gaze = true, show.gx = .55f * sinf(u * 8), show.gy = .7f + .15f * cosf(u * 8); /* abracadabra */
            f->s.lid = .12f;
            if (every(0, .14f, dt)) { float a = u * 6; sprite_go(SP_SPARK, (int)frand(0, 1.99f), cx + cosf(a) * 92, 338 + sinf(a) * 26, 0, -24, .5f, pastel(u * .4f, .5f)); }
        } else if (u < 3.0f) show.gaze = true, show.gx = 0, show.gy = .6f, f->s.h = 138, f->s.pw = .26f, f->s.ph = .28f;
        else show.gaze = true, show.gx = 0, show.gy = .1f, f->s.smile = .6f, f->head_roll = .1f;
        if (hits(u, dt, 1.9f)) {
            s.squash.v -= 5;
            for (int i = 0; i < 9; i++) {
                float a = -.25f - i * .33f;
                sprite_go(i % 3 ? SP_STAR : SP_PUFF, i % 3 ? i & 1 : 2, cx, 356, cosf(a) * 180, sinf(a) * 130, .7f, i % 3 ? (i & 1 ? 0xFFD23F : 0xC9A6FF) : 0xE4E0FF);
            }
        }
        if (hits(u, dt, 3.2f) || hits(u, dt, 3.6f)) s.squash.v -= 3;
        break;
    }
    case PIP_SUNRISE: { /* the sun comes up below him; he stirs, squints, stretches - good morning */
        float u = fmodf(t, 6.0f), k = ease(.3f, 3.2f, u) * (1 - ease(5.4f, 5.95f, u));
        float y = 530 - 128 * k;
        o->sink = -40, o->scale = .88f;
        f->tint = mix(0x9FB4E8, 0xFFEBB8, k);
        uint32_t sun = mix(0xFF7A3A, 0xFFCF3F, k);
        sprite_pin(0, SP_DOT, 4, cx, y, sun);
        float r = 68 + 4 * sinf(t * 3);
        for (int i = 0; i < 10; i++) {
            float a = t * .5f + i * .6283f, yy = y + r * sinf(a);
            if (yy < 462) sprite_pin(1 + i, SP_DOT, 0, cx + r * cosf(a), yy, sun);
        }
        f->s.lid = .8f - .78f * ease(1.0f, 3.0f, u) + .7f * ease(5.4f, 5.95f, u), f->blink = .05f;
        if (u > 1.4f && u < 2.0f) f->lid_asym = .3f; /* one eye first */
        if (u > 3.1f && u < 3.9f) f->s.lid = 0, f->s.w = 104, f->s.h = 150, f->s.smile = 0, f->gy = -.7f, f->tremble = .8f; /* a good stretch */
        else if (u > 3.9f && u < 5.4f) f->s.smile = .8f, f->s.lid = 0, f->s.h = 114, f->gy = -.2f, f->head_roll = .12f;
        if (hits(u, dt, 3.9f)) s.squash.v += 4;
        break;
    }
    case PIP_SHEEP: { /* counts sheep over a fence; heavier and heavier, nods off, catches himself */
        float u = fmodf(t, 6.0f), per = 1.5f;
        o->sink = -48, o->scale = .84f;
        sprite_bar(0, cx - 22, 424, 10, 56, 0x9A6B45);
        sprite_bar(1, cx + 22, 424, 10, 56, 0x9A6B45);
        sprite_bar(2, cx, 412, 74, 9, 0xB58255);
        sprite_bar(3, cx, 432, 74, 9, 0xB58255);
        float sx = cx;
        for (int i = 0; i < 2; i++) {
            float q = fmodf(u / per + i * .5f, 1), x = 60 + 346 * q, v = fminf(1, fmaxf(0, (q - .22f) / .56f)), y = 408 - 62 * 4 * v * (1 - v);
            if (!v || v >= 1) y -= 5 * fabsf(sinf(q * 40));
            sprite_pin(4 + i * 2, SP_WOOL, SP_OWN, x, y, 0xF4F1EA);
            sprite_pin(5 + i * 2, SP_SHEEP, SP_OWN, x, y, 0x8A7A72);
            if (fabsf(x - cx) < fabsf(sx - cx) || sx == cx) sx = x;
        }
        float k = ease(.4f, 4.6f, u) * (1 - ease(5.2f, 5.3f, u));
        show.gaze = true, show.gx = (sx - cx) / 190 * (1 - k * .6f), show.gy = .8f;
        f->s.lid = .3f + .55f * k, f->head_roll = .1f * k, o->sink = -48 + 14 * k;
        if (hits(u, dt, 5.2f)) s.squash.v -= 5, blink();
        if (u > 5.2f) f->s.lid = .05f, f->s.pw = .3f;
        if (u > 3.6f && u < 5.0f && every(0, .7f, dt) && (p = sprite_go(SP_ZED, sc.k++ & 1, 300, 96, 18, -36, 1.4f, 0xBFD4FF))) p->sway = 20;
        break;
    }
    case PIP_NINJA: { /* a red headband, narrowed eyes; a shuriken - dodged! another - dodged! a cool look */
        float u = fmodf(t, 4.2f);
        float dx = u > .75f && u < 1.2f ? 46 : u > 1.75f && u < 2.2f ? -46 : 0;
        o->pull[0][0] = o->pull[1][0] = dx * .5f, o->pull[0][1] = o->pull[1][1] = dx ? -34 : 0;
        o->scale = .9f, o->sink = 10;
        for (int i = 0; i < 2; i++) { /* thrown across at eye level */
            float v = (u - .6f - i) / .5f;
            if (v > 0 && v < 1) sprite_pin(5 + i, (int)(v * 12) & 1 ? SP_SHUR0 : SP_SHUR1, 3, i ? 500 - 530 * v : -30 + 530 * v, 322 + (i ? 30 : -10) * v, 0xDDE6F0);
        }
        float by = (ey0 + ey1) / 2 - 50;
        sprite_bar(0, cx, by, 466, 36, 0xD62839);
        sprite_bar(1, (ex0 + ex1) / 2, by, 66, 26, 0xAEB9C4);
        sprite_pin(2, SP_SHUR0, 2, (ex0 + ex1) / 2, by, 0x5A6672);
        show.gaze = true, show.gx = u > .45f && u < .9f ? -.8f : u > 1.45f && u < 1.9f ? .8f : 0, show.gy = 0;
        if (u > 2.6f) {
            f->s.lid = .42f, f->s.smile = .2f, f->gy = 0;
            if (u < 3.2f) { float v = (u - 2.6f) / .6f; sprite_pin(3, SP_SPARK, v < .2f ? 0 : v < .4f ? 1 : v < .65f ? 3 : v < .82f ? 1 : 0, ex1 + 56, ey1 + 6, 0xFFFFFF); }
        }
        if (hits(u, dt, .75f) || hits(u, dt, 1.75f)) {
            s.squash.v -= 3;
            for (int i = 0; i < 3; i++) sprite_go(SP_DOT, 0, cx + (dx > 0 ? -150 : 150), 210 + i * 34, dx > 0 ? -90 : 90, 0, .3f, 0xB8C0CC);
        }
        break;
    }
    case PIP_SPROUT: { /* three drops of water, and a sprout comes up on his head: a stem, leaves, a flower. He adores it */
        float u = fmodf(t, 6.0f), g = ease(1.2f, 2.6f, u) * (1 - ease(5.6f, 5.95f, u));
        o->sink = 62, o->scale = .86f;
        float bx = cx + s.head_yaw.x * 60, base = cx + sc.sink.x - 62, h = 92 * g, sw = 7 * sinf(t * 2.6f) * g;
        if (h > 6) sprite_bar(0, bx + sw * .3f, base - h / 2, 9, (int)h, 0x5FBF5A);
        if (g > .55f) sprite_pin(1, SP_LEAFR, 2, bx + 22 + sw * .5f, base - h * .5f - 6, 0x7AD66B), sprite_pin(2, SP_LEAFL, 2, bx - 22 + sw * .5f, base - h * .38f - 6, 0x6CC95F);
        if (u > 2.7f && u < 5.65f) {
            float a = u - 2.7f;
            int z = a < .08f ? 1 : a < .18f ? 2 : 3;
            sprite_pin(3, SP_FLOWER, z, bx + sw, base - h - 8, 0xFF7FB0);
            if (z > 1) sprite_pin(4, SP_DOT, 0, bx + sw, base - h - 8, 0xFFD23F);
        }
        if (hits(u, dt, .1f) || hits(u, dt, .4f) || hits(u, dt, .7f)) { if ((p = sprite_go(SP_DROP, 1, bx, 30, 0, 120, .62f, 0x7FC4FF))) p->ay = 500; }
        if (hits(u, dt, .55f) || hits(u, dt, .85f) || hits(u, dt, 1.15f)) s.squash.v += 2;
        if (u < 2.7f) f->gy = -.8f, f->cross = .2f, f->s.pw = .34f, f->s.ph = .37f;
        else if (u < 3.4f) f->gy = -.9f, f->s.h = 140, f->s.smile = .1f, f->cross = .25f;
        else f->s.smile = .85f, f->s.h = 112, f->s.glint = 0, f->gy = -.2f, f->blush = .3f, f->head_roll = .1f * sinf(t * 2.6f);
        if (hits(u, dt, 2.7f)) {
            s.squash.v -= 4;
            for (int i = 0; i < 6; i++) sprite_go(SP_SPARK, i & 1, bx, base - h - 8, cosf(i * 1.047f) * 110, sinf(i * 1.047f) * 90, .5f, 0xFFE27A);
        }
        break;
    }

    /* ---- the thirteen sore ones ---- */
    case PIP_RAINCLOUD: { /* a cloud of his own, raining on him; a look up at it, and down again */
        float u = fmodf(t, 4.4f), x = cx + 14 * sinf(t * 1.1f);
        o->sink = 72, o->scale = .86f;
        sprite_pin(0, SP_CLOUD, SP_OWN, x, 74, 0x7C8AA3);
        if (every(0, .19f, dt) && (p = sprite_go(SP_DROP, 0, x + frand(-66, 66), 126, 0, 170, .85f, 0x7FB4F0))) p->ay = 320;
        if (every(1, .6f, dt)) { int e = frand(0, 1) < .5f; for (int i = 0; i < 2; i++) sprite_go(SP_DOT, 0, eye_cx(e) + frand(-30, 30), eye_cy(e) - s.face.eye[e].h * .34f, i ? 46 : -46, -50, .25f, 0xBFE0FF); }
        if (u > 1.0f && u < 2.2f) f->gy = -.85f, f->s.lid = .14f;
        else if (u > 2.8f) f->s.lid = .42f, f->gy = .8f;
        if (hits(u, dt, 2.8f)) s.squash.v += 3; /* a sigh */
        break;
    }
    case PIP_SOBBING: { /* the waterworks: streams from shut eyes, a puddle rising below */
        float u = fmodf(t, 4.8f), fill = ease(.2f, 4.4f, u);
        o->sink = -22, o->scale = .88f;
        f->head_roll = .07f * sinf(t * 2.6f);
        int top = 420 - (int)(34 * fill);
        sprite_bar(0, cx, (top + 470) / 2.0f, 400, 470 - top, 0x5FA8F0);
        for (int e = 0; e < 2; e++) {
            float x = eye_cx(e), y = eye_cy(e) + 26, w = 34 + 5 * sinf(t * 11 + e);
            sprite_bar(1 + e, x, (y + top + 6) / 2, (int)w, (int)(top + 6 - y), 0x8CC8FF);
            for (int i = 0; i < 2; i++) { /* running down it */
                float q = fmodf(t * 1.7f + i * .5f + e * .3f, 1);
                sprite_pin(3 + e * 2 + i, SP_DROP, 0, x + (i ? 7 : -7), y + 8 + (top - y - 20) * q, 0xE4F4FF);
            }
            if (every(1 + e, .28f + .05f * e, dt)) for (int i = 0; i < 2; i++) { if ((p = sprite_go(SP_DOT, 0, x, top, i ? 70 : -70, -60, .3f, 0xBFE4FF))) p->ay = 240; }
        }
        if (every(0, .5f, dt)) s.squash.v += 3.2f; /* sob */
        break;
    }
    case PIP_HEARTBROKEN: { /* a heart over him beats - cracks - its halves fall; he watches them go */
        float u = fmodf(t, 5.0f);
        o->sink = 66, o->scale = .86f;
        if (u < 1.5f) {
            float b = fmodf(u, .7f);
            sprite_pin(0, b < .12f ? SP_HEART2 : SP_HEART, b < .12f ? SP_OWN : 4, cx, 82, 0xFF4D7A);
            f->gy = -.85f, f->s.lid = .06f, f->s.tilt = -.15f, f->s.pw = .36f, f->s.ph = .4f;
        } else if (u < 3.4f) {
            float w = u - 1.5f, d = w < .35f ? 3 + 14 * w : 8 + 70 * (w - .35f), fall = w < .35f ? 0 : 260 * (w - .35f) * (w - .35f);
            sprite_pin(0, SP_HALFL, 4, cx - d, 82 + fall, 0xC43A62);
            sprite_pin(1, SP_HALFR, 4, cx + d, 82 + fall * 1.15f, 0xC43A62);
            f->gy = w < .35f ? -.8f : fminf(.9f, -.8f + 1.4f * (w - .35f));
            if (w < .5f) f->s.lid = 0, f->s.h = 146, f->s.pw = .2f, f->s.ph = .22f, f->s.tilt = -.3f, f->tremble = 1;
        } else f->s.lid = .48f, f->gy = .85f, f->s.tilt = -.4f, f->tint = 0xCCD6E4;
        if (hits(u, dt, 1.5f)) {
            s.squash.v += 5;
            for (int i = 0; i < 5; i++) if ((p = sprite_go(SP_DOT, 0, cx, 80, (i - 2) * 60, -60, .5f, 0xFF8FB0))) p->ay = 300;
        }
        if (u > 3.5f && every(0, .9f, dt)) { sc.k ^= 1; if ((p = sprite_go(SP_DROP, 0, eye_cx(sc.k) + (sc.k ? 26 : -26), eye_cy(sc.k) + 40, 0, 20, 1.1f, 0x9ED8FF))) p->ay = 200; }
        break;
    }
    case PIP_FUMING: { /* holds it in, reddening and swelling - and steam whistles out at both sides */
        float u = fmodf(t, 4.0f), k = ease(.1f, 1.9f, u) * (1 - ease(3.4f, 3.9f, u));
        bool blow = u > 1.9f && u < 3.3f;
        o->scale = .86f + .07f * k - (blow ? .05f : 0), o->sink = 8;
        f->tint = mix(0xFFFFFF, 0xFF5A3A, k), f->tremble = .5f + 2.8f * k;
        f->s.lid = .3f + .06f * k, f->s.tilt = .4f + .15f * k;
        if (blow && every(0, .1f, dt)) {
            sc.k ^= 1;
            float sd = sc.k ? 1 : -1;
            if ((p = sprite_go(SP_PUFF, 2 + (int)frand(0, 1.99f), cx + sd * 158, 128, sd * frand(30, 60), -frand(60, 100), .7f, 0xE8E4E0))) p->ay = -40;
        }
        if (u > .8f && u < 3.4f) sprite_pin(0, SP_ANGER, 3, 316, 72 + (fmodf(t, .3f) < .15f ? 3 : -3), 0xFF3B30);
        if (hits(u, dt, 1.9f)) s.squash.v -= 6;
        if (blow && every(1, .16f, dt)) s.squash.v += frand(-2.5f, 2.5f), s.head_roll.v += frand(-1.2f, 1.2f);
        break;
    }
    case PIP_BURNING: { /* so angry his eyes are on fire */
        static const uint8_t fr[4] = {SP_FLAME0, SP_FLAME1, SP_FLAME2, SP_FLAME1};
        float fl = .5f + .5f * sinf(t * 9);
        o->sink = 36, o->scale = .9f;
        f->tint = mix(0xFF6A2A, 0xFFA53A, fl);
        f->head_roll = .04f * sinf(t * 2);
        for (int e = 0; e < 2; e++) {
            float x = eye_cx(e), y = eye_cy(e) - s.face.eye[e].h * .30f, sd = e ? 1 : -1;
            sprite_pin(e * 5, fr[(int)(t * 8 + e) & 3], 3, x - sd * 6, y - 22, 0xFF5A1F);
            sprite_pin(e * 5 + 1, fr[(int)(t * 9 + 2 + e) & 3], 2, x - sd * 44, y + 4, 0xFF7A2A);
            sprite_pin(e * 5 + 2, fr[(int)(t * 7 + 1 + e) & 3], 2, x + sd * 34, y - 18, 0xFF7A2A);
            sprite_pin(e * 5 + 3, fr[(int)(t * 10 + e) & 3], 2, x - sd * 6, y - 12, 0xFFC23A);
            sprite_pin(e * 5 + 4, fr[(int)(t * 9 + 1) & 3], 1, x + sd * 34, y - 12, 0xFFD86B);
        }
        if (every(0, .22f, dt)) {
            int e = frand(0, 1) < .5f;
            if ((p = sprite_go(SP_DOT, 0, eye_cx(e) + frand(-40, 40), eye_cy(e) - 90, frand(-20, 20), -frand(70, 120), .9f, frand(0, 1) < .5f ? 0xFF8A2A : 0xFFD23F))) p->sway = 30;
        }
        break;
    }
    case PIP_SULKING: { /* turned his back on you: a glance over, "hmf", and away again */
        float u = fmodf(t, 5.0f);
        bool peek = u > 1.9f && u < 3.0f;
        show.gaze = true, show.out = true, o->scale = .92f;
        show.gx = peek ? -.35f : 1.2f, show.gy = peek ? .1f : .25f;
        float k = 1 - ease(1.85f, 2.15f, u) * (1 - ease(2.95f, 3.2f, u));
        f->size_asym = -k, o->sink = 10, o->spread = .22f * k;
        f->s.lid = peek ? .22f : .34f, f->blink = .3f;
        if (hits(u, dt, 3.0f)) {
            s.squash.v += 4;
            for (int i = 0; i < 2; i++) sprite_go(SP_PUFF, 1 + i, 170 - i * 24, 350 + i * 12, -60 - 30 * i, -20, .8f, 0xC9CCD4);
        }
        if (u > 3.0f && u < 3.4f) f->s.h = 96, f->s.lid = .5f;
        if (u > 4.0f && hits(u, dt, 4.2f)) s.squash.v -= 2;
        break;
    }
    case PIP_SPOOKED: { /* dark: only his pupils glow, darting. A ghost! - eyes wide - it goes by, dark again */
        float u = fmodf(t, 6.0f);
        bool seen = u > 1.9f && u < 4.9f;
        o->sink = -44, o->scale = .86f;
        show.gaze = true;
        if (!seen) {
            static const float g[6] = {-.8f, .7f, 0, .9f, -.6f, .2f};
            f->tint = 0x010101, f->s.pw = .16f, f->s.ph = .2f, f->s.w = 112, f->s.h = 116, f->tremble = .8f;
            show.gx = g[(int)(u / .45f) % 6], show.gy = .15f * sinf(u * 7);
        } else {
            float v = (u - 1.9f) / 3.0f, x = 540 - 620 * v, y = 382 + 14 * sinf(u * 5);
            sprite_pin(0, SP_GHOST, SP_OWN, x, y, 0xD8E4FF);
            show.gx = (x - cx) / 170, show.gy = .6f;
            f->s.h = 150, f->tremble = 2.6f;
            if (fabsf(v - .5f) < .02f) f->s.lid = .1f;
        }
        if (hits(u, dt, 1.9f)) s.squash.v -= 5;
        if (hits(u, dt, 4.9f)) blink();
        break;
    }
    case PIP_TUMBLEWEED: { /* nothing happens. A tumbleweed rolls by. He watches it go. A sigh */
        float u = fmodf(t, 6.0f), v = (u - .5f) / 3.4f;
        o->sink = -36, o->scale = .9f;
        sprite_bar(0, cx, 432, 270, 5, 0x8A6B45);
        show.gaze = true, show.gx = u < .5f ? -.2f : 0, show.gy = -.25f;
        if (v > 0 && v < 1) {
            float x = -50 + 566 * v, h = fabsf(sinf(v * 11)) * 46;
            sprite_pin(1, SP_WEED0 + ((int)(v * 22) % 3), SP_OWN, x, 392 - h, 0xC79A5B);
            show.gx = (x - cx) / 190, show.gy = .5f;
            if (every(0, .3f, dt)) sprite_go(SP_DOT, 0, x - 20, 424, -30, -24, .4f, 0xB08C5A);
        }
        if (u > 4.1f) f->s.lid = .62f, show.gy = .3f;
        if (hits(u, dt, 4.3f)) s.squash.v += 3.5f;
        if (hits(u, dt, 5.2f)) blink();
        break;
    }
    case PIP_CHARGED: { /* full: the plug pops out with a spark, four pulses of charge flash through his eyes, a proud stretch */
        float u = fmodf(t, 4.0f), out = ease(.5f, .75f, u);
        o->sink = 40, o->scale = .88f;
        if (u < .9f) { /* the plug, then out it pops */
            float py = 408 + 140 * out;
            sprite_bar(0, cx, py + 62, 12, 80, 0x5A5F66), sprite_bar(1, cx, py + 8, 46, 34, 0xE8EAED), sprite_bar(2, cx, py - 17, 28, 18, 0xA9AFB8);
            f->s.lid = .2f, f->gy = .5f;
        }
        if (hits(u, dt, .5f)) {
            s.squash.v -= 4;
            for (int i = 0; i < 5; i++) sprite_go(SP_SPARK, i & 1, cx + (i - 2) * 14, 392, (i - 2) * 70, -frand(80, 150), .45f, i & 1 ? 0xFFF3B0 : 0x9CF56B);
        }
        if (u > .8f && u < 2.4f) { /* four pulses of charge: a ring of green sparks off his eyes, a green flash in them, a hop */
            float b = fmodf(u - .8f, .4f);
            f->tint = mix(0xFFFFFF, 0xB8FFA0, 1 - ease(0, .3f, b));
            f->s.h = 128, f->s.smile = .25f + .4f * ease(.8f, 2.4f, u), f->s.taper = -.08f, f->s.glint = .5f;
            if (b < dt) {
                s.squash.v -= 2.4f;
                for (int i = 0; i < 6; i++) {
                    float a = i * 1.0472f + u * 2;
                    sprite_go(SP_SPARK, 1 + (i & 1), cx + 70 * cosf(a), 246 + 60 * sinf(a), 210 * cosf(a), 170 * sinf(a), .45f, i & 1 ? 0x9CF56B : 0xD8FFB0);
                }
            }
        }
        if (u > 2.4f && u < 3.9f) /* charged up: sparks going round him */
            for (int i = 0; i < 4; i++) {
                float a = t * 3 + i * 1.5708f, k = ease(2.4f, 2.7f, u) * (1 - ease(3.6f, 3.9f, u));
                if (k > .05f) sprite_pin(3 + i, SP_SPARK, k > .6f ? 2 : 1, cx + 170 * cosf(a), 250 + 120 * sinf(a), i & 1 ? 0x9CF56B : 0xD8FFB0);
            }
        if (u > 2.4f && u < 3.2f) f->s.w = 106, f->s.h = 142, f->s.smile = .1f, f->gy = -.4f, f->tremble = .6f, o->sink = 50, show.gaze = false; /* a good stretch */
        else if (u >= 3.2f) f->s.smile = .9f, f->s.h = 112, f->gy = -.2f, f->head_roll = .1f;   /* aah, full */
        if (hits(u, dt, 3.2f)) {
            s.squash.v += 4;
            if ((p = sprite_go(SP_BOLT, 2, cx, 120, 0, -60, .9f, 0x7CF06B))) p->ay = -40;
        }
        break;
    }
    case PIP_CUTOFF: { /* not done: the plug yanked away, his eyes after it; they go dim and flicker (30..79%: under it,
                          PIP_EMPTY), the last sparks fizzle where the plug was, he leans after the cable - and sighs */
        float u = fmodf(t, 4.4f), gone = ease(.45f, .7f, u);
        o->scale = .9f;
        if (u < .9f) { /* drinking - and it is gone, sparks fizzling after it */
            float py = 408 + 150 * gone;
            sprite_bar(0, cx, py + 62, 12, 80, 0x5A5F66), sprite_bar(1, cx, py + 8, 46, 34, 0xE8EAED), sprite_bar(2, cx, py - 17, 28, 18, 0xA9AFB8);
            if (u < .45f) f->s.lid = .34f, f->s.smile = .2f, f->gy = .5f, f->s.tilt = 0;
            else show.gaze = true, show.gx = 0, show.gy = .9f, o->sink = 30 * gone, f->s.h = 130, f->s.lid = 0, f->s.tilt = 0; /* hey! */
        }
        if (hits(u, dt, .45f)) {
            s.squash.v += 4;
            for (int i = 0; i < 4; i++) if ((p = sprite_go(SP_SPARK, 0, cx + (i - 1.5f) * 12, 396, (i - 1.5f) * 40, -frand(30, 70), .5f, 0xFFB04A))) p->ay = 260;
        }
        if (u > .7f && u < 3.4f) { /* not enough in him: the eyes go dim and flicker like a bulb short of current */
            bool off = fmodf(u, .9f) < .08f;
            f->tint = off ? 0x9AA0A8 : 0xD6DAE0;
            if (every(1, .6f, dt) && (p = sprite_go(SP_SPARK, 0, cx + frand(-30, 30), 440, frand(-40, 40), -frand(60, 110), .5f, 0xFFB04A)))
                p->ay = 240; /* where the plug was: the last sparks */
        }
        if (u > .9f && u < 2.2f) { /* a look at you: is that all? */
            show.gaze = true, show.gx = .2f * sinf(t * 1.5f), show.gy = -.1f;
            f->s.lid = .12f, f->s.tilt = -.22f, f->s.h = 124;
        } else if (u > 2.2f && u < 3.4f) { /* leaning after the cable */
            show.gaze = true, show.gx = .15f * sinf(t * 6), show.gy = .85f;
            o->sink = 36, f->s.tilt = -.28f, f->head_roll = .06f * sinf(t * 7), f->s.pw = .38f, f->s.ph = .41f;
        } else if (u >= 3.4f) f->s.lid = .42f, f->s.tilt = -.2f, f->gy = .5f, o->sink = 10; /* a sigh */
        if (hits(u, dt, 2.2f)) s.squash.v -= 3;
        if (hits(u, dt, 3.4f)) { /* a sigh, a puff of it */
            s.squash.v += 3;
            if ((p = sprite_go(SP_PUFF, 2, cx, 380, 0, -40, .9f, 0xC9CED8))) p->sway = 20;
        }
        break;
    }
    case PIP_QUEASY: { /* too much food: green and swaying like at sea; his cheeks puff, he holds it - a retch, eyes popping - then
                          woozy, eyes wandering, a cold sweat */
        float u = fmodf(t, 4.2f), sea = sinf(t * 2.2f);
        o->sink = 16 + 8 * sinf(t * 1.1f);
        f->head_roll = .12f * sea, f->lid_asym = .14f * sea;
        f->tint = mix(0xD2F2B4, 0xB6E08C, .5f + .5f * sinf(t * 3));
        if (u > 1.0f && u < 1.6f) { /* cheeks puffed, holding it in */
            float k = ease(1.0f, 1.15f, u);
            f->s.h = 120 - 50 * k, f->s.w = 116 + 10 * k, f->s.smile = 0, f->s.lid = 0, f->s.tilt = -.1f, f->tremble = 1.2f * k;
            o->spread = .08f * k, o->scale = 1 + .06f * k;
            if (hits(u, dt, 1.0f)) s.squash.v += 3;
            if (k > .3f) for (int i = 0; i < 2; i++) sprite_pin(i, SP_DOT, k > .8f ? 3 : 2, eye_cx(i) + (i ? 38 : -38), eye_cy(i) + 74, 0x7FAE52); /* puffed cheeks */
        } else if (u >= 1.6f && u < 2.0f) { /* the retch */
            f->s.h = 136, f->s.lid = 0, f->s.tilt = 0, f->s.pw = .26f, f->s.ph = .28f, f->gy = .5f, f->tremble = 1.5f, o->sink = 34;
            if (hits(u, dt, 1.6f)) {
                s.squash.v -= 5, s.head_pitch.v += 4;
                for (int i = 0; i < 3; i++) if ((p = sprite_go(SP_DOT, i == 1 ? 1 : 0, cx + (i - 1) * 20, 390, (i - 1) * 30, -frand(60, 100), .7f, 0xA8D86A))) p->sway = 20;
            }
        } else if (u >= 2.0f) { /* woozy after it: the eyes wander round slowly */
            show.gaze = true, show.gx = .35f * cosf(t * 1.4f), show.gy = .25f + .2f * sinf(t * 1.4f);
            f->s.lid = .38f, f->cross = .15f;
        }
        if (hits(u, dt, 2.3f) && (p = sprite_go(SP_DROP, 1, eye_cx(1) + 66, eye_cy(1) - 40, 0, 20, 1.4f, 0x9ED8FF))) p->ay = 30; /* a cold sweat */
        break;
    }
    case PIP_EMPTY: { /* under 30%: the plug yanked - a gasp - his eyes go out like an old TV, a line, a dot; dark a moment; they
                         flicker back on, weak and grey, and he sighs */
        float u = fmodf(t, 4.6f), gone = ease(.45f, .7f, u);
        o->scale = .9f;
        if (u < .9f) { /* the plug, then gone */
            float py = 408 + 150 * gone;
            sprite_bar(0, cx, py + 62, 12, 80, 0x5A5F66), sprite_bar(1, cx, py + 8, 46, 34, 0xE8EAED), sprite_bar(2, cx, py - 17, 28, 18, 0xA9AFB8);
        }
        if (u < .45f) f->s.lid = .34f, f->s.tilt = 0, f->s.smile = .2f, f->gy = .5f, f->tint = 0xFFFFFF;
        else if (u < .85f) f->s.lid = 0, f->s.tilt = 0, f->s.h = 136, f->s.pw = .26f, f->s.ph = .28f, f->tint = 0xFFFFFF, f->gy = .6f; /* a gasp */
        else if (u < 1.5f) { /* going out: squeezed to a line, then to a dot */
            float k = ease(.85f, 1.25f, u);
            f->s.lid = 0, f->s.tilt = 0, f->s.h = 120 - 112 * k, f->s.w = 116 - 60 * ease(1.25f, 1.5f, u), f->tint = mix(0xFFFFFF, 0x9AA0A8, k);
            o->stiff = 500;
        } else if (u < 2.4f) { /* dark: two faint dots */
            f->s.h = 4, f->s.w = 8, f->s.lid = 0, f->tint = 0x5A5F66, o->stiff = 500;
            if (u < 1.9f) for (int i = 0; i < 2; i++) sprite_pin(3 + i, SP_DOT, 0, eye_cx(i), eye_cy(i), mix(0xFFFFFF, 0x000000, ease(1.5f, 1.9f, u)));
        } else if (u < 3.2f) { /* flickering back on */
            static const float fl[8] = {.25f, 0, .55f, .1f, .8f, .3f, 1, 1};
            float k = fl[(int)((u - 2.4f) / .1f) & 7];
            f->s.h = 6 + 106 * k, f->s.w = 60 + 56 * k, f->s.lid = .3f * k, f->tint = 0x8A9098, o->stiff = 900;
        } else f->s.lid = .38f, f->tremble = .5f, f->gy = .5f, o->sink = 12; /* weak, grey */
        if (hits(u, dt, .45f)) {
            s.squash.v += 4;
            for (int i = 0; i < 4; i++) if ((p = sprite_go(SP_SPARK, 0, cx + (i - 1.5f) * 12, 396, (i - 1.5f) * 40, -frand(30, 70), .5f, 0xFFB04A))) p->ay = 260;
        }
        if (hits(u, dt, 3.8f)) {
            s.squash.v += 3;
            if ((p = sprite_go(SP_PUFF, 2, cx, 380, 0, -40, .9f, 0xC9CED8))) p->sway = 20;
        }
        break;
    }
    case PIP_LOWBATT: { /* run down: an empty battery blinks over him, the lids sink, he jerks up and sinks again */
        float u = fmodf(t, 4.2f), k = ease(.2f, 3.2f, u) * (1 - ease(3.5f, 3.6f, u));
        o->sink = 56 + 18 * k, o->stiff = 60, o->scale = .86f;
        f->s.lid = .42f + .42f * k, f->head_roll = .1f + .06f * k;
        f->lid_asym = .08f * sinf(t * 1.5f);
        sprite_bar(0, cx, 78, 124, 60, 0xC9CED8);
        sprite_bar(1, cx + 67, 78, 12, 26, 0xC9CED8);
        sprite_bar(2, cx, 78, 110, 46, 0x08090B);
        if (fmodf(t, 1.0f) < .6f) sprite_bar(3, cx - 40, 78, 20, 34, 0xFF3B30);
        if (hits(u, dt, 3.5f)) s.squash.v -= 4;
        break;
    }
    case PIP_HUNGRY: { /* dreams of a biscuit: it hangs in a cloud over him, gets bitten - and is gone */
        float u = fmodf(t, 5.6f);
        o->sink = 78, o->scale = .82f;
        bool dream = u > .5f && u < 4.2f;
        if (u > .2f && u < 4.2f) sprite_pin(0, SP_DOT, 0, cx + 4, 172, 0xE6EAF2);
        if (dream) {
            float b = 3 * sinf(t * 2.4f);
            sprite_pin(1, SP_CLOUD, SP_OWN, cx, 82 + b, 0xE6EAF2);
            sprite_pin(2, u < 2.6f ? SP_COOKIE : SP_COOKIE2, 3, cx, 84 + b, 0xB87433);
        }
        if (dream) { f->gy = -.9f, f->s.pw = .4f, f->s.ph = .44f, f->s.lid = .06f; if (u > 1.2f && every(0, .9f, dt) && (p = sprite_go(SP_DROP, 0, cx, 352, 0, 14, 1.0f, 0x9ED8FF))) p->ay = 120; }
        else f->s.lid = .34f, f->gy = .7f;
        if (u > 2.4f && u < 2.9f) f->s.smile = .8f, f->s.h = 108, f->s.lid = 0;
        if (hits(u, dt, 4.2f)) {
            s.squash.v += 4;
            for (int i = 0; i < 6; i++) sprite_go(SP_PUFF, 1, cx + (i - 2.5f) * 24, 84, (i - 2.5f) * 50, i & 1 ? -30 : 40, .5f, 0xE6EAF2);
        }
        if (hits(u, dt, 1.6f) || hits(u, dt, 4.9f)) s.squash.v += 4, s.head_roll.v += 2; /* a rumble in the belly */
        if (u > 4.8f && u < 5.2f) f->tremble = 2;
        break;
    }
    case PIP_KNOCKOUT: { /* out for the count: lids heavy and uneven, the pupils drifting round on their own, cross-eyed, the head
                          lolling round with the stars; now and then he tries to focus - and goes again */
        float u = fmodf(t, 4.0f), a = t * 2.2f, gone = 1 - ease(2.4f, 2.7f, u) * (1 - ease(3.3f, 3.7f, u));
        o->spread = -.05f, o->sink = 44 + 6 * sinf(a);
        f->head_roll = .2f * sinf(t * 1.57f) * gone, f->tremble = .6f;
        show.gaze = true, show.gx = .55f * cosf(-a) * gone, show.gy = .1f + .4f * sinf(-a) * gone; /* round and round */
        f->cross = .45f * gone, f->lid_asym = .22f * sinf(t * .9f) * gone;
        f->s.lid = .42f - .3f * (1 - gone), f->s.h = 116 + 14 * (1 - gone);
        if (hits(u, dt, 2.4f)) s.squash.v += 3, s.head_pitch.v -= 2; /* a shake of the head: where am I */
        if (hits(u, dt, 3.4f)) s.squash.v -= 4;                       /* ...and gone again */
        for (int i = 0; i < 5; i++) {
            float a = t * 2.2f + i * 1.2566f;
            sprite_pin(4 + i, SP_STAR, sinf(a) > .2f ? 2 : sinf(a) > -.5f ? 1 : 0, cx + 128 * cosf(a), 96 + 30 * sinf(a), i & 1 ? 0xFFD23F : 0xFFF0B0);
        }
        if (t < dt) s.squash.v += 6;
        break;
    }
    case PIP_WILTED: { /* a daisy: its petals go one by one, and with them his hopes */
        float u = fmodf(t, 6.4f), y = 352;
        int left = u < .8f ? 8 : 8 - (int)((u - .8f) / .5f + 1);
        if (left < 0) left = 0;
        o->sink = -54, o->scale = .84f;
        float in = ease(0, .4f, u) * (1 - ease(5.9f, 6.3f, u));
        y += 140 * (1 - in);
        float droop = left ? 0 : 8;
        sprite_bar(0, cx, y + 60, 7, 90, 0x5A8A4A);
        sprite_pin(1, SP_LEAFR, 2, cx + 22, y + 66, 0x6A9A55);
        for (int i = 0; i < left; i++) sprite_pin(2 + i, SP_DOT, 1, cx + 29 * cosf(-1.57f + i * .7854f), y + 29 * sinf(-1.57f + i * .7854f), 0xE8E2F4);
        sprite_pin(10, SP_DOT, 1, cx, y + droop, left ? 0xFFC93A : 0xB08A3A);
        if (u >= .8f && u < 4.8f && fmodf(u - .8f, .5f) < dt) {
            float a = -1.57f + left * .7854f;
            if ((p = sprite_go(SP_DOT, 1, cx + 29 * cosf(a), y + 29 * sinf(a), 30 * cosf(a), 26, 1.3f, 0xCFC6E6))) p->sway = 40, p->ph = u;
            if (left & 1) blink();
        }
        float k = ease(.8f, 4.8f, u);
        f->gy = .75f, f->s.lid = .16f + .22f * k, f->s.tilt = -.2f - .2f * k, f->tint = mix(0xFFFFFF, 0xB8C0CC, k), f->head_roll = -.1f * k;
        if (u > 4.8f) f->s.lid = .6f;
        if (hits(u, dt, 5.0f)) s.squash.v += 3.5f;
        break;
    }
    case PIP_SNIFFLES: { /* a cold: a red nose, a striped scarf, sniff... ah... ah... a little sneeze */
        float u = fmodf(t, 4.6f);
        o->sink = -34, o->scale = .84f;
        float nx = cx + s.head_yaw.x * 90, ny = 262 + s.head_pitch.x * 60;
        bool ah = u > 2.4f && u < 3.2f;
        if (hits(u, dt, .5f) || hits(u, dt, 1.2f)) s.squash.v -= 2, sc.puff = .16f;
        if ((sc.puff -= dt) > 0) ny -= 7;
        if (ah) { float k = (u - 2.4f) / .8f; ny -= 18 * k; f->s.lid = .45f - .1f * k, f->gy = -.6f * k, f->s.h = 120 + 16 * k, o->sink = -34 - 10 * k; }
        else if (u > 3.2f && u < 3.6f) { f->s.w = 126, f->s.h = 34, f->s.roll = .24f, f->s.lid = 0, f->s.wet = 0; ny += 14; o->sink = -14; }
        sprite_bar(0, cx, 412, 380, 54, 0x3F7BD9);
        for (int i = 0; i < 4; i++) sprite_bar(1 + i, cx - 140 + i * 62, 412, 12, 54, 0x8FB8F5);
        sprite_bar(5, cx + 92, 424, 58, 84, 0x2F62B5); /* the end hanging over it */
        sprite_bar(8, cx + 92, 420, 58, 10, 0x8FB8F5);
        sprite_bar(9, cx + 92, 396, 64, 34, 0x4D8AE8); /* the knot */
        sprite_pin(6, SP_DOT, 2, nx, ny, 0xFF5C5C);
        sprite_pin(7, SP_DOT, 0, nx - 8, ny - 8, 0xFFA6A0);
        if (hits(u, dt, 3.2f)) {
            s.squash.v += 6;
            for (int i = 0; i < 6; i++) if ((p = sprite_go(SP_DOT, 0, nx, ny + 20, (i - 2.5f) * 70, frand(60, 120), .4f, 0xCFE6FF))) p->ay = 200;
        }
        break;
    }
    case PIP_NEWTREE: { /* a little fir below him, its lights running, snow falling; at 1.7 s (VOICE_JINGLE's ding) the star on top
                           lights - wow, the eyes up at it - and he beams at you, hopping. The lights' colours play on his eyes */
        static const uint32_t bc[4] = {0xFF4D4D, 0xFFD23F, 0x5AB8FF, 0xFF7AD9};
        static const int16_t bx[8] = {-12, 16, -28, 26, -4, -42, 40, 6}, by[8] = {352, 364, 382, 392, 404, 420, 428, 440};
        float u = fmodf(t, 4.8f), ty = 40 * (1 - ease(0, .45f, t)); /* the tree comes up once, as the face comes */
        int run = (int)(t * 6);
        bool star = u > 1.7f;
        o->sink = -46, o->scale = .86f;
        sprite_pin(0, SP_FIR, SP_OWN, cx, 392 + ty, 0x2E9E5B);
        sprite_bar(1, cx, 462 + ty, 22, 14, 0x8A5A32);
        for (int i = 0; i < 8; i++) {
            uint32_t c = bc[i & 3];
            sprite_pin(2 + i, SP_DOT, 0, cx + bx[i], by[i] + ty, (run + i * 3) % 5 < 3 ? c : mix(c, 0x1E5A38, .65f));
        }
        sprite_pin(10, SP_STAR, star && u < 2.0f ? 3 : 2, cx, 318 + ty, star ? 0xFFD23F : 0x8C7F4A);
        f->tint = mix(0xFFFFFF, bc[run & 3], .14f);
        if (!star) { /* looking it over */
            show.gaze = true, show.gx = .3f * sinf(t * 1.3f), show.gy = .7f;
            f->s.smile = .35f, f->head_roll = .05f * sinf(t * 1.3f);
        } else if (u < 2.4f) { /* the star: wow */
            show.gaze = true, show.gx = 0, show.gy = .45f;
            f->s.h = 138, f->s.pw = .3f, f->s.ph = .32f, f->s.smile = .1f;
        } else { /* beaming at you */
            f->s.smile = .62f, f->s.h = 112, f->gy = -.1f, f->head_roll = .08f * sinf(t * 3);
            if (u < 4.2f && every(0, .6f, dt)) s.squash.v -= 2.4f;
        }
        if (hits(u, dt, 1.7f)) {
            s.squash.v -= 4;
            for (int i = 0; i < 6; i++) {
                float a = -1.57f + (i - 2.5f) * .55f;
                sprite_go(SP_SPARK, i & 1, cx, 318 + ty, cosf(a) * 120, sinf(a) * 120, .6f, i & 1 ? 0xFFF3C0 : 0xFFD23F);
            }
        }
        if (every(1, .45f, dt) && (p = sprite_go(SP_FLAKE, frand(0, 1) < .5f, frand(70, 396), 36, frand(-8, 8), frand(38, 58), 3.0f, 0xEAF4FF)))
            p->sway = 26, p->ph = t;
        break;
    }
    case PIP_WREATH: { /* a Christmas wreath with a red bow comes down on a ribbon (VOICE_HANGUP's swoop), swinging, his eyes after
                          it; it settles, and at 2 s its lights come on (the bells) - wow - then he sways with it, beaming, snow
                          falling. It comes down once; looped, it hangs on, swinging, the lights running */
        static const uint32_t bc[4] = {0xFF4D4D, 0xFFD23F, 0x5AB8FF, 0xFFF3E0};
        float k = ease(0, .9f, t), y = -90 + 198 * k, amp = 8 + 18 * expf(-fmaxf(0, t - 1) * .8f), sw = amp * sinf(t * 2.6f), wx = cx + sw;
        if (t > .9f) y += 12 * sinf((t - .9f) * 10) * expf(-(t - .9f) * 4); /* a bounce on the ribbon as it stops */
        bool lit = t > 2.0f;
        int run = (int)(t * 5);
        o->sink = 62, o->scale = .84f;
        if (y > 2) sprite_bar(0, wx, (y - 60) / 2, 6, (int)fmaxf(2, y - 60), 0xC22E2E);
        sprite_pin(1, SP_WREATH, SP_OWN, wx, y, 0x2E8B4E);
        sprite_pin(2, SP_BOW, SP_OWN, wx, y - 62, 0xE23B3B);
        for (int i = 0; i < 8; i++) {
            float a = .3927f + i * .7854f;
            uint32_t c = bc[i & 3];
            sprite_pin(3 + i, SP_DOT, 0, wx + cosf(a) * 53, y + sinf(a) * 53, lit && (run + i * 3) % 5 < 3 ? c : mix(c, 0x1E5A38, .7f));
        }
        if (lit) f->tint = mix(0xFFFFFF, bc[run & 3], .12f);
        if (!lit) { /* watching it come and swing */
            show.gaze = true, show.gx = sw / 80, show.gy = -.8f;
            f->s.smile = .3f, f->head_roll = sw * .003f;
        } else if (t < 2.5f) { /* the lights: wow */
            show.gaze = true, show.gx = sw / 80, show.gy = -.7f;
            f->s.h = 138, f->s.pw = .3f, f->s.ph = .32f, f->s.smile = .1f;
        } else { /* beaming, swaying with it */
            f->s.smile = .62f, f->s.h = 112, f->gy = -.2f, f->head_roll = sw * .004f;
            if (fmodf(t, 5) < 1.6f && every(0, .55f, dt)) s.squash.v -= 2.4f;
        }
        if (hits(t, dt, .9f)) s.squash.v += 2; /* it lands: a little flinch */
        if (hits(t, dt, 2.0f)) {
            s.squash.v -= 4;
            for (int i = 0; i < 8; i++) {
                float a = i * .7854f;
                sprite_go(i & 1 ? SP_STAR : SP_SPARK, i & 1 ? 0 : 1, wx + cosf(a) * 60, y + sinf(a) * 60, cosf(a) * 110, sinf(a) * 110, .7f, bc[i & 3]);
            }
        }
        if (lit && every(1, .5f, dt) && (p = sprite_go(SP_FLAKE, frand(0, 1) < .5f, frand(70, 396), 36, frand(-8, 8), frand(38, 58), 3.0f, 0xEAF4FF)))
            p->sway = 26, p->ph = t;
        break;    }
    }
}
/* the eyes of this frame are known: what is stuck to them, and the pictures move on */
static void show_after(int mood, float dt)
{
    if (!show.live) sprite_unpin();
    if (s.dark || away || covered || unmet()) { sprite_clear(); return; }
    sprite_step(dt);
    if (show.live && mood == PIP_STARSTRUCK)
        for (int i = 0; i < 2; i++) { /* a star for a pupil, smaller as the lids close on it */
            const eye_t *e = &s.face.eye[i];
            float x, y;
            pupil_xy(e, &x, &y);
            if (e->h > 50) sprite_pin(i, SP_STAR, e->h > 120 ? 4 : e->h > 80 ? 3 : 2, x, y, 0xFFB300);
        }
}

static bool scene_mood(int mood) { return mood >= PIP_SQUISHED && mood <= PIP_GROGGY; }
#define PULL_PX 70.0f /* how far a finger can drag an eye */
/* the fingers on his eyes: each eye where its finger has taken it, up to PULL_PX (to[eye][x, y]) */
static void fingers_pull(float to[2][2], float k)
{
    to[0][0] = to[0][1] = to[1][0] = to[1][1] = 0;
    for (int f = 0; f < 2; f++) {
        int e = mt.eye_of[f];
        if (e < 0 || e > 1) continue;
        float dx = (mt.x[f] - mt.sx[f]) * k, dy = (mt.y[f] - mt.sy[f]) * k, d = hypotf(dx, dy);
        if (d > PULL_PX) dx *= PULL_PX / d, dy *= PULL_PX / d;
        to[e][0] = dx, to[e][1] = dy;
    }
}
/* the palm lifted: the air pressed out of him puffs from the sides of his flattened eyes - small, quick, sideways */
static void palm_puffs(void)
{
    for (int i = 0; i < 8; i++) {
        int e = i % 2;
        float sd = e ? 1 : -1;
        dot(eye_cx(e) + sd * frand(55, 75), eye_cy(e) + frand(-10, 10), sd * frand(140, 220), -frand(160, 240), frand(0.3f, 0.45f),
            (int)frand(3, 6), 0xFFFFFF);
    }
}
static const mood_def_t *scene_face(int mood, float dt, const mood_def_t *m)
{
    static mood_def_t f;
    if (away) { sc.mood = -1, show.live = show.out = show.gaze = false; return m; } /* off his face: no scene runs unseen (it starts when he is back) */
    if (mood != sc.mood) sc.mood = mood, sc.t = 0, sc.beat = 0, sc.puff = 0, sc.look = 0, sc.k = 0, sc.burst = false, sc.spin = 0;
    else sc.t += dt;
    sc.let_t += dt;
    float t = sc.t, spread = 0, sink = 0, scale = 1, stiff = 160, zeta = 0.8f, pull[2][2] = {{0, 0}, {0, 0}}, pk = 260, pz = 0.8f;
    if (scene_mood(mood) && m == &moods[mood]) { /* a sneeze, a burp, a boop over it: theirs, not the scene's */
        f = *m;
        switch (mood) {
        case PIP_SQUISHED: /* flat and sunk under the hand; let go, he pops up with a wobble, blinks it off, shakes his head */
            if (mt.kind == MT_PALM || sc.let_t > 1.6f) { /* under the hand (or shown pinned: the flat face) */
                sink = 34, f.tremble = 1.4f;
                if ((sc.beat -= dt) <= 0) sc.beat = 0.5f, s.squash.v -= 2;
                break;
            }
            if (sc.let_t < dt * 1.5f) s.squash.v += 10, palm_puffs(); /* boing: the air pressed out of him puffs away */
            stiff = 400;
            f.s.w = 122, f.s.h = 142, f.s.pw = .3f, f.s.ph = .32f, f.cross = 0, f.blink = 5, f.tremble = 0;
            f.wag = sc.let_t < 0.9f ? 0.2f * (1 - sc.let_t / 0.9f) : 0; /* a shake like a wet dog's */
            break;
        case PIP_STRETCHED: /* each eye goes where its finger takes it, drawn out that way; let go, they twang back, then cross */
            if (mt.kind == MT_STRETCH) fingers_pull(pull, 0.85f);
            else if (sc.let_t > 1.6f) spread = 0.12f; /* pinned, no fingers: pulled a little apart */
            else {
                pk = 300, pz = 0.12f; /* let go: twang */
                f.s.w = 126, f.s.h = 132, f.cross = sc.let_t > 0.5f ? 0.55f : 0, f.tremble = 0.3f;
            }
            break;
        case PIP_OVERWHELMED: /* the eyes run apart, swap sizes, and the longer it goes the more overloaded: the pupils swirl */
            f.cross = fmodf(t, 0.4f) < 0.2f ? 0.8f : -0.8f;
            f.size_asym = fmodf(t, 0.5f) < 0.25f ? 0.18f : -0.18f;
            f.tremble = 1 + fminf(2, t);
            if (t > 1.6f) f.s.spiral = fminf(0.4f, (t - 1.6f) * 0.3f);
            break;
        case PIP_PINCHED: /* the eyes go in where the fingers push them, crossed; let go, they pop apart and wobble, he blinks */
            if (mt.kind == MT_PINCH) fingers_pull(pull, 0.6f); /* together, not one over the other */
            else if (sc.let_t > 1.6f) spread = -0.12f; /* pinned, no fingers: squeezed a little */
            else {
                if (sc.let_t < dt * 1.5f) s.squash.v -= 5;
                pk = 320, pz = 0.14f; /* pop */
                f.s.w = 124, f.s.h = 136, f.cross = 0, f.tremble = 0, f.blink = 4;
            }
            break;
        case PIP_BEAMING: /* sparkles burst from both eyes, three hops, the shine pulsing */
            if (!sc.burst) {
                sc.burst = true;
                for (int i = 0; i < 12; i++) {
                    float a = i * 0.5236f;
                    int e = i % 2;
                    dot(eye_cx(e) + cosf(a) * 60, eye_cy(e) + sinf(a) * 60, cosf(a) * frand(170, 260), sinf(a) * frand(170, 260) - 120,
                        frand(0.6f, 0.9f), i % 3 ? 9 : 13, i % 2 ? 0xFFE27A : 0xFFFFFF);
                }
            }
            if (t < 1.0f && (sc.beat -= dt) <= 0) sc.beat = 0.33f, s.squash.v -= 3.5f;
            f.s.glint = 0.2f + 0.15f * (0.5f + 0.5f * sinf(t * 9)); /* a glint on a big pupil is what costs: small */
            break;
        case PIP_CUDDLY: { /* rubbing his head on your hand, side to side, and melting: eyes closing, sinking, a deeper blush */
            float k = fminf(1, t / 2.5f);
            f.head_roll = 0.26f * sinf(t * 1.7f);
            f.s.lid = 0.3f + 0.25f * k, f.s.smile = 0.35f + 0.4f * k, f.blush = 0.5f + 0.3f * k, sink = 18 * k;
            break;
        }
        case PIP_EAGER: /* hopping on the spot, pupils growing, his mouth watering: a drop runs down */
            f.s.pw = .32f + .05f * fminf(1, t), f.s.ph = .36f + .05f * fminf(1, t); /* no glint: on a growing pupil it costs */
            if ((sc.beat -= dt) <= 0) sc.beat = 0.3f, s.squash.v -= 2.4f;
            if (t > 0.5f && (sc.puff -= dt) <= 0) {
                part_t *p = free_part(sweat, N_SWEAT);
                sc.puff = 1.0f;
                if (p) launch(p, SCR / 2 + frand(-12, 12), (eye_cy(0) + eye_cy(1)) / 2 + 80, 0, 20, 1.1f);
            }
            break;
        case PIP_YUMMY: /* chewing: the eyes squeeze and stretch on every bite, crumbs fall; at the end a wink */
            if (t < 1.8f && (sc.beat -= dt) <= 0) {
                sc.beat = 0.22f, sc.k ^= 1;
                s.squash.v += sc.k ? 3 : -3;
                if (sc.k) dot(SCR / 2 + frand(-30, 30), MOUTH_Y - 20, frand(-60, 60), -frand(30, 90), 0.7f, 5, 0xC98B45);
            }
            if (t > 1.8f) f.wink = 0.5f;
            break;
        case PIP_TRIUMPHANT: /* a victory spin: his head goes right round (the eyes ride round it); then a cocky squint and a
                                little victory dance, side to side on the beat, and a wink to finish */
            if (t < 0.9f) {
                float u = t / 0.9f;
                sc.spin = 6.2832f * u * u * (3 - 2 * u), f.blink = 0.01f; /* eased: wind up, whirl, settle */
            } else {
                sc.spin = 0;
                f.s.lid = 0.3f, f.s.h = 116, f.s.smile = 0.42f, f.gy = -0.5f;
                if (t < 2.3f && (sc.beat -= dt) <= 0) sc.beat = 0.3f, sc.k ^= 1, s.squash.v -= 3.5f, s.head_roll.v += sc.k ? 1.6f : -1.6f;
                f.head_roll = sc.k ? 0.18f : -0.18f;
                if (t > 2.3f) f.wink = 0.6f;
            }
            break;
        case PIP_DEFLATED: { /* the air goes out of him: shrinking and sinking, puffs leaving his eyes; then flat, a weak sigh */
            float k = fminf(1, t / 1.6f);
            scale = 1 - 0.38f * k, sink = 48 * k, stiff = 60;
            if (k < 1 && (sc.puff -= dt) <= 0) {
                sc.puff = 0.07f, sc.k ^= 1;
                float side = sc.k ? 1 : -1;
                dot(eye_cx(sc.k) + side * 50, eye_cy(sc.k) - 20, side * frand(150, 230), -frand(40, 140), 0.45f, 5, 0xE8E8F0);
            }
            if (k >= 1 && (sc.beat -= dt) <= 0) sc.beat = 1.1f, s.squash.v += 1.5f;
            break;
        }
        case PIP_SPARKLING: /* twinkles come and go round his eyes, the shine turning */
            if ((sc.puff -= dt) <= 0) {
                sc.puff = 0.11f;
                int e = frand(0, 1) < 0.5f ? 0 : 1;
                float a = frand(0, 6.283f), r = frand(85, 125);
                dot(eye_cx(e) + cosf(a) * r, eye_cy(e) + sinf(a) * r * 0.9f, 0, -frand(60, 120), 0.5f, (int)frand(6, 11),
                    frand(0, 1) < 0.5f ? 0xFFFFFF : 0xBFE9FF);
            }
            f.s.glint = 0.35f + 0.12f * sinf(t * 5);
            break;
        case PIP_GROGGY: { /* the lids seesaw, one eye then the other; he nods off, sinking, and jerks awake */
            float p = fmodf(t, 2.6f);
            f.lid_asym = 0.42f * sinf(t * 1.4f);
            f.s.lid = 0.5f + 0.2f * fminf(1, p / 2.2f);
            sink = p < 2.2f ? 30 * p / 2.2f : 0, stiff = p < 2.2f ? 40 : 300;
            if (p < 2.2f && p + dt >= 2.2f) s.squash.v -= 5, blink(); /* caught himself */
            break;
        }
        }
        m = &f;
    }
    show.live = show.out = show.gaze = false;
    if (mood >= PIP_STARSTRUCK && m == &moods[mood]) { /* one of the scenes with pictures */
        scene_out_t o = {0, 0, 1, 160, 0.8f, pull};
        f = *m;
        show.live = true;
        if (t == 0) memset(show.next, 0, sizeof show.next);
        sprite_unpin();
        if (mood >= PIP_XD) sc.spin = 0; /* the new ones spin only when they say so */
        show_face(mood, t, dt, &f, &o);
        spread = o.spread, sink = o.sink, scale = o.scale, stiff = o.stiff, zeta = o.zeta;
        m = &f;
    }
    for (float left = dt; left > 0; left -= 1.0f / 120) {
        float h = fminf(left, 1.0f / 120);
        spring(&sc.spread, spread, stiff, zeta, h);
        spring(&sc.sink, sink, stiff, 0.8f, h);
        spring(&sc.scale, scale, stiff, 0.8f, h);
        for (int e = 0; e < 2; e++) spring(&sc.ox[e], pull[e][0], pk, pz, h), spring(&sc.oy[e], pull[e][1], pk, pz, h);
    }
    return m;
}
/* where the scene's eyes look: overwhelmed from finger to finger, alert scanning the room */
static void scene_gaze(int mood, float dt)
{
    if (mood == PIP_OVERWHELMED && mt.kind == MT_PAIR && mt.n > 0) {
        if ((sc.look -= dt) <= 0) sc.look = 0.3f, sc.k = (sc.k + 1) % mt.n;
        int k = sc.k < mt.n ? sc.k : 0;
        s.tgx = (mt.x[k] - SCR / 2) / 160.0f, s.tgy = (mt.y[k] - SCR / 2) / 160.0f;
    } else if (show.gaze) s.tgx = show.gx, s.tgy = show.gy;
    else if ((mood == PIP_STRETCHED && mt.kind == MT_STRETCH) || (mood == PIP_PINCHED && mt.kind == MT_PINCH)) s.tgx = 0, s.tgy = 0;
}

/* s.t is a float: past ~5 days (2^19 s) a frame's dt is under half its step and it stops moving, and every window
   and cooldown measured on it with it. So it is kept under a few hours: an hour off it and off every moment kept on its
   scale at once (differences stay the same), while the screen is dark so no sway jumps, or anyway past ten hours */
static void clock_rebase(void)
{
    const float H = 3600;
    if (s.t < 2 * H || (!s.dark && s.t < 10 * H)) return;
    s.t -= H;
    float *at[] = {&s.last_voice, &s.last_bang, &s.hi_at, &s.flinch_at, &s.upside_at, &s.jealous_at, &s.tease_t, &tp.last,
                   &tp.first, &sw.last, &peek_at, &chase_at, &spiral_at, &s.input_at, &s.react_at, &s.babble_at, &snd.at, &rq.at,
                   &s.shook_at, &s.shown_at, &s.bridge_at, &s.last_sneeze, &s.boop_at, &s.tease_at, &s.zone_paid, &ninja_at,
                   &strong_at, &mt.palm_at, &game_kept.at, &rk.last}; /* every moment on s.t's scale: one left out reads as "just now" for up to an hour */
    for (unsigned i = 0; i < sizeof at / sizeof *at; i++) *at[i] -= H;
    for (int i = 0; i < 8; i++) s.notes[i] -= H, cab.plugs[i] -= H; /* never set (0): still <= 0, still "none" */
    for (int i = 0; i < TUMBLE_N; i++) tb.at[i] -= H;
    for (int i = 0; i < MEM_N; i++) mem[i].t -= H;
    for (int i = 0; i < 4; i++) spoke[i].t -= H;
}

static void update(float dt)
{
    static bool birth;
    if (s.hatch < 0 && birth && (s.dead || unmet())) cosmos_set(-1), voice_pad(NULL, 0, 0, 0.3f); /* cut short */
    birth = s.hatch >= 0;
    power(dt);
    if (s.phew_due && !away && !s.dark && !s.pet.asleep) s.phew_due = false, react(PIP_RELIEVED, 2.0f, "phew");
    jealous_step(dt);
    /* reactions */
    s.annoy = fmaxf(0, s.annoy - dt * 0.4f);
    s.finger_path *= expf(-dt / 0.25f);
    for (int i = 0; i < 2; i++) {
        /* a fixed curve, not a spring: a stiff spring at the board's frame time overshot every frame and the eye shook */
        float t = s.squint_t[i];
        s.squint[i] = t < 0 ? 0 : t < SQUINT_IN ? t / SQUINT_IN : t < SQUINT_IN + SQUINT_HOLD ? 1
                    : fmaxf(0, 1 - (t - SQUINT_IN - SQUINT_HOLD) / SQUINT_OUT);
        if (t >= 0 && (s.squint_t[i] += dt) > SQUINT_IN + SQUINT_HOLD + SQUINT_OUT) s.squint_t[i] = -1;
        s.squint[i] = fmaxf(s.squint[i], s.wind * WIND_SQUINT); /* the wind in his eyes: held while the breath lasts */
    }
    s.shakes = fmaxf(0, s.shakes - dt / 20); /* three shakes within about a minute */
    tb.much = fmaxf(0, tb.much - dt / 40); /* tumbles fade: one in 40 s */
    if (s.petting && s.touching && (s.pet_recent -= dt) > 0) {
        if (tp.rh != RH_OFF) rh_stop(); /* stroked mid-rhythm: that game is over (both kept their faces up every frame) */
        s.pet_total += dt;
        int up = -1;
        bool made_up = false; /* coaxed first: the stroke that brings him round also makes up a hurt aftertaste */
        if ((s.stroke_t += dt) > 0.5f)
            s.stroke_t = 0, made_up = pet_coax(&s.pet, s.pet_total), pet_event(&s.pet, PET_STROKE), up = pet_rouse(&s.pet, s.tz, PET_STROKE);
        bool making_up = s.react_mood >= 0 && !strcmp(s.react_name, "made up"); /* let him enjoy it */
        bool perking = s.react_mood >= 0 && !strcmp(s.react_name, "perked up"); /* let him open his eyes first */
        /* rubbed fast in one small patch, not stroked across: tickled, and he laughs */
        s.tickle_t = s.tk_spread < TICKLE_SPREAD ? s.tickle_t + dt : 0;
        if (s.tickle_t > TICKLE_S && s.tickle_t - dt <= TICKLE_S) /* it starts: the reaction laughs once; the next "ha" in a moment */
            pet_event(&s.pet, PET_TICKLE), s.laugh_voice = 1.2f, s.laugh_beat = 0;
        if (s.tickle_t > 5 && s.tickle_t - dt <= 5 && pet_tune.vary > 0 && hic_rand(0, 1) < 0.3f) pip_hiccups(1.5f); /* laughed himself into hiccups */
        if (made_up) react(PIP_TOUCHED, 3.0f, "made up"), s.squash.v -= 4, blink(); /* won round: he melts */
        else if (up >= 0) react(up, 1.5f, "perked up");                             /* stroked out of his drowsiness */
        else if (perking || making_up) {}
        else if (pet_cold(&s.pet)) { /* still cross deep down: at first he puts up with it; kept at it, he comes round by
                                        degrees (pet_coax wears the grudge down): pouty, then shy, then he melts */
            float g = s.pet.grudge;
            if (s.pet_total < COAX_NOTICE_S) react(g > 0.65f ? PIP_ANNOYED : g > 0.35f ? PIP_POUTY : PIP_SKEPTICAL, 1.2f, "still cross");
            else react(g > 0.5f ? PIP_POUTY : PIP_SHY, 1.2f, "softening");
        }
        else if (s.tickle_t > TICKLE_S) { /* tickled, wherever (a cheek or the chin too: fast in a small patch is a tickle, not a rub) */
            if (s.tickle_mood < 0) s.tickle_mood = vary_to(PIP_LAUGHING, PIP_XD, 0.45f); /* the real laugh, or "> <" */
            if (s.tickle_t > 4 && s.tickle_t - dt <= 4 && vary_to(0, 1, 0.5f)) s.tickle_mood = PIP_ROFL; /* kept at it: rolling about */
            if (s.tickle_mood != PIP_LAUGHING) react(s.tickle_mood, s.tickle_mood == PIP_XD ? 2.3f : 2.0f, show_name(s.tickle_mood));
            else {
                react(PIP_LAUGHING, 1.0f, "tickle");
                laugh_step(dt);
            }
        }
        else if (zone_at(s.fx, s.fy) == Z_CHIN) { /* under his chin (stroked: a tickle there is the branch above) */
            if (s.chin_mood < 0) s.chin_mood = s.pet.love > 0.5f ? vary_to(PIP_BLISSFUL, PIP_KITTY, 0.35f) : PIP_BLISSFUL;
            if (s.chin_mood == PIP_KITTY) react(PIP_KITTY, 1.5f, "purrs");
            else react(s.pet_total > TOUCHED_S && s.pet.love > 0.6f ? PIP_TOUCHED : PIP_BLISSFUL, 1.0f, "chin rub");
            s.lean_pitch += (-0.14f - s.lean_pitch) * fminf(1, dt * 4);
        }
        else if (zone_at(s.fx, s.fy) == Z_CHEEK_L || zone_at(s.fx, s.fy) == Z_CHEEK_R) { /* a cheek: he nestles into the hand */
            if (s.cheek_mood < 0) {
                static int last;
                const answer_t a[] = {{PIP_BLISSFUL, 2, "cheek rub", 1.0f}, {PIP_FLUSTERED, s.pet.love > 0.5f ? 1.2f : 0.3f, "cheek rub", 1.0f},
                                      {PIP_CONTENT, 1, "cheek rub", 1.0f}, {PIP_SHY, 0.6f, "cheek rub", 1.0f}};
                s.cheek_mood = a[pick(a, 4, &last)].mood;
            }
            react(s.pet_total > TOUCHED_S && s.pet.love > 0.6f ? PIP_TOUCHED : s.cheek_mood, 1.0f, "cheek rub");
            s.lean_roll += ((s.fx - SCR / 2) / (SCR / 2) * 0.3f - s.lean_roll) * fminf(1, dt * 4);
        }
        else if (s.fy < HEAD_Y) { /* along the top of his head: he screws his eyes up and leans into the hand */
            if (s.pat_mood < 0) {
                static int last;
                const answer_t a[] = {{PIP_CONTENT, 3, "head pat", 1.0f}, {PIP_DREAMY, 1.2f, "head pat", 1.0f},
                                      {PIP_SHY, s.pet.love > 0.6f ? 1.2f : 0.2f, "head pat", 1.0f}, {PIP_COMFY, s.pet.energy < 0.5f ? 1.2f : 0.3f, "head pat", 1.0f},
                                      {PIP_KITTY, s.pet.love > 0.5f ? 1.5f : 0.3f, "purrs", 1.0f},
                                      {PIP_PAWS, pet_tune.vary > 0 && (s.shown == PIP_SHY || s.shown == PIP_FLUSTERED) ? 4 : 0, "hides in paws", 1.0f}};
                s.pat_mood = a[pick(a, 6, &last)].mood;
            }
            if (s.pat_mood == PIP_KITTY) react(PIP_KITTY, 1.5f, "purrs"); /* a cat does not let go of a hand */
            else if (s.pat_mood == PIP_PAWS) show_react(PIP_PAWS); /* praised while shy: he hides in his paws */
            else react(s.pet_total > TOUCHED_S && s.pet.love > 0.6f ? PIP_TOUCHED : s.pat_mood, 1.0f, "head pat");
            s.lean_roll += ((s.fx - SCR / 2) / (SCR / 2) * 0.25f - s.lean_roll) * fminf(1, dt * 4);
            s.lean_pitch += (-0.08f - s.lean_pitch) * fminf(1, dt * 4); /* up into the hand, a little */
        }
        else if (s.pet_total > TOUCHED_S && s.pet.love > 0.6f) { /* stroked and stroked: moved, or he melts, or a kiss for it */
            if (s.long_mood < 0) {
                static int last;
                const answer_t a[] = {{PIP_TOUCHED, 3, "touched", 2.5f}, {PIP_MELTING, 2, "melts", 2.5f}, {PIP_KISS, s.pet.love > 0.7f ? 1.8f : 0.5f, "kisses", 2.5f},
                                      {PIP_HEARTEYES, s.pet.love > 0.8f ? 1.5f : 0.3f, "heart eyes", 2.5f},
                                      {PIP_BLOOMING, pet_valence(&s.pet) > 0.4f ? 1 : 0.1f, "blooms", 2.5f}};
                s.long_mood = a[pick(a, 5, &last)].mood;
            }
            if (s.long_mood >= PIP_XD) react(s.long_mood, 2.5f, show_name(s.long_mood));
            else react(s.long_mood, 2.5f, s.long_mood == PIP_MELTING ? "melts" : s.long_mood == PIP_KISS ? "kisses" : "touched");
        }
        else if (s.pet_total > 2.5f) react(PIP_LOVE, 2.5f, "love");
        else if (s.react_mood != PIP_LOVE) {
            if (s.stroke_mood < 0) {
                static int last;
                float l = s.pet.love;
                const answer_t a[] = {{PIP_HAPPY, 3, "pet", 1.0f}, {PIP_CONTENT, 1.4f, "pet", 1.0f}, {PIP_SHY, l > 0.6f ? 1.2f : 0.2f, "pet", 1.0f},
                                      {PIP_COMFY, s.pet.energy < 0.5f ? 1.2f : 0.2f, "pet", 1.0f}, {PIP_DREAMY, 0.7f, "pet", 1.0f},
                                      {PIP_LOVE, l > 0.7f ? 0.8f : 0.1f, "pet", 1.0f},
                                      {PIP_BEAMING, 1.3f, "pet", 1.0f}, {PIP_CUDDLY, l > 0.5f ? 1.6f : 0.4f, "pet", 1.0f},
                                      {PIP_BOUNCY, pet_valence(&s.pet) > 0.4f ? 0.8f : 0.1f, "bounces", 1.0f}};
                s.stroke_mood = a[pick(a, 9, &last)].mood;
            }
            if (s.stroke_mood == PIP_BOUNCY) react(PIP_BOUNCY, 2.4f, "bounces");
            else react(s.stroke_mood, 1.0f, "pet");
        }
        /* one giggle as the stroking starts (the "pet" reaction says it), not a giggle a second */
    } else if (s.touching && !s.petting && !mt.seen && !s.pet.asleep && s.sneeze_anim < 0 && s.burp_anim < 0) {
        /* a finger on him that is not stroking: resting still (warm, he goes dreamy) or moving slowly (he follows it,
           intent); kept moving about for pet_tune.chase_s it is a game: he hops after it and pounces when it stops */
        float held = lv_tick_elaps(s.press_tick) / 1000.0f, speed = s.finger_path * 4;
        float off = hypotf(s.fx - s.press_x, s.fy - s.press_y);
        if (sp.on) s.chase_t = 0; /* a spiral drawn on him: no game of chase */
        else s.chase_t = speed > 70 ? s.chase_t + dt : fmaxf(0, s.chase_t - dt * 0.5f); /* a pause does not end it at once */
        if (!s.chasing && s.chase_t > pet_tune.chase_s && pip_wants_play()) {
            s.chasing = true, s.pounce_t = 0;
            if (!s.chased) { /* one game's worth, however long it goes on; lifting the finger and chasing again pays once in CHASE_GAP_S */
                s.chased = true;
                if (s.t - chase_at >= CHASE_GAP_S) chase_at = s.t, pet_event(&s.pet, PET_CHASED);
            }
        }
        if (sp.on) { if (speed > 40) react(sp.mood, 1.0f, sp.name); } /* his answer holds while the finger goes on */
        else if (s.chasing) chase_step(dt, speed);
        else if (s.press_eye >= 0 && pet_tune.zones > 0 && held > EYE_HOLD_S && off < 30) eye_held(dt, held); /* on an eye: not warm, a bother */
        else if (held > REST_S && off < 30 && speed < 60) { /* held on long, now and then he drifts off into it altogether */
            if (s.rest_mood < 0 && held > REST_S + 2) s.rest_mood = vary_to(PIP_DREAMY, PIP_ZEN, 0.4f);
            if (s.rest_mood == PIP_ZEN) react(PIP_ZEN, 2.0f, "zen");
            else react(PIP_DREAMY, 0.6f, "warm finger");
        }
        else if (held > TRACK_S && s.path > 150 && speed > 40) react(PIP_FOCUSED, 0.6f, "tracking");
    }
    /* the mic hears his own voice from the speaker: nothing it picks up then (or just after) is someone else's */
    static float hush;
    hush = voice_busy() ? 0 : hush + dt;
    bool ears = hush > 0.4f;
    if (!ears) s.bang_wait = 0;
    /* shaken (and a second after) the mic hears the case rattle: no breath, no bang (a wild shake came out a "breeze") */
    bool rattled = s.t - s.shook_at < 1.0f;
    bool breath = mic_blowing() && HEARS(PIP_F_BLOW) && !s.pet.asleep && !rattled; /* someone blows on him: heard over his own voice once begun */
    blow_step(dt, breath && (ears || s.blowing));
    if (!mic_on()) s.mic_t = 0; /* switched off and on again: the room's level is learnt anew */
    float peak = mic_on() ? mic_peak_db() : 0; /* the loudest block since the last frame: a knock is over in 50 ms */
    if (mic_on() && HEARS(PIP_F_NOISE)) { /* a bang: far over the room's level, all at once, and gone again */
        float db = mic_db();
        if ((s.mic_t += dt) < 2) s.room_db = db; /* the meter settles first */
        if (s.bang_wait > 0) { /* held back a moment: a breath on the mic, or a noise come on, start the same way */
            s.bang_db = fmaxf(s.bang_db, peak);
            if ((s.bang_wait -= dt) <= 0 && !mic_blowing() && db < s.bang_db - BANG_GONE && !s.pet.asleep && !away) {
                int h = pet_hour(s.pet.last, s.tz);
                if (s.bang_db < pet_tune.shock_db && s.pet.trust > 0.6f && vary_to(0, 1, 0.35f)) show_react(PIP_POPEYED); /* bold: not frightened, amazed */
                else {
                    pet_event(&s.pet, PET_FRIGHT);
                    if ((h >= 19 || h < 6) && vary_to(0, 1, 0.4f)) show_react(PIP_SPOOKED); /* in the dark: a ghost! */
                    else react(s.bang_db >= pet_tune.shock_db ? PIP_SHOCKED : PIP_AFRAID, 2.0f, "bang");
                }
            }
        } else if (ears && !rattled && s.mic_t >= 2 && peak >= pet_tune.loud_db && peak - s.room_db >= pet_tune.loud_jump && s.t - s.last_bang > 3 && !mic_blowing() &&
                   mic_music() <= 0) /* a song's drums are no bang */
            s.bang_wait = 0.45f, s.bang_db = peak, s.last_bang = s.t;
        s.room_db += (db - s.room_db) * (1 - expf(-dt / 4)); /* a loud room becomes the room */
    }
    s.greet_t -= dt;
    if (s.dark_in > 0 && (s.dark_in -= dt) <= 0) s.dark = true;
    /* his voice's budget comes back; a reaction held back gets its turn (stale after 1.5 s) */
    if (pet_tune.snd_refill > 0) snd.tokens = fminf(2.5f, snd.tokens + dt / pet_tune.snd_refill);
    if (rq.mood >= 0) {
        if (s.t - rq.at > 1.5f) rq.mood = -1;
        else if (s.react_mood < 0 || s.react_left <= 0 || s.t - s.react_at >= pet_tune.hold) {
            int m = rq.mood;
            rq.mood = -1, rq.force = true;
            react(m, rq.secs, rq.name);
            rq.force = false;
        }
    }
    if (s.react_mood >= 0 && (s.react_left -= dt) <= 0) {
        int told = s.react_mem; /* what it left behind: maybe a word about it */
        s.react_mood = -1, s.react_mem = -1;
        tell_after(told);
        if (s.meet_pending) {
            s.meet_pending = false;
            static const char *const said[PIP_MOOD_COUNT] = {[PIP_LOVE] = "likes you", [PIP_SHY] = "shy", [PIP_SUSPICIOUS] = "wary",
                                                             [PIP_ANGRY] = "dislikes you", [PIP_BELLIGERENT] = "dislikes you",
                                                             [PIP_SKEPTICAL] = "not in the mood"};
            int r = pet_meet(&s.pet, (int)lv_rand(0, 99), meet_from);
            if (r == PIP_LOVE && vary_to(0, 1, 0.3f)) show_react(PIP_HELLO);
            else react(r, 3.0f, said[r]);
        } else if (tp.phew && !strcmp(s.react_name, "head spin")) react(PIP_RELIEVED, 1.6f, "phew");
        tp.phew = false;
    }
    if (s.tell_in > 0) { /* a tale due: once nothing else is going on (a few seconds at most, then it is forgotten) */
        s.tell_until -= dt;
        if ((s.tell_in -= dt) <= 0) {
            bool open = s.react_mood < 0 && !s.touching && !away && !s.dark && !covered && !s.pet.asleep && !unmet() && !s.washing &&
                        s.sneeze_anim < 0 && s.burp_anim < 0 && s.boop_anim < 0 && s.intro < 0 && s.hatch < 0 && tp.n == 0;
            s.tell_in = open ? (tell(s.tell_mem), 0) : s.tell_until > 0 ? 0.1f : 0;
        }
    }
    taps_step(dt);
    dance_step(dt);
    bday_step(dt);
    if (s.washing) wash_step(dt);
    life_step(dt);
    if (s.was_asleep && !s.pet.asleep && !unmet()) good_morning();
    s.was_asleep = s.pet.asleep;
    int mood = s.react_mood >= 0 ? s.react_mood : calm_base(s.mood);
    if (unmet()) mood = PIP_ASLEEP; /* dormant until the first button */
    if (!s.pet.asleep) s.sweet = 0;
    else if (mood == PIP_ASLEEP && !unmet()) { /* fell asleep fed, clean and loved: more often than not, sweetly */
        if (!s.sweet) s.sweet = s.pet.food > 0.4f && s.pet.clean > 0.4f && s.pet.love > 0.5f && vary_to(0, 1, 0.6f) ? 1 : 2;
        if (s.sweet == 1) mood = PIP_SWEETDREAM;
    }
    affect_step(mood, dt);
    if (mood != s.shown) {
        s.shown_at = s.t;
        /* his own mood swings get a comment now and then, not every time */
        if (s.react_mood < 0 && s.t - s.last_voice > 15 && lv_rand(0, 100) < 35) speak(mood_sound(mood), 1, mood);
        s.shown = mood;
        s.next_look = 0;
        s.squash.v -= 3.0f; /* anticipation: squash first, the spring overshoots into a stretch */
    }
    const mood_def_t *m = &moods[mood];
    /* waking up after power-on: out of black, sleepy and lopsided, a shake, then off he goes */
    static mood_def_t wake;
    if (s.intro >= 0) {
        float t = s.intro += dt;
        wake = moods[PIP_SLEEPY];
        wake.slow = 0.8f, wake.blink = 0.01f, wake.gy = 0.3f;
        if (t < 0.8f) wake.closed = 1;                                        /* still dark */
        else if (t < 3.0f) wake.closed = 1 - 0.6f * fminf(1, (t - 0.8f) / 1.6f), wake.lid_asym = 0.32f; /* one eye opens more */
        else if (t < 3.8f) {                                                  /* a shake to wake up */
            wake.closed = 0.25f, wake.lid_asym = 0.1f, wake.tremble = 3;
        } else {
            s.intro = -1;
            if (woke_drained && !unmet() && !s.dead) { /* he ran flat last time: angry about it */
                woke_drained = false, pet_dirty = true;
                int r = pet_event(&s.pet, PET_WOKE_DRAINED);
                react(r >= 0 ? r : PIP_ANGRY, 3.5f, "drained");
            } else react(PIP_CHEEKY, 3.0f, "awake");
        }
        if (t >= 0.8f && t - dt < 0.8f) say(VOICE_YAWN);
        if (t >= 3.0f && t - dt < 3.0f) s.squash.v -= 4, say(VOICE_DIZZY);
        if (s.intro >= 0) m = &wake;
    }
    static mood_def_t first;
    if (unmet()) s.intro = -1, s.open.x = s.open.v = 0, m = &moods[PIP_ASLEEP]; /* dormant: eyes shut from the first frame */
    else if (s.hatch >= 0) {
        /* his birth (cosmos.c: the sky, the spark, the galaxy, the bang), then his first waking: the shut eyes, gold with the
           light still, stir, open bit by bit (one first), pop open at the world; stars in them; a butterfly comes and sits on
           his nose; then he sees you, and falls for you, and there are fireworks */
        float t = s.hatch += dt, t0 = t - dt;
#define AT(x) (t0 < (x) && t >= (x))
        hatch_fx(t, t0);
        bool scene = false;
        if (t < HATCH_E(1.5f)) first = moods[PIP_ASLEEP], first.tremble = t > HATCH_EYES ? 0.5f : 0;
        else if (t < HATCH_E(3.6f)) first = moods[PIP_SLEEPY], first.slow = 0.85f, first.lid_asym = 0.35f, first.closed = 1 - 0.65f * fminf(1, (t - HATCH_E(1.5f)) / 1.8f);
        else if (t < HATCH_E(4.7f)) first = moods[PIP_SURPRISED];
        else if (t < HATCH_E(6.6f)) mood = PIP_STARSTRUCK, scene = true;                     /* the world! stars in his eyes */
        else if (t < HATCH_E(10.4f)) {                                                       /* a butterfly, on his nose */
            mood = PIP_BUTTERFLY, scene = true;
            if (sc.mood != PIP_BUTTERFLY) sc.mood = PIP_BUTTERFLY, sc.t = 1.3f;
        }
        else if (t < HATCH_E(12.0f)) first = moods[PIP_ADORING], first.s.glint = .6f, first.blush = 0.3f * fminf(1, (t - HATCH_E(10.4f)) / 1.2f); /* and then: you */
        else if (t < HATCH_END) first = moods[PIP_HAPPY], first.s.smile = .55f;
        else s.hatch = -1, cosmos_set(-1), react(PIP_LOVE, 3.4f, "hello"), say(VOICE_LOVE);
        float gold = 1 - fminf(1, fmaxf(0, (t - HATCH_E(0.3f)) / 3.0f));
        if (!scene) first.tint = (uint32_t)0xFF << 16 | (uint32_t)(0xFF - 0x2A * gold) << 8 | (uint32_t)(0xFF - 0x8C * gold);
        first.blink = 0.01f, first.look_rate = 0.01f;
        if (t < HATCH_E(3.6f)) first.head_roll = 0, first.gy = 0, s.tgx = s.tgy = 0, s.idle_roll = 0, s.next_look = 1; /* straight before you */
        if (t >= HATCH_E(12.0f)) s.tgx = 0.25f * sinf(t * 3), s.next_look = 1;
        if (AT(HATCH_E(0.5f)) || AT(HATCH_E(1.0f)) || AT(HATCH_E(1.4f))) s.squash.v -= 2.0f; /* little wriggles */
        if (AT(HATCH_E(2.5f))) say(VOICE_YAWN);
        if (AT(HATCH_E(3.65f))) s.squash.v -= 4.5f, say(VOICE_OH);
        if (AT(HATCH_E(3.85f))) pip_glance(110, 225, 0.4f);
        if (AT(HATCH_E(4.25f))) pip_glance(356, 225, 0.4f);
        if (AT(HATCH_E(4.75f))) say(VOICE_WOW);
        if (AT(HATCH_E(5.6f))) say(VOICE_TWINKLE);
        if (AT(HATCH_E(6.8f))) say(VOICE_FLUTTER);
        if (AT(HATCH_E(8.1f))) say(VOICE_HUSHED);
        if (AT(HATCH_E(9.0f))) say(VOICE_GIGGLE);
        if (AT(HATCH_E(10.5f))) pip_glance(233, 233, 1.4f);
        if (AT(HATCH_E(10.8f))) say(VOICE_AWW);
        for (int k = 0; k < 10; k++) if (AT(HATCH_E(12.0f + 0.42f * k))) s.squash.v -= 5.0f; /* hop, hop */
        if (AT(HATCH_E(12.0f))) say(VOICE_ANTHEM);
        if (AT(HATCH_E(14.75f))) say(VOICE_TADA);
#undef AT
        if (s.hatch >= 0) m = scene ? &moods[mood] : &first;
    }
    if (s.sneeze_anim >= 0) {
        if (unmet() || s.pet.asleep || s.dark) s.sneeze_anim = -1; /* fell asleep, the screen went off: no sneeze */
        else m = sneeze_face(dt);
    }
    if (s.burp_anim >= 0) {
        if (unmet() || s.pet.asleep || s.dark) s.burp_anim = -1;
        else m = burp_face(dt);
    }
    if (toot.anim >= 0) {
        if (unmet() || s.pet.asleep || s.dark) toot.anim = -1;
        else m = toot_face(dt);
    }
    if (s.react_mood == PIP_LAUGHING && s.react_name && !strncmp(s.react_name, "tickle", 6)) { /* tickled: the real laugh */
        if (strcmp(s.react_name, "tickle")) s.laugh_k = fmaxf(0, s.laugh_k - dt * 0.6f); /* let go: calming down */
        m = laugh_face();
    }
    if (mood == PIP_EYEROLL) { if (s.eyeroll_t == 0) s.roll_dir = lv_rand(0, 1) ? 1 : -1; s.eyeroll_t += dt; }
    else s.eyeroll_t = 0;
    if (s.boop_anim >= 0) {
        if (unmet() || s.pet.asleep || s.dark) s.boop_anim = -1;
        else m = boop_face(dt, m);
    }
    m = scene_face(mood, dt, m); /* the newer faces' little scenes */
    bool awake = m->closed < 1 && s.intro < 0 && mood != PIP_SWEETDREAM;
    float rate = or1(m->look_rate), amp = or1(m->look_amp);

    {
        shape_t sh = mood_shape(m);
        float sq = fmaxf(s.squint[0], s.squint[1]);
        eyes_open = m->closed <= 0 && sh.lid + fmaxf(m->lid_asym, 0) < 0.2f && sh.smile < 0.6f && sq < 0.1f; /* not a scowl, a "^ ^", half-shut */
    }
    if (awake && (s.next_blink -= dt) <= 0) {
        blink();
        s.next_blink = lv_rand(0, 100) < 15 ? 0.3f : frand(1.8f, 5.0f) / or1(m->blink);
    }
    float shut = 0;
    if (s.blink_t >= 0) {
        const float close = 0.07f, open = 0.14f;
        shut = fmaxf(s.blink_t < close ? s.blink_t / close : 1.0f - (s.blink_t - close) / open, 0);
        if ((s.blink_t += dt) > close + open) s.blink_t = -1;
    }
    float wink = 0;
    if (awake && m->wink > 0 && (s.next_wink -= dt) <= 0) { s.wink_t = 0; s.next_wink = m->wink * frand(0.7f, 1.3f); }
    if (s.wink_t >= 0) {
        wink = fmaxf(0, s.wink_t < 0.08f ? s.wink_t / 0.08f : s.wink_t < 0.25f ? 1 : 1 - (s.wink_t - 0.25f) / 0.15f);
        if ((s.wink_t += dt) > 0.4f) s.wink_t = -1;
    }
    if ((mood == PIP_LOVE || mood == PIP_SMITTEN) && (s.beat -= dt) <= 0) { s.squash.v += 2.2f; s.beat = mood == PIP_SMITTEN ? 0.55f : 0.7f; } /* heartbeat */
    if (mood == PIP_EXCITED && (s.beat -= dt) <= 0) s.squash.v -= 3.2f, s.beat = 0.33f;
    if (s.talk_t > 0 && (s.talk_t -= dt) > 0 && (s.talk_beat -= dt) <= 0) /* talking: a bob a syllable */
        s.squash.v -= 1.3f, s.head_roll.v += frand(-0.3f, 0.3f), s.talk_beat = frand(0.1f, 0.16f);                                     /* hop, hop */
    if (mood == PIP_LAUGHING && (s.beat -= dt) <= 0) s.squash.v -= 1.8f, s.head_roll.v += frand(-1.2f, 1.2f), s.beat = 0.17f; /* shaking with it */
    if (mood == PIP_BERSERK && (s.beat -= dt) <= 0) /* beside himself: jolted about at random */
        s.squash.v += frand(-3.5f, 3.5f), s.head_roll.v += frand(-2.8f, 2.8f), s.beat = frand(0.07f, 0.2f);
    sneeze_step(dt, s.mood == PIP_ALLERGIC && s.auto_mood, awake && s.intro < 0 && s.hatch < 0 && !away);
    burp_step(dt, awake && s.intro < 0 && s.hatch < 0 && !away);
    hic_step(dt, awake && s.intro < 0 && s.hatch < 0 && !away);
    toot_step(dt, awake && s.intro < 0 && s.hatch < 0 && !away);

    if (s.touching && sp.on && mood == PIP_WHIRLED) { /* a spiral drawn on him: his pupils ride round where the finger is */
        float x, y;
        spiral_gaze(&x, &y);
        s.tgx = x * 0.9f, s.tgy = y * 0.7f;
        s.idle_roll = x * 0.15f;
    } else if (mood == PIP_WOOZY && !s.touching) { /* let go: still going round by himself, slower and slower */
        sp.wz_phase += sp.wz_w * dt, sp.wz_w *= expf(-dt / 1.6f);
        float r = 0.25f + 0.45f * fminf(1, fabsf(sp.wz_w) / 6);
        s.tgx = cosf(sp.wz_phase) * r, s.tgy = sinf(sp.wz_phase) * r * 0.75f;
        s.idle_roll = sinf(sp.wz_phase) * 0.2f;
    } else if (s.touching && s.dodged) { /* ducking the finger: eyes and head away from it */
        s.tgx = (s.fx < SCR / 2 ? 1 : -1) * 1.1f;
        s.tgy = -0.3f;
    } else if (s.touching && !s.petting) {
        float lead = s.chasing ? 0.22f : 0; /* chasing: a step ahead, where the finger is going */
        s.tgx = (s.fx + s.fvx * lead - SCR / 2) / 160.0f;
        s.tgy = (s.fy + s.fvy * lead - SCR / 2) / 160.0f;
    } else if (s.glance_left > 0 && awake) {
        s.glance_left -= dt;
        s.tgx = (s.glance_x - SCR / 2) / 160.0f;
        s.tgy = (s.glance_y - SCR / 2) / 160.0f;
        if (s.glance_left <= 0) s.next_look = frand(0.3f, 0.8f);
    } else if (mood == PIP_EYEROLL && awake) { /* rolling his eyes: from one side up over the top and down the other, then aside */
        float t = s.eyeroll_t;
        if (t < 0.9f) {
            float a = 3.1416f * t / 0.9f;
            s.tgx = -cosf(a) * 0.95f * s.roll_dir, s.tgy = -sinf(a) * 0.95f - 0.05f;
        } else s.tgx = 0.55f * s.roll_dir, s.tgy = 0.25f;
    } else if (mood == PIP_DIZZY) {
        s.tgx = cosf(s.t * 6) * 0.6f;
        s.tgy = sinf(s.t * 6) * 0.4f;
        s.idle_roll = sinf(s.t * 2.5f) * 0.25f;
    } else if ((s.next_look -= dt) <= 0) {
        bool centre = lv_rand(0, 100) < 25 || !awake;
        float nx = centre ? 0 : (lv_rand(0, 1) ? 1 : -1) * frand(0.4f, 1.1f) * amp, ny = centre ? 0 : frand(-0.6f, 0.6f) * amp;
        if (awake && fabsf(nx - s.tgx) > 0.9f && lv_rand(0, 100) < 50) blink(); /* big gaze shifts come with a blink */
        s.tgx = nx;
        s.tgy = ny;
        s.idle_roll = lv_rand(0, 100) < 25 ? frand(-0.12f, 0.12f) : 0; /* curious head tilt now and then */
        s.next_look = frand(0.5f, 2.2f) / rate;
    }
    if (awake && rate >= 0.2f && (s.next_micro -= dt) <= 0) {
        s.micro_x = frand(-0.08f, 0.08f);
        s.micro_y = frand(-0.06f, 0.06f);
        s.next_micro = frand(0.15f, 0.5f);
    }
    scene_gaze(mood, dt);
    s.tgx = fmaxf(-1.2f, fminf(1.2f, s.tgx));
    s.tgy = fmaxf(-1.0f, fminf(1.0f, s.tgy));

    for (float left = dt; left > 0; left -= 1.0f / 120) step(fminf(left, 1.0f / 120), m, (mood == PIP_DIZZY || mood == PIP_WOOZY) && s.intro < 0);
    /* black until he stirs: the power-on intro, and before (and just after the button of) the first meeting */
    lv_obj_set_flag(s.canvas, LV_OBJ_FLAG_HIDDEN, (s.intro >= 0 && s.intro < 0.8f) || unmet() || (s.hatch >= 0 && s.hatch < HATCH_EYES));
    if (s.intro >= 3.0f) { /* the shake: the head whips side to side, fading out */
        float k = 1 - (s.intro - 3.0f) / 0.8f;
        s.head_yaw.x = sinf(s.intro * 34) * 0.45f * k, s.head_roll.x = sinf(s.intro * 27) * 0.25f * k;
    }
    s.t += dt;
    clock_rebase();

    float cx = SCR / 2, cy = SCR / 2 + sinf(s.t * (awake ? 1.6f : 0.9f)) * (awake ? 2.5f : 5.0f);
    float swing = sinf(s.t * 13); /* yes / no: the head swings about twice a second, the pupils stay on you */
    float spin = sc.spin > 3.1416f ? sc.spin - 6.2832f : sc.spin; /* a victory spin, kept in -pi..pi */
    float hy = s.head_yaw.x + s.wag.x * swing + spin, hp = s.head_pitch.x + s.nod.x * swing, hr = s.head_roll.x;
    float open = fmaxf(s.open.x, 0) * (1 - shut), sq = s.squash.x;
    float trem = fmaxf(s.tremble.x, 0), jx = frand(-1, 1) * trem, jy = frand(-1, 1) * trem;
    for (int i = 0; i < 2; i++) {
        float side = i ? 1.0f : -1.0f;
        shape_t cur;
        for (int j = 0; j < SHAPE_N; j++) ((float *)&cur)[j] = s.shape[i][j].x;
        const shape_t *sh = &cur;
        eye_t *e = &s.face.eye[i];
        float phi = hy + side * SPREAD;
        float k = EYE_SCALE * (1 + 0.35f * (cosf(phi) - 0.8525f)); /* cos(SPREAD): the far eye shrinks */
        float x0 = HEAD_R * sinf(phi) * (1 + sc.spread.x), y0 = 240.0f * sinf(hp) + sc.sink.x; /* a scene draws them apart, sinks them */

        if (i) k *= 1 + m->size_asym;
        k *= fmaxf(0.3f, sc.scale.x);
        e->w = sh->w * k * (1 + 0.12f * (1 - open)) * (1 - 0.5f * sq);
        float sqz = s.squint[i];
        e->h = sh->h * k * open * (1 + sq) * (i ? 1 - 0.95f * wink : 1) * (1 - 0.55f * sqz);
        if (hic.pop > 0) e->h *= 1 + 0.16f * hic.pop / HIC_POP, e->w *= 1 + 0.05f * hic.pop / HIC_POP; /* "hic!": eyes popped wide */
        float px = sc.ox[i].x, py = sc.oy[i].x; /* drawn by a finger: out that way, and longer along it */
        if (px != 0 || py != 0) e->w *= 1 + fabsf(px) / 140 - fabsf(py) / 700, e->h *= 1 + fabsf(py) / 140 - fabsf(px) / 700;
        e->depth = sh->w * k * 0.5f; /* flatter discs foreshorten more when turned away */
        y0 += (sh->h * k - e->h) * 0.25f; /* closing settles toward the lower half */
        e->x = cx + x0 * cosf(hr) - y0 * sinf(hr) + jx + frand(-0.3f, 0.3f) * trem;
        e->y = cy + x0 * sinf(hr) + y0 * cosf(hr) + jy + frand(-0.3f, 0.3f) * trem;
        if (fabsf(px) + fabsf(py) > 0.5f) { /* drawn off by a finger: never past the round glass */
            e->x += px, e->y += py;
            float ex = e->x - SCR / 2, ey = e->y - SCR / 2, er = hypotf(ex, ey), lim = fmaxf(40, SCR / 2 - 6 - 0.5f * fmaxf(e->w, e->h));
            if (er > lim && er > 1) e->x = SCR / 2 + ex * lim / er, e->y = SCR / 2 + ey * lim / er;
        }
        e->yaw = hy + side * SPREAD * 0.5f;
        e->pitch = hp;
        e->roll = hr - side * sh->roll;
        e->taper = sh->taper;
        e->lid = fmaxf(sh->lid, 0.3f * sqz);
        e->tilt = -side * sh->tilt;
        e->smile = sh->smile;
        /* dizzy: each pupil circles on its own, in opposite directions */
        float dz = s.dizzy.x * 0.45f, ph = s.t * 9 * side;
        e->look_x = fmaxf(-0.85f, fminf(0.85f, s.look_x.x - e->yaw + cosf(ph) * dz - side * m->cross));
        e->look_y = fmaxf(-0.7f, fminf(0.7f, s.look_y.x - hp + sinf(ph) * dz));
        e->pw = sh->pw;
        e->ph = sh->ph;
        e->square = sh->square;
        /* the springs ring down toward 0 for minutes, never reaching it: a trace (under a hundredth: less than half a
           step of colour, nothing to see) would keep the renderer on its slow per-pixel path for the whole eye */
#define TRACE(v) ((v) < 0.01f ? 0 : (v))
        e->heart = TRACE(sh->heart);
        e->glint = TRACE(sh->glint);
        e->spiral = TRACE(sh->spiral);
        e->veins = TRACE(sh->veins);
        e->wet = TRACE(sh->wet);
#undef TRACE
        e->wobble = 0.022f * fminf(e->w, e->h) * 0.5f;
        e->wob_t = s.t + i * 7.3f;

    }

    eye_colors();
    s.face.blush = fmaxf(s.blush.x, 0);
    /* the screen is dark: no grawlix, no bobbing "?" - they would keep LVGL drawing for nobody */
    effects(s.dark ? FX_NONE : m->fx, dt);
    feed_step(dt);
    grime_dust(unmet() ? 0 : pet_dust(&s.pet)); /* dust on the glass, by how grubby he is */
    grime_step(dt);
    const char *text = s.dark ? NULL : m->text;

    /* text above the eyes: swearing shakes, a question mark bobs */
    if (text != s.text_shown) {
        s.text_shown = text;
        lv_label_set_text(s.text, text ? text : "");
        lv_obj_set_style_text_color(s.text, lv_color_hex(m->text_color), 0);
    }
    if (text) {
        float bob = mood == PIP_SWEARING ? 0 : sinf(s.t * 3) * 6;
        lv_obj_align(s.text, LV_ALIGN_TOP_MID, (int)(jx * 2), 48 + (int)(bob + jy * 2));
    }

    /* keep the eyes inside the round screen: they move as a pair, so the one that hits the rim stops both */
    for (int pass = 0; pass < (show.out ? 0 : 3); pass++) { /* (peeking hides under the edge) */
        float mx = 0, my = 0, worst = 0;
        for (int i = 0; i < 2; i++) {
            eye_t *e = &s.face.eye[i];
            float dx = e->x - SCR / 2, dy = e->y - SCR / 2, d = sqrtf(dx * dx + dy * dy) + 1e-3f;
            float ea = e->w * 0.5f * (1 + fabsf(e->taper)), eb = e->h * 0.5f;
            float over = d + sqrtf(ea * dx * ea * dx + eb * dy * eb * dy) / d - (SCR / 2 - 8);
            if (over > worst) { worst = over; mx = -dx / d * over; my = -dy / d * over; }
        }
        if (worst <= 0) break;
        for (int i = 0; i < 2; i++) s.face.eye[i].x += mx, s.face.eye[i].y += my;
    }
    show_after(mood, dt);
}

/* LVGL redraws where an eye or a cheek was and is, a box each: boxes far apart stay apart, so the black between
   the eyes is not copied to the screen again */
static void redraw(const face_rect_t *was, const face_rect_t *now)
{
    for (int i = 0; i < 4; i++) {
        face_rect_t a = was[i].x2 >= was[i].x1 ? was[i] : now[i], b = now[i].x2 >= now[i].x1 ? now[i] : a;
        if (a.x2 < a.x1) continue;
        lv_area_t r = {LV_MIN(a.x1, b.x1), LV_MIN(a.y1, b.y1), LV_MAX(a.x2, b.x2), LV_MAX(a.y2, b.y2)};
        lv_obj_invalidate_area(s.canvas, &r);
    }
}

/* how long frames take: smoothed, and the worst of each second with what was on then */
#define WORST_S 8
typedef struct { uint32_t sec; float ms; int mood; const char *reaction; } worst_t;
static worst_t worst[WORST_S];

/* ---------- developer mode: the wireframe and numbers under the skin, to show how he is made ---------- */
static struct {
    bool on;
    lv_obj_t *top, *bot;
    int frames;
    uint32_t since;
    float fps;
    char note[96];
} dev;
#define DEV_GREEN 0x34C759
#define DEV_PINK 0xFF2D95

/* The wireframe is plotted straight into the face's buffer, clipped to the boxes the renderer paints anyway (they are
   wiped with the next frame): no extra LVGL drawing, no extra area to refresh. */
static uint16_t *dev_px;
static face_rect_t dev_clip[4];
static uint16_t dev_ink(uint32_t rgb)
{
    uint16_t c = (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
    return s.face.swapped ? (uint16_t)(c << 8 | c >> 8) : c;
}

static void dev_dot(int x, int y, uint16_t c)
{
    for (int i = 0; i < 4; i++) {
        const face_rect_t *r = &dev_clip[i];
        if (x >= r->x1 && x <= r->x2 && y >= r->y1 && y <= r->y2) { dev_px[y * SCR + x] = c; return; }
    }
}

static void dev_line(float x1, float y1, float x2, float y2, uint16_t c, bool thick)
{
    int n = (int)fmaxf(fabsf(x2 - x1), fabsf(y2 - y1)) + 1;
    for (int k = 0; k <= n; k++) {
        int x = (int)(x1 + (x2 - x1) * k / n), y = (int)(y1 + (y2 - y1) * k / n);
        dev_dot(x, y, c);
        if (thick) dev_dot(x + 1, y, c), dev_dot(x, y + 1, c);
    }
}

static void dev_paint(uint16_t *px, const face_rect_t *clip)
{
    if (!dev.on) return;
    dev_px = px;
    memcpy(dev_clip, clip, sizeof dev_clip);
    uint16_t green = dev_ink(DEV_GREEN), white = dev_ink(0xFFFFFF), pink = dev_ink(DEV_PINK);
    for (int i = 0; i < 2; i++) {
        const eye_t *eye = &s.face.eye[i];
        float a = eye->w * 0.5f, b = eye->h * 0.5f, c = cosf(eye->roll), sn = sinf(eye->roll), px0 = 0, py0 = 0;
        for (int k = 0; k <= 32; k++) { /* the outline the shape springs toward */
            float t = k * 6.2831853f / 32, x = cosf(t) * a, y = sinf(t) * b;
            float X = eye->x + x * c - y * sn, Y = eye->y + x * sn + y * c;
            if (k) dev_line(px0, py0, X, Y, green, true);
            px0 = X, py0 = Y;
        }
        float ly = eye->y - b + eye->lid * eye->h; /* the lid */
        dev_line(eye->x - a, ly - eye->tilt * a, eye->x + a, ly + eye->tilt * a, green, true);
        float gx = eye->x + sinf(eye->look_x) * a, gy = eye->y + sinf(eye->look_y) * b; /* the gaze */
        dev_line(eye->x, eye->y, gx, gy, white, false);
        dev_line(gx - 8, gy, gx + 8, gy, white, false), dev_line(gx, gy - 8, gx, gy + 8, white, false);
    }
    for (int i = 0; i < 4; i++) { /* what the renderer repainted */
        const face_rect_t *r = &clip[i];
        if (r->x2 < r->x1) continue;
        dev_line(r->x1, r->y1, r->x2, r->y1, pink, false), dev_line(r->x2, r->y1, r->x2, r->y2, pink, false);
        dev_line(r->x2, r->y2, r->x1, r->y2, pink, false), dev_line(r->x1, r->y2, r->x1, r->y1, pink, false);
    }
}

static void dev_text(void)
{
    static const char *const why[] = {"сон", "карточка дня", "тело", "послевкусие", "нужда", "ночь", "занят", "палитра"};
    pet_why_t w;
    pet_mood_why(&s.pet, s.tz, &w);
    char t[160], b[200];
    snprintf(t, sizeof t, "DEV MODE\n%.0f к/с  кадр %.0f мс\n%s%s%s  <- %s", dev.fps, s.frame_ms, moods[s.shown].name,
             s.react_mood >= 0 ? " / " : "", s.react_mood >= 0 ? s.react_name : "", w.kind >= 0 && w.kind < 8 ? why[w.kind] : "?");
    snprintf(b, sizeof b, "наклон %+.1f %+.1f\nтряска %.1f  мик %.0f дБ  бат %d%%%s%s", s.imu.tilt_x, s.imu.tilt_y, s.imu.energy,
             mic_on() ? mic_db() : 0.0f, s.bat_pct, dev.note[0] ? "\n" : "", dev.note);
    lv_label_set_text(dev.top, t);
    lv_label_set_text(dev.bot, b);
}

static void dev_frame(void) /* each shown frame: count it, and redraw the wireframe where it was and is */
{
    if (!dev.on) return;
    dev.frames++;
    if (lv_tick_elaps(dev.since) >= 500) {
        dev.fps = dev.frames * 1000.0f / lv_tick_elaps(dev.since), dev.frames = 0, dev.since = lv_tick_get();
        dev_text();
    }
}

void pip_dev(bool on)
{
    dev.on = on;
    lv_obj_set_flag(dev.top, LV_OBJ_FLAG_HIDDEN, !on);
    lv_obj_set_flag(dev.bot, LV_OBJ_FLAG_HIDDEN, !on);
    dev.since = lv_tick_get(), dev.frames = 0;
    if (on) dev_text();
}
bool pip_dev_on(void) { return dev.on; }
void pip_dev_note(const char *line) { snprintf(dev.note, sizeof dev.note, "%s", line); }

static void dev_create(lv_obj_t *parent)
{
    for (int i = 0; i < 2; i++) {
        lv_obj_t *t = lv_label_create(parent);
        lv_obj_set_style_text_font(t, th->tiny, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(DEV_GREEN), 0);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(t, 330);
        lv_obj_align(t, i ? LV_ALIGN_BOTTOM_MID : LV_ALIGN_TOP_MID, 0, i ? -18 : 26);
        lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE);
        if (i) dev.bot = t;
        else dev.top = t;
    }
    pip_dev(false);
}

static struct { int n; float sum, max; } perf;
void pip_perf_take(int *frames, float *sum_ms, float *max_ms)
{
    *frames = perf.n, *sum_ms = perf.sum, *max_ms = perf.max;
    perf.n = 0, perf.sum = perf.max = 0;
}
void pip_keep_lit(void) { light_up(); }
void pip_frame_ready(void) { if (s.tick_timer) lv_timer_ready(s.tick_timer); } /* the background frame is done: take it now */

static void frame_took(float ms)
{
    perf.n++, perf.sum += ms, perf.max = ms > perf.max ? ms : perf.max;
    s.frame_ms += (ms - s.frame_ms) * 0.1f;
    dev_frame();
    uint32_t sec = lv_tick_get() / 1000;
    if (worst[sec % WORST_S].sec != sec || ms > worst[sec % WORST_S].ms)
        worst[sec % WORST_S] = (worst_t){sec, ms, s.shown, s.react_mood >= 0 ? s.react_name : ""};
}

float pip_frame_worst(int secs, const char **mood, const char **reaction)
{
    uint32_t now = lv_tick_get() / 1000;
    int k = -1;
    for (int i = 0; i < WORST_S; i++)
        if (now - worst[i].sec < (uint32_t)secs && worst[i].ms > 0 && (k < 0 || worst[i].ms > worst[k].ms)) k = i;
    if (mood) *mood = k < 0 ? "" : moods[worst[k].mood].name;
    if (reaction) *reaction = k < 0 ? "" : worst[k].reaction;
    return k < 0 ? 0 : worst[k].ms;
}

static void cosmos_show(void) /* his birth's sky, with the frame */
{
    for (int i = 0; i < 2; i++) cosmos_eye(i, s.face.eye[i].x, s.face.eye[i].y, s.face.eye[i].w);
    cosmos_set(s.hatch);
}
static void tick_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t el = lv_tick_elaps(s.last_tick);
    s.last_tick = lv_tick_get();
    update(fminf(el / 1000.0f, 0.1f));
    /* On his face the firmware's frames pace the screen: LVGL refreshes once a face frame, with whatever the effects
       (confetti, crumbs, the sponge...) moved meanwhile - not two or three times between, each nearly the whole screen
       when many small pieces move (past LVGL's 32 areas it redraws all of it) and each holding core 0 from the eyes */
    lv_timer_t *refr = face_start ? lv_display_get_refr_timer(lv_display_get_default()) : NULL;
    if (s.dark || unmet() || covered || lv_obj_get_screen(s.canvas) != lv_screen_active()) { /* dark, dormant, covered, or an app is open: skip drawing */
        s.last_swap = lv_tick_get(); /* and the time away is no slow frame */
        if (refr) lv_timer_resume(refr); /* the rest of the UI refreshes as LVGL likes */
        return;
    }
    if (face_start) { /* the firmware draws the next frame in the background while LVGL shows this one */
        if (refr) lv_timer_pause(refr);
        if (face_busy) return;
        if (s.hatch >= 0 && s.hatch < HATCH_EYES - 0.25f) { /* his birth's sky: no eyes to draw, both cores are its (cosmos_parallel) */
            if (s.pending) s.pending = false, memcpy(s.back_rects, s.rface.drawn, sizeof s.back_rects); /* what was drawn is on the back canvas, unseen */
            frame_took((float)lv_tick_elaps(s.last_swap));
            s.last_swap = lv_tick_get();
            cosmos_show(), sprite_snap(), sp_paint();
            if (refr) lv_timer_resume(refr), lv_timer_ready(refr);
            return;
        }
        if (s.pending) { /* it is ready: swap the canvases and redraw where the eyes were and are */
            uint8_t *d = s.buf->data;
            s.buf->data = s.back->data, s.back->data = d;
            dev_paint((uint16_t *)s.buf->data, s.rface.drawn);
            redraw(s.front_rects, s.rface.drawn);
            memcpy(s.back_rects, s.front_rects, sizeof s.back_rects);
            memcpy(s.front_rects, s.rface.drawn, sizeof s.front_rects);
            s.pending = false;
            frame_took((float)lv_tick_elaps(s.last_swap));
            s.last_swap = lv_tick_get();
            /* shown at once, in this pass: the refresh (and its wait for the panel) is then over long before the next
               frame is done, instead of holding LVGL's task just when that frame wants to be swapped in */
            cf_paint(); /* the confetti moves with the eyes' frame */
            if (s.hatch >= 0) cosmos_show();
            sp_paint();
            if (refr) lv_timer_resume(refr), lv_timer_ready(refr);
        }
        if (s.eco && lv_tick_elaps(s.last_draw) < ECO_FRAME_MS) return; /* the cores rest until the next frame is due */
        s.last_draw = lv_tick_get();
        s.rface = s.face; /* this moment's eyes, drawn over what the back canvas held two frames ago */
        memcpy(s.rface.drawn, s.back_rects, sizeof s.rface.drawn);
        sprite_snap(); /* the pictures as they are with these eyes */
        s.pending = true;
        face_start(&s.rface, (uint16_t *)s.back->data, SCR, SCR);
        return;
    }
    if (s.eco && lv_tick_elaps(s.last_draw) < ECO_FRAME_MS) return;
    s.last_draw = lv_tick_get();
    face_rect_t was[4];
    memcpy(was, s.face.drawn, sizeof was);
    face_draw(&s.face, (uint16_t *)s.buf->data, SCR, SCR);
    dev_paint((uint16_t *)s.buf->data, s.face.drawn);
    redraw(was, s.face.drawn);
    cf_paint();
    sprite_snap(), sp_paint();
    if (s.hatch >= 0) cosmos_show();
    frame_took((float)lv_tick_elaps(s.last_tick));
}

/* tap = poke (on an eye: that eye squints), a quick stroke = pet, a slow drag or a hold = Pip follows the finger;
   the constants for the newer feelings are with the others at the top */
#define PET_FAST 560.0f /* px/s: rubbing faster than this is petting */
#define PET_SLOW 320.0f /* px/s: slower than this he only watches the finger (between: whatever it was) */
static void touch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (pip_unmet()) return; /* the first waking takes a button */
    /* back after a while: now and then he says hello (pet_tune.talk 0: the checks' quiet runs) */
    if (code == LV_EVENT_PRESSED && s.t - s.input_at > 90 && pet_tune.talk > 0 && !s.pet.asleep && !s.dark && pet_valence(&s.pet) > -0.2f &&
        vrand(0, 1) < 0.45f * chat_mul[chat_lvl])
        s.hello_in = 0.5f;
    s.input_at = s.t; /* what he does now answers you */
    if (mt.seen && code != LV_EVENT_PRESSED) { /* two fingers or more this press: theirs (pip_fingers), no poke, stroke or swipe */
        if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            mt.seen = false, s.touching = s.petting = s.chasing = false, s.eye_pressed = false, s.press_eye = -1;
            s.tickle_t = 0, s.dodged = false, sp.on = false, s.next_look = frand(0.4f, 1.0f);
        }
        return;
    }
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        lv_point_t p;
        lv_indev_get_point(lv_indev_active(), &p);
        float d = code == LV_EVENT_PRESSED ? 0 : hypotf(p.x - s.fx, p.y - s.fy);
        s.fx = p.x;
        s.fy = p.y;
        if (code == LV_EVENT_PRESSED) s.press_x = s.tk_x = p.x, s.press_y = s.tk_y = p.y, s.tk_spread = TICKLE_SPREAD * 2, s.tickle_t = 0;
        /* how far the finger strays from where it has lately been: small while rubbing one patch, large stroking across */
        s.tk_x += (p.x - s.tk_x) * 0.2f, s.tk_y += (p.y - s.tk_y) * 0.2f;
        s.tk_spread += (hypotf(p.x - s.tk_x, p.y - s.tk_y) - s.tk_spread) * 0.2f;
        s.path += d;
        s.finger_path += d;
        if (code == LV_EVENT_PRESSING) { /* the finger's velocity, smoothed: where it is heading, for the chase */
            uint32_t ms = lv_tick_elaps(s.finger_tick);
            if (ms > 0 && ms < 200) {
                float k = fminf(1, ms / 80.0f);
                s.fvx += ((p.x - s.fx_prev) * 1000.0f / ms - s.fvx) * k, s.fvy += ((p.y - s.fy_prev) * 1000.0f / ms - s.fvy) * k;
            }
        } else s.fvx = s.fvy = 0;
        s.fx_prev = p.x, s.fy_prev = p.y, s.finger_tick = lv_tick_get();
        if (d > 1.5f) s.pet_recent = 0.3f;
        float speed = s.finger_path * 4; /* px/s over the last quarter second */
        if (code == LV_EVENT_PRESSING) spiral_feed(p.x, p.y);
        /* a spiral on him, or the finger going steadily round towards one: not a stroke (he watches it go round) */
        if (sp.on || fabsf(sp.turn) > SPIRAL_LEAD) s.petting = false;
        else if (lv_tick_elaps(s.press_tick) > 300 && s.path > 80 && speed > PET_FAST && fabsf(sp.turn) < SPIRAL_LEAD / 2)
            s.petting = true; /* a flick is too short */
        else if (speed < PET_SLOW) s.petting = false; /* slowed down: back to watching it */
    }
    if (code == LV_EVENT_PRESSED) {
        pip_seen();
        s.press_tick = lv_tick_get();
        s.touching = true;
        s.petting = false;
        s.path = s.pet_total = s.finger_path = 0;
        s.chase_t = 0, s.chasing = s.chased = false;
        s.pat_mood = s.stroke_mood = s.cheek_mood = s.eye_hold_mood = s.long_mood = s.chin_mood = s.rest_mood = s.tickle_mood = -1;
        s.eye_pressed = s.eye_hold_cross = false;
        spiral_reset(s.fx, s.fy), sp.on = false;
        if (!flinch()) life_press(s.fx, s.fy); /* timid: a finger coming at him; playful, he may duck it */
        s.press_eye = eyes_open && !s.dodged && !s.pet.asleep ? eye_at(s.fx, s.fy) : -1;
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (s.chasing) react(vary_to(PIP_HAPPY, PIP_BEAMING, 0.45f), 1.4f, "caught"), s.squash.v -= 4; /* the game ends: he got you */
        s.chasing = false;
        if (s.react_mood == PIP_LAUGHING && s.react_name && !strcmp(s.react_name, "tickle") && s.tickle_t > TICKLE_S + 1)
            react(PIP_LAUGHING, 1.2f + 1.2f * s.laugh_k, "tickle: after"), say(VOICE_GIGGLE); /* still giggling, getting his breath */
        s.tickle_t = 0;
        /* a stroke broken off before it got going, again and again: teased, he pouts */
        if (s.petting && s.pet_total < TEASE_S && !s.pet.asleep) {
            s.teases = s.t - s.tease_t < TEASE_SPAN_S ? s.teases + 1 : 1, s.tease_t = s.t;
            if (s.teases >= 3) {
                pet_event(&s.pet, PET_POKE), s.teases = 0;
                if (s.pet.love > 0.4f && vary_to(0, 1, 0.4f)) react(PIP_PUPPY, 3.0f, "puppy eyes"); /* why did you stop? */
                else if (vary_to(0, 1, 0.35f)) show_react(PIP_SULKING); /* hmf! */
                else react(PIP_POUTY, 3.0f, "teased");
            }
        }
        s.touching = false, s.eye_pressed = false, s.press_eye = -1;
        s.next_look = frand(0.4f, 1.0f);
        uint32_t ms = lv_tick_elaps(s.press_tick); /* a flick does nothing: the menu opens with the top button only */
        spiral_release();
        if (code == LV_EVENT_RELEASED && ms < TAP_MS && s.path < 25) { if (!life_tap(s.fx, s.fy)) poke_at(eye_at(s.fx, s.fy)); }
        else if (code == LV_EVENT_RELEASED) life_swipe(ms);
        s.dodged = false;
    }
}

/* On his face, a small invalidated area (a piece of confetti, a crumb, a bubble: where it was and where it is) grows to
   the 48 px tiles it touches. Pieces near each other then share tiles and LVGL keeps one (it drops an area inside one it
   has), so a burst of confetti stays under LVGL's 32 areas: past them it redraws the whole screen, ~26 ms a frame on the
   board. What is drawn is the same, only in fewer, slightly larger areas. */
#define FX_TILE 48
static void fx_tiles(lv_event_t *e)
{
    lv_area_t *a = lv_event_get_param(e);
    if (!a || cf_painting || !s.canvas || lv_obj_get_screen(s.canvas) != lv_screen_active()) return;
    if (lv_area_get_width(a) > FX_TILE || lv_area_get_height(a) > FX_TILE) return; /* the eyes, the text: as they are */
    a->x1 -= a->x1 % FX_TILE, a->y1 -= a->y1 % FX_TILE;
    a->x2 = LV_MIN(a->x2 - a->x2 % FX_TILE + FX_TILE - 1, SCR - 1), a->y2 = LV_MIN(a->y2 - a->y2 % FX_TILE + FX_TILE - 1, SCR - 1);
}

void pip_create(lv_obj_t *parent)
{
    if (face_start) lv_display_add_event_cb(lv_display_get_default(), fx_tiles, LV_EVENT_INVALIDATE_AREA, NULL);
    s.scr = parent, s.grave = NULL;
    s.input_at = s.babble_at = -100, s.react_mem = -1; /* nobody yet, nothing said yet */
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    /* crumbs and curses fly past the edge: that must not make the screen scrollable (LVGL would draw a scrollbar) */
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);

    /* the face is drawn in the screen's own byte order (the board's panel takes RGB565 swapped): LVGL then copies it
       to the frame as it is, without a pass swapping every pixel */
    s.face.swapped = lv_display_get_color_format(NULL) == LV_COLOR_FORMAT_RGB565_SWAPPED;
    lv_color_format_t cf = s.face.swapped ? LV_COLOR_FORMAT_RGB565_SWAPPED : LV_COLOR_FORMAT_RGB565;
    s.buf = lv_draw_buf_create(SCR, SCR, cf, 0);
    LV_ASSERT_MALLOC(s.buf);
    lv_draw_buf_clear(s.buf, NULL);
    if (face_start) {
        s.back = lv_draw_buf_create(SCR, SCR, cf, 0);
        LV_ASSERT_MALLOC(s.back);
        lv_draw_buf_clear(s.back, NULL);
        for (int i = 0; i < 4; i++) s.front_rects[i] = s.back_rects[i] = (face_rect_t){0, 0, -1, -1};
    }
    s.canvas = lv_canvas_create(parent);
    lv_canvas_set_draw_buf(s.canvas, s.buf);
    lv_obj_add_flag(s.canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s.canvas, touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s.canvas, touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s.canvas, touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s.canvas, touch_cb, LV_EVENT_PRESS_LOST, NULL);
    dev_create(parent); /* over the canvas */

    s.face.color = 0xFFFF;
    eye_colors(); /* the white, the pupil, the heart, veins and tears: again every frame */
    s.face.shading = 0; /* flat whites: the soft shadow crescent cost a third of the ESP32 frame */
    s.face.blush_color = 0xFBD4; /* #FF7AA0 */
    shape_t start = mood_shape(&moods[0]);
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < SHAPE_N; j++) s.shape[i][j].x = ((const float *)&start)[j];
    for (int c = 0; c < 3; c++) s.tint[c].x = 1;

    s.wink_t = -1;

    /* the biscuit: a round bake with three chocolate chips; the crumbs: little squares */
    biscuit = lv_obj_create(parent);
    lv_obj_remove_style_all(biscuit);
    lv_obj_set_size(biscuit, 34, 34);
    lv_obj_set_style_radius(biscuit, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(biscuit, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(biscuit, lv_color_hex(0xD9A35B), 0);
    lv_obj_remove_flag(biscuit, LV_OBJ_FLAG_CLICKABLE);
    static const int8_t chips[3][2] = {{8, 9}, {19, 6}, {14, 20}};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *c = lv_obj_create(biscuit);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, 6, 6);
        lv_obj_set_pos(c, chips[i][0], chips[i][1]);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(c, lv_color_hex(0x5A3418), 0);
    }
    lv_obj_add_flag(biscuit, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < N_CRUMBS; i++) {
        lv_obj_t *c = crumbs[i].o = lv_obj_create(parent);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, 6, 6);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    }

    for (int i = 0; i < N_SWEAR; i++) {
        lv_obj_t *l = swear[i].o = lv_label_create(parent);
        lv_obj_set_style_text_font(l, &font_pip_48, 0);
        lv_obj_set_size(l, 60, 58);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    }

    /* snowflakes (frozen), sweat drops (overheated) and a sneeze's spray: small round blobs that drift and fade */
    for (int i = 0; i < N_SNOW + N_SWEAT + N_SPRAY + N_PUFF; i++) {
        bool flake = i < N_SNOW, drop = !flake && i < N_SNOW + N_SWEAT, mist = i >= N_SNOW + N_SWEAT + N_SPRAY;
        int w = flake ? 6 + i % 3 * 2 : drop ? 10 : mist ? 14 + i % 2 * 6 : 6 + i % 2 * 2, h = drop ? 15 : w;
        lv_obj_t *o = lv_obj_create(parent);
        lv_obj_remove_style_all(o);
        lv_obj_set_size(o, w, h);
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(o, lv_color_hex(flake ? 0xEAF6FF : drop ? 0x9ED8FF : mist ? 0xC9DDB4 : 0xD6F0FF), 0);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        if (flake) snow[i].o = o;
        else if (drop) sweat[i - N_SNOW].o = o;
        else if (mist) puff[i - N_SNOW - N_SWEAT - N_SPRAY].o = o;
        else spray[i - N_SNOW - N_SWEAT].o = o;
    }

    mood_vals();
    for (int i = 0; i < N_DRIP; i++) { /* a wet Pip's drops: small, pale blue */
        lv_obj_t *o = drip[i].o = lv_obj_create(parent);
        lv_obj_remove_style_all(o);
        lv_obj_set_size(o, 5, 8);
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(o, lv_color_hex(0x8FD0FF), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
    sprite_create(parent);
    cosmos_create(parent);
    confl.layer = lv_obj_create(parent); /* the birthday's confetti: strips and squares, all drawn by this one */
    lv_obj_remove_style_all(confl.layer);
    lv_obj_set_size(confl.layer, SCR, SCR);
    lv_obj_remove_flag(confl.layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(confl.layer, cf_draw, LV_EVENT_DRAW_MAIN, NULL);
    for (int i = 0; i < N_CONFETTI; i++) confetti[i].o = NULL, confl.shown[i] = (lv_area_t){0, 0, -1, -1};

    s.text = lv_label_create(parent);
    lv_obj_set_style_text_font(s.text, &font_pip_48, 0);
    lv_label_set_text(s.text, "");
    s.open.x = 1;
    s.react_mood = -1;
    s.auto_mood = true;
    s.blink_t = -1;
    s.next_blink = 2;
    s.burp_anim = -1, s.next_burp = burp_rand(pet_tune.burp_min * 60, pet_tune.burp_min * 180);
    s.boop_anim = -1;
    s.sneeze_anim = -1, s.last_sneeze = -1e3f, s.next_sneeze = sneeze_rand(pet_tune.sneeze_min * 60, pet_tune.sneeze_min * 180);
    s.last_tick = lv_tick_get();
    grime_create(parent); /* the glass: over everything else on his screen */
    if (s.dead) grave_show(); /* rebuilt (a theme change): still gone */
    s.tick_timer = lv_timer_create(tick_cb, face_start ? 4 : 16, NULL); /* in the background mode a finished frame is picked up quickly */
}

void pip_glance(float x, float y, float secs)
{
    s.glance_x = x, s.glance_y = y, s.glance_left = secs;
}

/* the clock jumped (a made-up time replaced by the real one, the web sim's "Вернуть реальное время" after fast-forwards):
   every moment he remembers moves with it, so what he has lived stays lived (his age, his needs) and nothing he waits
   for (an aftertaste, a nap, the cable's last plug) ends early or stands still until the old clock comes round again */
void pip_pet_rebase(int64_t now)
{
    pet_t *p = &s.pet;
    int64_t d = now - p->last;
    if (!d) return;
    int64_t *at[] = {&p->up_until, &p->after_until, &p->rare_at, &p->hold_since, &p->charge_since, &p->woke_at,
                     &p->stuffed_at, &p->born, &p->nap_since, &p->perk_until, &p->lost_at, &p->washed_at,
                     &cab.plug_at, &cab.paid_at, &cab.fiddled_at};
    for (size_t i = 0; i < sizeof at / sizeof *at; i++) if (*at[i]) *at[i] += d; /* 0 is "never" */
    p->last = now;
    int32_t today = pet_local_day(now, s.tz); /* back in time: today is still today, no new morning rolled */
    if (p->day > today) p->day = today;
    if (p->bday_greeted > today) p->bday_greeted = today;
    pet_dirty = true;
}
