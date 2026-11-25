/* His birth's sky (cosmos.c): everything before his eyes. Times in s after the button. */
#pragma once
#include "lvgl.h"
#include <stdbool.h>
#define COSMOS_STAR 3.5f      /* a star falls from the rim */
#define COSMOS_SPARK 4.7f     /* ...into the middle: the spark lights */
#define COSMOS_BEATS 4
static const float cosmos_beat[COSMOS_BEATS] = {5.7f, 6.75f, 7.65f, 8.4f}; /* it beats, quicker each time */
#define COSMOS_GALAXY 8.9f    /* the stars are drawn into a galaxy round it */
#define COSMOS_FALL_S 2.5f    /* ...and this long after start falling in */
#define COSMOS_COLLAPSE 13.0f /* all of it in: it goes out */
#define COSMOS_HUSH 13.3f     /* dark, not a sound */
#define COSMOS_BANG 14.0f
#define COSMOS_END (COSMOS_BANG + 2.0f) /* his eyes, shut, where the light was */
void cosmos_create(lv_obj_t *parent);
void cosmos_eye(int i, float x, float y, float w); /* where his shut eyes will be, and how long */
/* optional (firmware): run job(ctx, 0) here and job(ctx, 1) on the other core, and return when both are done. NULL: one core */
extern void (*cosmos_parallel)(void (*job)(void *ctx, int core), void *ctx);
void cosmos_set(float t);     /* the moment to show, once a frame; t < 0: nothing */
