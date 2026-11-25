/* Dust on Pip's glass and the sponge that washes it off.
   Everything is a small plain LVGL object that only shows, hides, moves or fades: no scaling, no rotation, no
   full-screen layers. The dust is laid out once (the same pattern every time) and each piece has the level it
   shows from, so it builds up and washes off bit by bit; LVGL repaints the few pieces over the eyes along with
   them, a handful of tiny rectangles a frame. Smudges are the only see-through pieces, three at most. */
#include "grime.h"
#include <math.h>

#define SCR 466
#define MID (SCR / 2)
#define N_SPECK 46
#define N_HAIR 5
#define N_SMUDGE 3
#define N_BUBBLE 6 /* each a see-through round LVGL object redrawn every frame: half as many as at first, for the frame rate */
#define N_FOAM 6
#define N_GLINT 5
#define SPONGE_W 100
#define SPONGE_H 66

typedef struct { lv_obj_t *o; float x, y, vx, vy, life, max, delay; int size; } bit_t;

static struct {
    lv_obj_t *speck[N_SPECK], *hair[N_HAIR], *smudge[N_SMUDGE], *sponge;
    float speck_at[N_SPECK], hair_at[N_HAIR], smudge_at[N_SMUDGE];
    lv_point_precise_t hair_pts[N_HAIR][3];
    float level;
    bool scrubbing;
    float t, in;            /* scrubbing for t s; the sponge 0 away .. 1 on the glass */
    float fling;            /* flung: px it has flown off to the side (0: not flung) */
    float bubble_t, foam_t; /* s to the next one */
    int live;               /* bubbles, foam and glints still showing */
    bit_t bubble[N_BUBBLE], foam[N_FOAM], glint[N_GLINT];
} g = {.level = -1};

static uint32_t seed = 0x5EED1234u; /* the dust's layout: the same every time */
static float rnd(float a, float b)
{
    seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
    return a + (b - a) * (float)(seed % 10000) / 10000.0f;
}
static float frand(float a, float b) { return a + (b - a) * (float)lv_rand(0, 10000) / 10000.0f; }

static lv_obj_t *blob(lv_obj_t *parent, int w, int h, int radius, uint32_t color, lv_opa_t opa)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE); /* touches go through to his face */
    return o;
}

static void show(lv_obj_t *o, bool on) /* only on a change: hiding or showing invalidates it */
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) == on) lv_obj_set_flag(o, LV_OBJ_FLAG_HIDDEN, !on);
}

void grime_create(lv_obj_t *parent)
{
    /* specks: grit and fluff all over the round glass, the first ones tiny, later ones bigger; a mix of light dust
       (it reads on the black) and darker grit (it reads on the whites, whatever their colour) */
    static const uint32_t dust[] = {0xA79B85, 0x7D725F, 0xC9BDA4, 0x8F836D, 0x655B4B, 0xB5A98F};
    for (int i = 0; i < N_SPECK; i++) {
        float a = rnd(0, 6.2832f), r = 200 * sqrtf(rnd(0.02f, 1)), k = (float)i / N_SPECK;
        int size = (int)(2 + 4.5f * k + rnd(0, 1.6f));
        lv_obj_t *o = g.speck[i] = blob(parent, size, i % 3 ? size : size + 1, i % 3 ? LV_RADIUS_CIRCLE : 1, dust[i % 6], LV_OPA_COVER);
        lv_obj_set_pos(o, (int)(MID + cosf(a) * r) - size / 2, (int)(MID + sinf(a) * r) - size / 2);
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        g.speck_at[i] = k * 0.9f + rnd(0, 0.08f);
    }
    /* hairs and lint: three-point bent lines, once it is properly dusty */
    for (int i = 0; i < N_HAIR; i++) {
        float a = rnd(0, 6.2832f), r = 170 * sqrtf(rnd(0.05f, 1)), d = rnd(0, 6.2832f), len = rnd(16, 34), bend = rnd(-0.6f, 0.6f);
        float x0 = MID + cosf(a) * r, y0 = MID + sinf(a) * r;
        float x1 = x0 + cosf(d) * len * 0.5f, y1 = y0 + sinf(d) * len * 0.5f, x2 = x1 + cosf(d + bend) * len * 0.5f, y2 = y1 + sinf(d + bend) * len * 0.5f;
        float lx = fminf(x0, fminf(x1, x2)), ly = fminf(y0, fminf(y1, y2));
        g.hair_pts[i][0] = (lv_point_precise_t){x0 - lx, y0 - ly};
        g.hair_pts[i][1] = (lv_point_precise_t){x1 - lx, y1 - ly};
        g.hair_pts[i][2] = (lv_point_precise_t){x2 - lx, y2 - ly};
        lv_obj_t *l = g.hair[i] = lv_line_create(parent);
        lv_line_set_points(l, g.hair_pts[i], 3);
        lv_obj_set_pos(l, (int)lx, (int)ly);
        lv_obj_set_style_line_width(l, i % 2 + 1, 0);
        lv_obj_set_style_line_color(l, lv_color_hex(0x9A8F7A), 0);
        lv_obj_set_style_line_rounded(l, true, 0);
        lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
        g.hair_at[i] = 0.3f + i * 0.13f;
    }
    /* smudges: faint greasy blots where fingers go, over the eyes, only when he is really grubby */
    for (int i = 0; i < N_SMUDGE; i++) {
        int d = (int)rnd(64, 100);
        lv_obj_t *o = g.smudge[i] = blob(parent, d, (int)(d * rnd(0.75f, 1)), LV_RADIUS_CIRCLE, 0xC8BBA0, (lv_opa_t)rnd(30, 42));
        lv_obj_set_pos(o, (int)(MID + rnd(-130, 130)) - d / 2, (int)(MID + rnd(-70, 70)) - d / 2);
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        g.smudge_at[i] = 0.55f + i * 0.16f;
    }

    /* the lather: foam left where the sponge passed, bubbles rising (a ring with a glint) */
    for (int i = 0; i < N_FOAM; i++) {
        int d = 14 + (i * 7) % 18;
        g.foam[i] = (bit_t){blob(parent, d, d, LV_RADIUS_CIRCLE, 0xF4F8FF, 235), .size = d};
        lv_obj_add_flag(g.foam[i].o, LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < N_BUBBLE; i++) {
        int d = 10 + (i * 5) % 18;
        lv_obj_t *o = blob(parent, d, d, LV_RADIUS_CIRCLE, 0xFFFFFF, LV_OPA_TRANSP);
        lv_obj_set_style_border_width(o, 2, 0);
        lv_obj_set_style_border_color(o, lv_color_hex(0xEAF6FF), 0);
        lv_obj_set_style_border_opa(o, 230, 0);
        lv_obj_set_pos(blob(o, 3, 3, LV_RADIUS_CIRCLE, 0xFFFFFF, LV_OPA_COVER), d / 4, d / 4);
        g.bubble[i] = (bit_t){o, .size = d};
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }

    /* the sponge: a yellow kitchen sponge with its green scourer on top and a few pores */
    lv_obj_t *sp = g.sponge = blob(parent, SPONGE_W, SPONGE_H, 0, 0, LV_OPA_TRANSP);
    blob(sp, SPONGE_W, 30, 14, 0x3BAA66, LV_OPA_COVER);
    lv_obj_set_pos(blob(sp, SPONGE_W, SPONGE_H - 16, 14, 0xFFD45C, LV_OPA_COVER), 0, 16);
    static const int8_t pores[6][3] = {{16, 30, 8}, {40, 44, 6}, {62, 28, 9}, {82, 46, 7}, {26, 52, 6}, {70, 54, 5}};
    for (int i = 0; i < 6; i++) lv_obj_set_pos(blob(sp, pores[i][2], pores[i][2], LV_RADIUS_CIRCLE, 0xE9B33E, LV_OPA_COVER), pores[i][0], pores[i][1]);
    lv_obj_add_flag(sp, LV_OBJ_FLAG_HIDDEN);

    /* glints for squeaky clean: a thin cross with a dot */
    for (int i = 0; i < N_GLINT; i++) {
        lv_obj_t *o = blob(parent, 24, 24, 0, 0, LV_OPA_TRANSP);
        lv_obj_set_pos(blob(o, 2, 24, 1, 0xFFFFFF, LV_OPA_COVER), 11, 0);
        lv_obj_set_pos(blob(o, 24, 2, 1, 0xFFFFFF, LV_OPA_COVER), 0, 11);
        lv_obj_set_pos(blob(o, 6, 6, LV_RADIUS_CIRCLE, 0xFFFFFF, LV_OPA_COVER), 9, 9);
        g.glint[i] = (bit_t){o, .size = 24};
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
    grime_dust(0);
}

void grime_dust(float level)
{
    level = level < 0 ? 0 : level > 1 ? 1 : level;
    if (fabsf(level - g.level) < 0.002f) return; /* it creeps by the hour: nothing to do most frames */
    g.level = level;
    for (int i = 0; i < N_SPECK; i++) show(g.speck[i], level > g.speck_at[i]);
    for (int i = 0; i < N_HAIR; i++) show(g.hair[i], level > g.hair_at[i]);
    for (int i = 0; i < N_SMUDGE; i++) show(g.smudge[i], level > g.smudge_at[i]);
}

void grime_scrub(bool on)
{
    if (on && !g.scrubbing) g.t = 0, g.bubble_t = g.foam_t = 0, g.fling = 0;
    g.scrubbing = on;
}

void grime_fling(void)
{
    g.scrubbing = false;
    if (g.in > 0) g.fling = 1;
}

void grime_sparkle(void)
{
    for (int i = 0; i < N_GLINT; i++) {
        bit_t *b = &g.glint[i];
        float a = frand(0, 6.2832f), r = frand(30, 150);
        if (b->life <= 0) g.live++;
        *b = (bit_t){b->o, MID + cosf(a) * r * 1.3f, MID + sinf(a) * r * 0.7f, 0, 0, 0.55f, 0.55f, i * 0.11f, b->size};
        lv_obj_set_style_opa(b->o, LV_OPA_TRANSP, 0);
        lv_obj_set_pos(b->o, (int)b->x - b->size / 2, (int)b->y - b->size / 2);
        show(b->o, true);
    }
}

static void spawn(bit_t *pool, int n, float x, float y, float vx, float vy, float life)
{
    for (int i = 0; i < n; i++)
        if (pool[i].life <= 0) {
            pool[i] = (bit_t){pool[i].o, x, y, vx, vy, life, life, 0, pool[i].size};
            lv_obj_set_style_opa(pool[i].o, LV_OPA_TRANSP, 0);
            show(pool[i].o, true);
            g.live++;
            return;
        }
}

/* fade in quickly, out at the end; returns false once it is gone */
static bool age(bit_t *b, float dt, float wobble)
{
    if (b->life <= 0) return false;
    if (b->delay > 0) { b->delay -= dt; return true; }
    if ((b->life -= dt) <= 0) { show(b->o, false), g.live--; return false; }
    b->x += (b->vx + wobble * sinf(b->life * 11)) * dt, b->y += b->vy * dt;
    float in = fminf(1, (b->max - b->life) / 0.1f), out = fminf(1, b->life / 0.3f);
    lv_obj_set_style_opa(b->o, (lv_opa_t)(255 * in * out), 0);
    lv_obj_set_pos(b->o, (int)b->x - b->size / 2, (int)b->y - b->size / 2);
    return true;
}

void grime_step(float dt)
{
    if (!g.scrubbing && g.in <= 0 && g.live <= 0 && g.fling <= 0) return; /* nothing moving */
    /* the sponge slides in from below, scrubs a figure of eight over both eyes, slides back down when let go */
    float x = MID, y = SCR + SPONGE_H;
    if (g.fling > 0) { /* flung: it flies off to the side and a little up, fast, from where it was */
        g.fling += 1500 * dt;
        float px = MID + 92 * sinf(g.t * 6.2f), py = MID - 4 + 50 * sinf(g.t * 8.9f + 0.7f);
        x = px + g.fling, y = py - g.fling * 0.25f;
        lv_obj_set_pos(g.sponge, (int)x - SPONGE_W / 2, (int)y - SPONGE_H / 2);
        if (x - SPONGE_W / 2 > SCR) g.fling = 0, g.in = 0;
    } else {
        g.in = fmaxf(0, fminf(1, g.in + (g.scrubbing ? dt : -dt) / 0.25f));
        if (g.in > 0) {
            g.t += dt;
            float px = MID + 92 * sinf(g.t * 6.2f), py = MID - 4 + 50 * sinf(g.t * 8.9f + 0.7f);
            float e = g.in * g.in * (3 - 2 * g.in);
            x = px, y = SCR + SPONGE_H + (py - SCR - SPONGE_H) * e;
            lv_obj_set_pos(g.sponge, (int)x - SPONGE_W / 2, (int)y - SPONGE_H / 2);
        }
    }
    show(g.sponge, g.in > 0);
    if (g.scrubbing && g.in > 0.6f) { /* lather as it goes */
        if ((g.foam_t -= dt) <= 0) g.foam_t = 0.1f, spawn(g.foam, N_FOAM, x + frand(-46, 46), y + frand(-26, 26), 0, 6, frand(0.8f, 1.5f));
        if ((g.bubble_t -= dt) <= 0) g.bubble_t = 0.16f, spawn(g.bubble, N_BUBBLE, x + frand(-40, 40), y - 20 + frand(-15, 15), frand(-30, 30), frand(-140, -70), frand(0.8f, 1.4f));
    }
    for (int i = 0; i < N_FOAM; i++) age(&g.foam[i], dt, 0);
    for (int i = 0; i < N_BUBBLE; i++) age(&g.bubble[i], dt, 25);
    for (int i = 0; i < N_GLINT; i++) age(&g.glint[i], dt, 0);
}
