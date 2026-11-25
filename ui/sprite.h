/* Little pictures over Pip's face: hearts, stars, notes, a butterfly... Each is a small A8 mask made beforehand
   (tools/sprites.py -> sprite_art.c, in flash) and drawn in one flat colour, all by one object, as the confetti is:
   a few thousand pixels a frame, no scaling, no layers, no RAM of its own but the list. */
#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

enum { SP_HEART, SP_STAR, SP_SPARK, SP_NOTE, SP_NOTE2, SP_ZED, SP_RING, SP_BFLY0, SP_BFLY1, SP_EARL, SP_EARR, SP_FLAME0, SP_FLAME1, SP_FLAME2, SP_DOT, SP_BOLT, SP_HEART2, SP_HALFL, SP_HALFR, SP_FLOWER, SP_LEAFR, SP_LEAFL, SP_RIBBON, SP_DISCO, SP_BULB, SP_SCREW, SP_PAW, SP_PADS, SP_CROWN, SP_HALO, SP_HORNL, SP_HORNR, SP_BALLOON, SP_CONE, SP_UMBRELLA, SP_HOOK, SP_CLOUD, SP_DROP, SP_WOOL, SP_SHEEP, SP_SHUR0, SP_SHUR1, SP_TOPHAT, SP_BUNNY, SP_WEED0, SP_WEED1, SP_WEED2, SP_GHOST, SP_COOKIE, SP_COOKIE2, SP_PUFF, SP_ANGER, SP_FIR, SP_FLAKE, SP_WREATH, SP_BOW,
       SP_KINDS, SP_RECT = SP_KINDS };
#define SPRITE_SIZES 6
#define SP_OWN 5
extern const int sprite_px[SPRITE_SIZES]; /* 18 28 44 64 92: a picture's side by its size 0..4 */
extern const uint8_t *const sprite_art[SP_KINDS][SPRITE_SIZES]; /* NULL: not made in this size (tools/sprites.py ART) */

extern const uint16_t sprite_own[SP_KINDS][2];

typedef struct {
    uint8_t kind, size;
    float x, y, vx, vy, ay;       /* the middle, px; px/s; gravity */
    float sway, ph;               /* drifting from side to side: px/s, and where in it */
    float life, max;              /* s left, s in all: eased in over 0.12 s, out over the last third */
    uint32_t color;
    int16_t w, h;                 /* a bar's size */
} sprite_t;

void sprite_create(lv_obj_t *parent);
/* one flying free: moves and fades by itself (NULL: none left) */
sprite_t *sprite_go(int kind, int size, float x, float y, float vx, float vy, float life, uint32_t color);
/* one held: put anew every frame in a slot of its own (0..SPRITE_PINS-1), gone once it is not (sprite_unpin) */
#define SPRITE_PINS 14
void sprite_pin(int slot, int kind, int size, float x, float y, uint32_t color);
void sprite_bar(int slot, float x, float y, int w, int h, uint32_t color);
void sprite_unpin(void);
void sprite_clear(void);          /* all of them, at once */
sprite_t *sprite_oldest(int kind); /* the one flying the longest, or NULL */
void sprite_step(float dt);
/* the board shows a face a frame after it was worked out (pip.c tick_cb): snap keeps the sprites as they are with it, paint
   shows those - so a star stays in its pupil. Where each one was and is goes to LVGL in a few joined boxes */
void sprite_snap(void);
void sprite_paint(void);
long sprite_px_drawn(void);       /* px handed to LVGL since the last call (pipsim's profile) */
