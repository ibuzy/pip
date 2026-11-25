#include "cosmos.h"
#include "lvgl_private.h" /* the layer's buffer and clip area: the frame is written directly, as theme.c's grid is */
#include <math.h>
#include <string.h>

/* His birth's sky, everything before his eyes: stars come out, one falls into the middle and lights a spark, it beats like a
   heart and the stars give way in ripples, they are drawn into a galaxy that winds up and falls in; dark, not a sound; the
   bang, and out of its fading light two slits of gold: his eyes, shut (cosmos_eye).
   Every frame is worked out from the time alone and covers the screen. The soft light (glows, rings) is worked out for 2x2
   cells, two rows at a time, into a small buffer (hot in the cache) and written once, dithered pixel by pixel, never read
   back - the frame is in slow PSRAM; the stars' points go on top, a few pixels each. Both cores take rows, then stars
   (cosmos_parallel). What a star always has (its place, size, colour) is worked out once. No libm per pixel but a ring's
   square root, and per star only fast sines. (The first version, full resolution, one core, libm: 9..13 fps on the board.) */
#define SCR 466
#define HALF (SCR / 2)
#define N 240
#define TAU 6.2831853f
#define SOFT 22

void (*cosmos_parallel)(void (*job)(void *ctx, int core), void *ctx);

typedef struct { float r, g, b; } rgb_t; /* 0..255 in 1/16: 0..4080 */
typedef struct {
    float a0, r0, rt, th0, swirl;   /* in the sky; in the galaxy: how far out, where on its arm, how fast round */
    float in, twf, twp, cs, sn;     /* when it comes out; its twinkle; the bang: its way */
    float sp, ilife;                /* ...how far, 1 / how long */
    rgb_t c, cb;
    uint8_t sz;
} star_t;
typedef struct { float x, y, R, w; int r, g, b; } soft_t; /* w 0: a glow of radius R; else a ring of radius R, w wide */
static struct {
    star_t st[N];
    soft_t soft[SOFT];
    int16_t acc[2][HALF * 3];       /* a row of cells' light, each core's own */
    rgb_t hue[64];                  /* the galaxy's colours from its middle to its rim */
} *wk; /* on the heap only while it plays (PSRAM on the board) */
static int n_soft;
static lv_obj_t *layer;
static float now;
static bool on;
static volatile int next_row, next_star, next_ray;
static struct { uint8_t *base; int stride, x0, y0; lv_area_t c; bool sw; } fr;

static inline uint16_t bswap(uint16_t v) { return (uint16_t)(v << 8 | v >> 8); }
static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static rgb_t hex(uint32_t c) { return (rgb_t){(c >> 16 & 255) * 16.0f, (c >> 8 & 255) * 16.0f, (c & 255) * 16.0f}; }
static rgb_t mix(rgb_t a, rgb_t b, float t) { return (rgb_t){a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
static inline float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static float ease(float a, float b, float x) { x = clamp01((x - a) / (b - a)); return x * x * (3 - 2 * x); }
static float hash(int i, int k) { uint32_t h = (uint32_t)(i * 374761393u + k * 668265263u); h = (h ^ h >> 13) * 1274126177u; return (h ^ h >> 16) % 10000 / 10000.0f; }
static inline float fsin(float x) /* within 2e-4 */
{
    float t = x * 0.15915494f;
    t -= (float)(int)t;
    if (t < 0) t += 1;
    float y = (t - 0.5f) * 6.2831853f;
    if (y > 1.5707963f) y = 3.1415927f - y;
    else if (y < -1.5707963f) y = -3.1415927f - y;
    float y2 = y * y;
    return -y * (1 + y2 * (-0.16666667f + y2 * (0.0083333310f + y2 * -0.00019840874f)));
}
static inline float fcos(float x) { return fsin(x + 1.5707963f); }
static const float C = SCR / 2;

/* ---- points: light added to the frame's own pixels ---- */
static inline void add(int x, int y, int r, int g, int b)
{
    if (x < fr.c.x1 || x > fr.c.x2 || y < fr.c.y1 || y > fr.c.y2) return;
    uint16_t *p = (uint16_t *)(fr.base + (y - fr.y0) * fr.stride) + (x - fr.x0), v = fr.sw ? bswap(*p) : *p;
    int d = bayer[y & 3][x & 3] * 8; /* 0..120 of 128: a 5-bit step is 128 of these, a 6-bit one 64 */
    int R = (v >> 11) + ((r + d) >> 7), G = ((v >> 5) & 63) + ((g + (d >> 1)) >> 6), B = (v & 31) + ((b + d) >> 7);
    v = (uint16_t)((R > 31 ? 31 : R) << 11 | (G > 63 ? 63 : G) << 5 | (B > 31 ? 31 : B));
    *p = fr.sw ? bswap(v) : v;
}
static void point(float x, float y, int sz, rgb_t c, float k) /* sz 1..4 px, soft at its edges; 4: with a cross of rays */
{
    if (k <= 0.01f) return;
    if (k > 1) k = 1;
    int r = (int)(c.r * k), g = (int)(c.g * k), b = (int)(c.b * k), ix = (int)x, iy = (int)y;
    add(ix, iy, r, g, b);
    if (sz < 2) return;
    add(ix + 1, iy, r, g, b), add(ix, iy + 1, r, g, b), add(ix + 1, iy + 1, r, g, b);
    if (sz < 3) return;
    int r2 = r / 2, g2 = g / 2, b2 = b / 2;
    for (int i = 0; i < 2; i++) add(ix + i, iy - 1, r2, g2, b2), add(ix + i, iy + 2, r2, g2, b2), add(ix - 1, iy + i, r2, g2, b2), add(ix + 2, iy + i, r2, g2, b2);
    if (sz < 4) return;
    for (int d = 2; d < 5; d++) {
        int q = 5 - d, rq = r * q / 6, gq = g * q / 6, bq = b * q / 6;
        add(ix, iy - d, rq, gq, bq), add(ix + 1, iy + d + 1, rq, gq, bq), add(ix - d, iy + 1, rq, gq, bq), add(ix + d + 1, iy, rq, gq, bq);
    }
}

/* ---- the soft light: noted, then worked out a row of cells at a time ---- */
static void glow(float cx, float cy, float R, rgb_t c, float k)
{
    if (k <= 0.004f || R < 2 || n_soft >= SOFT) return;
    wk->soft[n_soft++] = (soft_t){cx, cy, R, 0, (int)(c.r * k), (int)(c.g * k), (int)(c.b * k)};
}
static void ring(float R, float w, rgb_t c, float k)
{
    if (k <= 0.01f || R < w || R > 345 || n_soft >= SOFT) return;
    wk->soft[n_soft++] = (soft_t){C, C, R, w, (int)(c.r * k), (int)(c.g * k), (int)(c.b * k)};
}
static struct { float x, y, w, k; } slit[2]; /* a shut eye: a slit of light on the dark, w px long */

static void cells(int cy, int core) /* the two pixel rows of a row of cells */
{
    int16_t *acc = wk->acc[core];
    int lo = HALF, hi = -1;
    const float py = cy * 2 + 1;
    for (int i = 0; i < n_soft; i++) {
        const soft_t *f = &wk->soft[i];
        float dy = py - f->y, o = f->R + f->w, w2 = o * o - dy * dy;
        if (w2 <= 0) continue;
        float w = sqrtf(w2), dy2 = dy * dy;
        int x0 = (int)(f->x - w) / 2, x1 = (int)(f->x + w) / 2;
        if (f->x - w < 0) x0 = 0;
        if (x1 > HALF - 1) x1 = HALF - 1;
        if (x0 > x1) continue;
        if (x0 < lo) lo = x0;
        if (x1 > hi) hi = x1;
        if (f->w == 0) { /* (1 - d2/R2)^2 */
            float ir2 = 1 / (f->R * f->R), dx = x0 * 2 + 1 - f->x, v = 1 - (dy2 + dx * dx) * ir2, dv = -(4 * dx + 4) * ir2, ddv = -8 * ir2;
            int16_t *p = acc + x0 * 3;
            int fr_ = f->r, fg = f->g, fb = f->b;
            if (f->R > 100) { /* wide and dim: in steps of two cells, the light everywhere in steps of four (no one sees 4 or 8 px of it) */
                int st = f->R > 240 ? 4 : 2;
                float h = 2.0f * st;
                dv = -(2 * dx * h + h * h) * ir2, ddv = -2 * h * h * ir2;
                for (int x = x0; x + st - 1 <= x1; x += st, p += 3 * st, v += dv, dv += ddv) {
                    if (v <= 0) continue;
                    int q = (int)(v * v * 256), r = fr_ * q >> 8, g = fg * q >> 8, b = fb * q >> 8;
                    for (int k = 0; k < st * 3; k += 3) p[k] += r, p[k + 1] += g, p[k + 2] += b;
                }
            } else
                for (int x = x0; x <= x1; x++, p += 3, v += dv, dv += ddv) {
                    if (v <= 0) continue;
                    int q = (int)(v * v * 256);
                    p[0] += fr_ * q >> 8, p[1] += fg * q >> 8, p[2] += fb * q >> 8;
                }
        } else { /* only the band the ring is in: its two ends on this row */
            float ri = f->R - f->w, in2 = ri * ri - dy2, xi = in2 > 0 ? sqrtf(in2) : 0, iw = 256 / f->w;
            int l1 = (int)(f->x - xi) / 2, r0 = (int)(f->x + xi) / 2;
            if (r0 <= l1) r0 = l1 + 1;
            for (int e = 0; e < 2; e++) {
                int a = e ? r0 : x0, b = e ? x1 : l1;
                if (a < 0) a = 0;
                if (b > x1) b = x1;
                for (int x = a; x <= b; x++) {
                    float dx = x * 2 + 1 - f->x;
                    int q = 256 - (int)(fabsf(sqrtf(dx * dx + dy2) - f->R) * iw);
                    if (q <= 0) continue;
                    int16_t *p = acc + x * 3;
                    p[0] += f->r * q >> 8, p[1] += f->g * q >> 8, p[2] += f->b * q >> 8;
                }
            }
        }
    }
    for (int i = 0; i < 2; i++) { /* the light round a slit */
        float dy = fabsf(py - slit[i].y), hw = slit[i].w / 2;
        if (slit[i].k <= 0 || dy > 9 || hw < 2) continue;
        float kv = 0.5f * (1 - dy / 9) * (1 - dy / 9) * slit[i].k, ih = 1 / hw;
        int x0 = (int)(slit[i].x - hw) / 2, x1 = (int)(slit[i].x + hw) / 2;
        if (x0 < 0) x0 = 0;
        if (x1 > HALF - 1) x1 = HALF - 1;
        if (x0 < lo) lo = x0;
        if (x1 > hi) hi = x1;
        for (int x = x0; x <= x1; x++) {
            float u = (x * 2 + 1 - slit[i].x) * ih, v = (1 - u * u) * kv;
            int16_t *p = acc + x * 3;
            p[0] += (int)(4080 * v), p[1] += (int)(3500 * v), p[2] += (int)(1900 * v);
        }
    }
    /* out: both pixel rows, as far as the round glass goes (its corners are never seen). A cell's three channels are packed
       into one word with 4 bits of fraction each, the dither added to all three at once, two pixels written at a time */
    float gy = fmaxf(fabsf(py - C) - 1, 0), gw = sqrtf(fmaxf(0, 234.0f * 234.0f - gy * gy));
    int g0 = (int)(C - gw) / 2, g1 = (int)(C + gw) / 2;
    if (g0 < 0) g0 = 0;
    if (g1 > HALF - 1) g1 = HALF - 1;
    if (fr.c.x1 > g0 * 2) g0 = (fr.c.x1 + 1) / 2;
    if (fr.c.x2 < g1 * 2 + 1) g1 = (fr.c.x2 - 1) / 2;
    int l = lo > g0 ? lo : g0, h = hi < g1 ? hi : g1;
    for (int j = 0; j < 2 && g1 >= g0; j++) {
        int y = cy * 2 + j;
        if (y < fr.c.y1 || y > fr.c.y2) continue;
        uint32_t *out = (uint32_t *)((uint16_t *)(fr.base + (y - fr.y0) * fr.stride) - fr.x0); /* a cell is one word: x even */
        if (h < l) { memset(out + g0, 0, (size_t)(g1 - g0 + 1) * 4); continue; }
        if (l > g0) memset(out + g0, 0, (size_t)(l - g0) * 4);
        if (h < g1) memset(out + h + 1, 0, (size_t)(g1 - h) * 4);
        const uint8_t *by = bayer[y & 3];
        uint32_t dd[4];
        for (int k = 0; k < 4; k++) dd[k] = by[k] * 0x00200401u; /* the same dither in all three fields */
        const bool sw = fr.sw;
        const int16_t *q = acc + l * 3;
        for (int x = l; x <= h; x++, q += 3) {
            int r = q[0], g = q[1], bl = q[2];
            if (!(r | g | bl)) { out[x] = 0; continue; }
            r >>= 3, g >>= 2, bl >>= 3; /* 5.4, 6.4, 5.4 */
            if (r > 496) r = 496;
            if (g > 1008) g = 1008;
            if (bl > 496) bl = 496;
            uint32_t w = (uint32_t)r << 21 | (uint32_t)g << 10 | (uint32_t)bl, e = (x & 1) * 2, a_ = w + dd[e], b_ = w + dd[e + 1];
            a_ = (a_ >> 14 & 0xF800) | (a_ >> 9 & 0x07E0) | (a_ >> 4 & 31);
            b_ = (b_ >> 14 & 0xF800) | (b_ >> 9 & 0x07E0) | (b_ >> 4 & 31);
            w = b_ << 16 | a_;
            out[x] = sw ? (w >> 8 & 0x00FF00FFu) | (w & 0x00FF00FFu) << 8 : w;
        }
    }
    if (hi >= lo) memset(acc + lo * 3, 0, (size_t)(hi - lo + 1) * 6);
}

/* ---- the stars ---- */
static void stars_make(void)
{
    static const uint32_t star_hue[6] = {0xFFFFFF, 0xCFE0FF, 0xFFF0CF, 0xBFD0FF, 0xFFE0EC, 0xE4D8FF};
    static const uint32_t bang_hue[6] = {0xFFD65A, 0xFF7FB0, 0x7FD8FF, 0xB48CFF, 0xFFFFFF, 0x8CFFC8};
    for (int i = 0; i < N; i++) {
        star_t *s = &wk->st[i];
        float big = hash(i, 6);
        s->a0 = fmodf(i * 2.39996f, TAU), s->r0 = 18 + 222 * sqrtf(hash(i, 1));
        s->rt = 16 + 196 * powf(hash(i, 2), 0.75f);
        s->th0 = (i % 3) * (TAU / 3) + 1.9f * logf(s->rt / 18) + (hash(i, 3) - 0.5f) * 0.5f, s->swirl = 0.55f + 70 / (s->rt + 50);
        s->in = 0.5f + hash(i, 4) * 3.0f, s->twf = 1.4f + 2.6f * hash(i, 5), s->twp = (float)i;
        s->cs = cosf(s->a0), s->sn = sinf(s->a0), s->sp = (90 + 190 * hash(i, 11)) * 1.15f, s->ilife = 1 / (1.1f + 0.9f * hash(i, 12));
        s->c = hex(star_hue[i % 6]), s->cb = hex(bang_hue[i % 6]);
        s->sz = big > 0.94f ? 4 : big > 0.76f ? 3 : big > 0.35f ? 2 : 1;
    }
    for (int i = 0; i < 64; i++) { /* gold in the middle, rose, violet, blue at the rim */
        float q = i / 63.0f;
        wk->hue[i] = q < 0.33f ? mix(hex(0xFFE9B0), hex(0xFF8FB4), q * 3) : q < 0.66f ? mix(hex(0xFF8FB4), hex(0xA77BFF), q * 3 - 1)
                                                                                     : mix(hex(0xA77BFF), hex(0x62C6FF), fminf(1, q * 3 - 2));
    }
}
/* the moment's numbers, the same for every star */
typedef struct { float t, g, gal, fall, e, spin, shrink, age[COSMOS_BEATS], amp[COSMOS_BEATS]; } moment_t;
static moment_t moment(float t)
{
    moment_t m = {.t = t, .g = t - COSMOS_GALAXY};
    m.gal = ease(0, 1.6f, m.g), m.fall = ease(COSMOS_FALL_S, COSMOS_COLLAPSE - COSMOS_GALAXY, m.g), m.e = ease(0, 1.7f, m.g);
    m.spin = 0.55f * m.g + 0.24f * m.g * m.g, m.shrink = powf(1 - m.fall, 1.3f);
    for (int k = 0; k < COSMOS_BEATS; k++) m.age[k] = t - cosmos_beat[k], m.amp[k] = m.age[k] <= 0 || m.age[k] > 1.6f ? 0 : (10 + 3 * k) * (1 - m.age[k] / 1.6f);
    return m;
}
/* where star s is: in the sky (pushed out by the beats as they pass, like a ripple through water), then in an arm of the
   galaxy, then falling in */
static void star_at(const star_t *s, const moment_t *m, float *x, float *y, float *rad)
{
    float a = s->a0 + 0.035f * m->t, r = s->r0;
    for (int k = 0; k < COSMOS_BEATS; k++) {
        if (m->amp[k] <= 0) continue;
        float q = (s->r0 - m->age[k] * 270) * (1 / 34.0f), w = 1 - q * q * 0.2f;
        if (w > 0) r += m->amp[k] * w * w * w * w;
    }
    if (m->g > 0) {
        float th = s->th0 + m->spin * s->swirl + m->fall * m->fall * 5, da = th - a; /* onto the arm the shorter way at first, then on round with it */
        da -= TAU * floorf((da - m->spin + TAU / 2) * (1 / TAU));
        a += da * m->e, r += (s->rt * m->shrink - r) * m->e;
    }
    *x = C + r * fcos(a), *y = C + r * fsin(a), *rad = r;
}
static moment_t mo, mo2; /* now, and a moment before (the stars' trails) */
static void sky_star(int i)
{
    const star_t *s = &wk->st[i];
    float in = clamp01((mo.t - s->in) * (1 / 0.6f)), x, y, r, x2, y2, r2;
    if (in <= 0) return;
    float tw = 0.62f + 0.38f * fsin(mo.t * s->twf + s->twp);
    star_at(s, &mo, &x, &y, &r);
    rgb_t c = s->c;
    int sz = s->sz < 2 && mo.gal > 0.5f ? 2 : s->sz;
    if (mo.gal > 0) c = mix(c, wk->hue[r < 200 ? (int)(r * (63 / 200.0f)) : 63], 0.8f * mo.gal);
    float k = in * (tw + 0.5f * mo.gal);
    point(x, y, sz, c, k);
    if (mo.g <= 0.3f) return;
    star_at(s, &mo2, &x2, &y2, &r2);
    for (int j = 1, n = mo.fall > 0.05f ? 5 : 3; j < n; j++) point(x + (x2 - x) * j, y + (y2 - y) * j, 2, c, k * 0.7f / (j + 0.4f));
}
static float bang_e[5]; /* how far out, of a star's own way, at five moments just gone */
static void bang_star(int i)
{
    const star_t *s = &wk->st[i];
    float u = mo.t - COSMOS_BANG, k = 1 - u * s->ilife;
    if (k <= 0) return;
    float b = (k > 0.667f ? 1 : 1.5f * k) * (0.75f + 0.25f * fsin(u * 18 + s->twp));
    static const float f[5] = {1, 0.571f, 0.333f, 0.235f, 0.182f};
    for (int j = 4; j >= 0; j--) {
        float v = u - j * 0.028f;
        if (v < 0) continue;
        float r = s->sp * bang_e[j];
        point(C + r * s->cs, C + r * s->sn + 16 * v * v, j > 1 ? 2 : j ? 3 : 4, s->cb, b * f[j]);
    }
}

/* ---- the scenes: what is soft is noted first, what is points drawn after the rows ---- */
static void sky_soft(float t)
{
    /* clouds far off, turning slowly; then the galaxy's own, strung along its arms, drawn in with it */
    static const uint32_t cloud[3] = {0x7A3CFF, 0x1F8CFF, 0xFF4FA0};
    float far_ = ease(0.6f, 3.5f, t) * (1 - mo.gal);
    for (int i = 0; i < 3 && far_ > 0.01f; i++) {
        float a = 0.06f * t + i * 2.094f + 0.5f;
        glow(C + 120 * fcos(a), C + 120 * fsin(a), 135, hex(cloud[i]), 0.115f * far_ * (0.8f + 0.2f * fsin(t * 0.9f + i * 2)));
    }
    if (mo.gal <= 0.02f) return;
    for (int arm = 0; arm < 3; arm++)
        for (int j = 0; j < 2; j++) {
            float rt = j ? 140 : 74, a = arm * (TAU / 3) + (j ? 3.897f : 2.686f) + mo.spin * (0.55f + 70 / (rt + 50)) + mo.fall * mo.fall * 5, q = rt * (1 - mo.fall);
            glow(C + q * fcos(a), C + q * fsin(a), (j ? 78 : 66) * (1 - 0.4f * mo.fall), hex(j ? 0x4A6CFF : 0xE050C0), (j ? 0.2f : 0.24f) * mo.gal * (1 - 0.3f * mo.fall));
        }
    glow(C, C, 60 + 40 * mo.gal, hex(0xFFB060), 0.4f * mo.gal);
}
/* a falling star: in from the rim on a long curve, a tail behind it, into the middle */
static void falling(float t, bool points)
{
    float u = (t - COSMOS_STAR) / (COSMOS_SPARK - COSMOS_STAR);
    if (u <= 0 || u > 1.4f) return;
    for (int k = points ? 34 : 0; k >= 0; k--) {
        float v = u - k * 0.011f;
        if (v < 0 || v > 1) continue;
        float q = 1 - v, a = -2.35f + 2.6f * v * v, r = 250 * q * (1 + 0.25f * v), f = 1 - k / 35.0f, x = C + r * fcos(a), y = C + r * fsin(a);
        if (points) point(x, y, k == 0 ? 4 : k < 8 ? 3 : 2, mix(hex(0x6FA8FF), hex(0xFFF3C8), f), f * f);
        else glow(x, y, 26, hex(0xFFE2A0), 0.7f);
    }
}
/* the spark in the middle: lit, beating, brighter with every beat, and swallowing the galaxy */
static void spark(float t)
{
    float a = t - COSMOS_SPARK;
    if (a <= 0) return;
    float beat = 0;
    int n = 0;
    for (int k = 0; k < COSMOS_BEATS; k++) {
        float g = t - cosmos_beat[k];
        if (g >= 0) beat += expf(-g * 5) * (1 - expf(-g * 40)) + (g > 0.24f ? 0.7f * expf(-(g - 0.24f) * 6) * (1 - expf(-(g - 0.24f) * 40)) : 0), n++;
    }
    float fall = ease(COSMOS_GALAXY + COSMOS_FALL_S, COSMOS_COLLAPSE, t), lit = fminf(1, a / 0.25f), first = expf(-a * 4);
    float R = (9 + 2.2f * n + 13 * beat + 12 * fall) * lit;
    if (t > COSMOS_COLLAPSE) R *= 1 - ease(COSMOS_COLLAPSE, COSMOS_HUSH, t); /* gone out: a breath held */
    glow(C, C, R * 4.4f + 90 * first, hex(0xFF8A30), 0.30f + 0.2f * beat);
    glow(C, C, R * 2.0f, hex(0xFFD080), 0.8f);
    glow(C, C, R, hex(0xFFFFFF), 1);
    if (first > 0.05f) ring(20 + 150 * (1 - first), 5, hex(0xFFE2A0), first * 0.8f);
    for (int k = 0; k < COSMOS_BEATS; k++) { /* every beat: a ring, farther and brighter each time */
        float g = t - cosmos_beat[k];
        if (g < 0 || g > 1.1f) continue;
        static const uint32_t hue[COSMOS_BEATS] = {0xFFB870, 0xFF8FA8, 0xC08CFF, 0x7FB8FF};
        ring(14 + 250 * g * (1 - 0.25f * g), 4 + 2 * k, hex(hue[k]), (0.45f + 0.15f * k) * (1 - g / 1.1f));
    }
    float u = (t - COSMOS_COLLAPSE + 0.5f) / 0.5f;
    if (u > 0 && u < 1) ring(230 * (1 - u) * (1 - u) + 4, 6, hex(0xCFE0FF), 0.5f * u), ring(170 * (1 - u) + 4, 4, hex(0xFFD6F0), 0.4f * u); /* drawn in */
}
/* the bang: light everywhere, fading gold to violet to black; rays, four rings, every star flung out in its own colour */
static void bang_soft(float u)
{
    float fl = expf(-u * 5.5f);
    glow(C, C, 560, hex(0xFFE6C0), 1.15f * expf(-u * 7));               /* for a moment, light everywhere */
    glow(C, C, 300, hex(0xFF9A50), 0.8f * expf(-u * 4));
    glow(C, C, 250, hex(0xB060FF), 0.45f * (1 - fl) * expf(-u * 2.2f)); /* ...cooling to violet */
    glow(C, C, 50 + 130 * fl, hex(0xFFFFFF), 1.2f * expf(-u * 3.2f));
    static const uint32_t hue[4] = {0xFFFFFF, 0xFFC860, 0xFF7FC0, 0x70B8FF};
    for (int k = 0; k < 4; k++) {
        float v = u - 0.13f * k;
        if (v > 0 && v < 1.2f) ring(10 + 330 * (1 - expf(-v * (2.4f - 0.25f * k))), 9 - 1.2f * k, hex(hue[k]), 1 - v / 1.2f);
    }
}
static void ray(int k, float u, float lum)
{
    float a = k * (TAU / 18) + 0.35f * u + 0.2f * hash(k, 9), len = (150 + 110 * hash(k, 8)) * ease(0, 0.4f, u), cs = fcos(a), sn = fsin(a), il = 1 / fmaxf(len, 1);
    rgb_t c = hex(0xFFE8C0);
    for (float r = 20; r < len; r += 3.2f) point(C + r * cs, C + r * sn, r < len * 0.5f ? 3 : 2, c, lum * 0.7f * (1 - r * il));
}
static void slit_line(int i) /* its bright middle */
{
    float hw = slit[i].w / 2;
    if (slit[i].k <= 0 || hw < 2) return;
    int y0 = (int)(slit[i].y - 1), x0 = (int)(slit[i].x - hw), x1 = (int)(slit[i].x + hw);
    float ih = 1 / hw;
    for (int x = x0; x <= x1; x++) {
        float u = (x - slit[i].x) * ih, v = (1 - u * u) * slit[i].k * 0.8f;
        int r = (int)(4080 * v), g = (int)(3500 * v), b = (int)(1900 * v);
        for (int j = 0; j < 2; j++) add(x, y0 + j, r, g, b);
    }
}

static void rows_job(void *ctx, int core)
{
    (void)ctx;
    for (int y; (y = __atomic_fetch_add(&next_row, 8, __ATOMIC_RELAXED)) < HALF;)
        for (int k = 0; k < 8 && y + k < HALF; k++)
            if ((y + k) * 2 + 1 >= fr.c.y1 && (y + k) * 2 <= fr.c.y2) cells(y + k, core);
}
static float ray_lum;
static void stars_job(void *ctx, int core)
{
    (void)ctx, (void)core;
    if (ray_lum > 0.02f)
        for (int k; (k = __atomic_fetch_add(&next_ray, 1, __ATOMIC_RELAXED)) < 18;) ray(k, mo.t - COSMOS_BANG, ray_lum);
    for (int i; (i = __atomic_fetch_add(&next_star, 12, __ATOMIC_RELAXED)) < N;)
        for (int k = 0; k < 12 && i + k < N; k++) {
            if (mo.t < COSMOS_COLLAPSE) sky_star(i + k);
            else if (mo.t >= COSMOS_BANG) bang_star(i + k);
        }
}
static void both(void (*job)(void *, int))
{
    if (cosmos_parallel) cosmos_parallel(job, NULL);
    else job(NULL, 0);
}

static void draw(lv_event_t *e)
{
    lv_layer_t *l = lv_event_get_layer(e);
    if (!on || !l || !l->draw_buf) return;
    lv_area_t full = {0, 0, SCR - 1, SCR - 1};
    lv_color_format_t cf = l->draw_buf->header.cf;
    if ((cf != LV_COLOR_FORMAT_RGB565 && cf != LV_COLOR_FORMAT_RGB565_SWAPPED) || !lv_area_intersect(&fr.c, &l->_clip_area, &full)) return;
    fr.base = l->draw_buf->data, fr.stride = (int)l->draw_buf->header.stride, fr.x0 = l->buf_area.x1, fr.y0 = l->buf_area.y1, fr.sw = cf == LV_COLOR_FORMAT_RGB565_SWAPPED;
    float t = now, u = t - COSMOS_BANG;
    mo = moment(t), mo2 = moment(t - 0.03f * (1 + mo.fall));
    n_soft = 0, next_row = next_star = next_ray = 0;
    if (t < COSMOS_COLLAPSE) sky_soft(t), falling(t, false);
    if (t < COSMOS_HUSH) spark(t);
    else if (u >= 0) bang_soft(u);
    both(rows_job);
    if (t < COSMOS_COLLAPSE || u >= 0) {
        for (int j = 0; j < 5; j++) bang_e[j] = u - j * 0.028f > 0 ? 1 - expf(-2.6f * (u - j * 0.028f)) : 0;
        ray_lum = u >= 0 ? ease(0, 0.12f, u) * (1 - ease(0.5f, 1.5f, u)) : 0;
        both(stars_job);
    } else if (t >= COSMOS_HUSH) point(C - 1, C - 1, 2, hex(0xFFFFFF), 0.25f + 0.2f * fsin(t * 20)); /* the hush: one faint point, barely there */
    if (t < COSMOS_COLLAPSE) falling(t, true);
    for (int i = 0; i < 2; i++) slit_line(i);
}
static void cover(lv_event_t *e) /* it paints every pixel: nothing under it need be drawn */
{
    if (on) ((lv_cover_check_info_t *)lv_event_get_param(e))->res = LV_COVER_RES_COVER; /* set_cover_res only raises what the object said */
}
void cosmos_create(lv_obj_t *parent)
{
    layer = lv_obj_create(parent);
    lv_obj_remove_style_all(layer);
    lv_obj_set_size(layer, SCR, SCR);
    lv_obj_remove_flag(layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(layer, draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(layer, cover, LV_EVENT_COVER_CHECK, NULL);
    on = false;
}
void cosmos_set(float t)
{
    bool want = t >= 0 && t < COSMOS_END;
    if (want && !wk && (wk = lv_calloc(1, sizeof *wk))) stars_make();
    if (!want && wk) lv_free(wk), wk = NULL;
    want = want && wk;
    if (want || on) lv_obj_invalidate(layer);
    on = want, now = t;
}
void cosmos_eye(int i, float x, float y, float w)
{
    slit[i].x = x, slit[i].y = y, slit[i].w = w * ease(COSMOS_BANG + 0.7f, COSMOS_BANG + 1.6f, now), slit[i].k = ease(COSMOS_BANG + 0.7f, COSMOS_BANG + 1.3f, now);
}
