/* Dust on Pip's glass and the sponge that washes it off (on his screen the bottom key tapped, then held). */
#pragma once
#include "lvgl.h"
#include <stdbool.h>

void grime_create(lv_obj_t *parent); /* over everything on his screen: call last */
void grime_dust(float level);        /* 0 none .. 1 filthy: specks, then grit and hairs, then smudges, more and bigger */
void grime_scrub(bool on);           /* the sponge comes in and scrubs, lathering, until off; then slides away */
void grime_fling(void);              /* he will not have it: the sponge is flung off sideways */
void grime_sparkle(void);            /* squeaky clean: a few glints pop */
void grime_step(float dt);           /* moves what is moving; does nothing when nothing is */
