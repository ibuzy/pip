#include "sprite.h"
#include <math.h>
#include <string.h>

#define N 28 /* the first SPRITE_PINS held, the rest flying */
#define BOXES 8

const int sprite_px[SPRITE_SIZES] = {18, 28, 44, 64, 92, 0};

typedef struct { lv_area_t a; uint32_t color; uint8_t kind, size, opa; } shown_t; /* opa 0: nothing there */
static struct pool {
    sprite_t s[N];
    shown_t snap[N], shown[N]; /* as they were with the face being drawn; as they are on the screen */
    lv_image_dsc_t img[SP_KINDS][SPRITE_SIZES]; /* LVGL draws a while after it is asked to: these must outlive draw() */
} *p; /* on the heap, over 2 KB: the board puts it in PSRAM */
static lv_obj_t *layer;
static long drawn;

static void draw(lv_event_t *e)
{
    lv_layer_t *l = lv_event_get_layer(e);
    for (int i = 0; i < N; i++) {
        const shown_t *s = &p->shown[i];
        if (!s->opa) continue;
        if (s->kind == SP_RECT) {
            lv_draw_rect_dsc_t d;
            lv_draw_rect_dsc_init(&d);
            d.radius = 4, d.bg_color = lv_color_hex(s->color), d.bg_opa = s->opa;
            lv_draw_rect(l, &d, &s->a);
            continue;
        }
        const lv_image_dsc_t *img = &p->img[s->kind][s->size];
        if (!img->data) continue;
        lv_draw_image_dsc_t d;
        lv_draw_image_dsc_init(&d);
        d.src = img, d.recolor = lv_color_hex(s->color), d.recolor_opa = LV_OPA_COVER, d.opa = s->opa;
        lv_draw_image(l, &d, &s->a);
    }
}

void sprite_create(lv_obj_t *parent)
{
    if (!p) p = lv_malloc(sizeof *p);
    LV_ASSERT_MALLOC(p);
    memset(p, 0, sizeof *p);
    for (int k = 0; k < SP_KINDS; k++)
        for (int z = 0; z < SPRITE_SIZES; z++) {
            uint32_t n = z == SP_OWN ? sprite_own[k][0] : (uint32_t)sprite_px[z], nh = z == SP_OWN ? sprite_own[k][1] : n;
            p->img[k][z] = (lv_image_dsc_t){.header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .w = n, .h = nh, .stride = n},
                                            .data_size = n * nh, .data = sprite_art[k][z]};
        }
    layer = lv_obj_create(parent);
    lv_obj_remove_style_all(layer);
    lv_obj_set_size(layer, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(layer, draw, LV_EVENT_DRAW_MAIN, NULL);
}

sprite_t *sprite_go(int kind, int size, float x, float y, float vx, float vy, float life, uint32_t color)
{
    for (int i = SPRITE_PINS; i < N; i++) {
        if (p->s[i].life > 0) continue;
        p->s[i] = (sprite_t){.kind = (uint8_t)kind, .size = (uint8_t)size, .x = x, .y = y, .vx = vx, .vy = vy, .life = life, .max = life, .color = color};
        return &p->s[i];
    }
    return NULL;
}
void sprite_pin(int slot, int kind, int size, float x, float y, uint32_t color)
{
    p->s[slot] = (sprite_t){.kind = (uint8_t)kind, .size = (uint8_t)size, .x = x, .y = y, .life = 1, .max = 1, .color = color};
}
void sprite_bar(int slot, float x, float y, int w, int h, uint32_t color)
{
    p->s[slot] = (sprite_t){.kind = SP_RECT, .x = x, .y = y, .life = 1, .max = 1, .color = color, .w = (int16_t)w, .h = (int16_t)h};
}
void sprite_unpin(void) { for (int i = 0; i < SPRITE_PINS; i++) p->s[i].life = 0; }
void sprite_clear(void) { for (int i = 0; i < N; i++) p->s[i].life = 0; }
sprite_t *sprite_oldest(int kind)
{
    sprite_t *o = NULL;
    for (int i = SPRITE_PINS; i < N; i++)
        if (p->s[i].life > 0 && p->s[i].kind == kind && (!o || p->s[i].max - p->s[i].life > o->max - o->life)) o = &p->s[i];
    return o;
}

void sprite_step(float dt)
{
    for (int i = SPRITE_PINS; i < N; i++) {
        sprite_t *s = &p->s[i];
        if (s->life <= 0) continue;
        s->life -= dt, s->vy += s->ay * dt, s->x += s->vx * dt, s->y += s->vy * dt;
        if (s->sway != 0) s->ph += dt * 4, s->x += cosf(s->ph) * s->sway * dt;
        if (s->x < -50 || s->x > 516 || s->y < -50 || s->y > 520) s->life = 0; /* off the glass */
    }
}

void sprite_snap(void)
{
    for (int i = 0; i < N; i++) {
        const sprite_t *s = &p->s[i];
        shown_t *n = &p->snap[i];
        n->opa = 0;
        if (s->life <= 0) continue;
        float k = s->life / s->max, opa = i < SPRITE_PINS ? 1 : fminf(1, (s->max - s->life) / 0.12f) * fminf(1, k / 0.35f);
        int w = s->kind == SP_RECT ? s->w : s->size == SP_OWN ? sprite_own[s->kind][0] : sprite_px[s->size], h = s->kind == SP_RECT ? s->h : s->size == SP_OWN ? sprite_own[s->kind][1] : w;
        n->kind = s->kind, n->size = s->size, n->color = s->color, n->opa = (uint8_t)(opa * 255);
        n->a.x1 = (int32_t)lroundf(s->x) - w / 2, n->a.y1 = (int32_t)lroundf(s->y) - h / 2;
        n->a.x2 = n->a.x1 + w - 1, n->a.y2 = n->a.y1 + h - 1;
    }
}

static lv_area_t both(const lv_area_t *a, const lv_area_t *b)
{
    return (lv_area_t){LV_MIN(a->x1, b->x1), LV_MIN(a->y1, b->y1), LV_MAX(a->x2, b->x2), LV_MAX(a->y2, b->y2)};
}
void sprite_paint(void)
{
    if (!layer) return;
    lv_area_t box[BOXES];
    int nb = 0;
    for (int i = 0; i < N; i++) {
        const shown_t *was = &p->shown[i], *now = &p->snap[i];
        if (!was->opa && !now->opa) continue;
        if (was->opa == now->opa && was->kind == now->kind && was->size == now->size && was->color == now->color &&
            !memcmp(&was->a, &now->a, sizeof was->a)) continue; /* as it was: nothing to draw */
        lv_area_t u = was->opa && now->opa ? both(&was->a, &now->a) : was->opa ? was->a : now->a;
        p->shown[i] = *now;
        int best = -1;
        int32_t cost = 0;
        for (int b = 0; b < nb; b++) { /* the box it grows least (px added beyond the two) */
            lv_area_t j = both(&box[b], &u);
            int32_t c = (int32_t)lv_area_get_size(&j) - (int32_t)lv_area_get_size(&box[b]) - (int32_t)lv_area_get_size(&u);
            if (best < 0 || c < cost) best = b, cost = c;
        }
        if (best >= 0 && (nb == BOXES || cost < 1500)) box[best] = both(&box[best], &u);
        else box[nb++] = u;
    }
    for (int b = 0; b < nb; b++) lv_obj_invalidate_area(layer, &box[b]), drawn += (long)lv_area_get_size(&box[b]);
}
long sprite_px_drawn(void) { long d = drawn; drawn = 0; return d; }
