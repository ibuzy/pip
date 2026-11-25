/* Motion detection from the accelerometer: tilt, shake, face-down, steps. Pure C, no platform code. */
#pragma once
#include <stdbool.h>

enum { IMU_SHAKE = 1, IMU_FACE_DOWN = 2, IMU_FACE_UP = 4, IMU_STEP = 8, IMU_UPSIDE = 16, IMU_UPRIGHT = 32, IMU_FALL = 64,
       IMU_PEEK = 128,   /* face down a moment (under FACE_DOWN_S) and back: hiding, not bedtime */
       IMU_THROW = 256,  /* a free fall that a hard shove started: thrown, not dropped (IMU_FALL) */
       IMU_TURN = 512 }; /* turned over: upright -> upside down or back, at once (IMU_UPSIDE waits a second) */

typedef struct {
    float tilt_x, tilt_y; /* where "downhill" moved on the screen vs. the usual hand pose, -1..1, x right, y down */
    float energy;         /* smoothed linear acceleration, g */
    bool shaking, face_down, upside;
    int steps;
    /* internal */
    bool init, above, falling, inverted; /* inverted: upside down now, for IMU_TURN (with hysteresis) */
    float bx, by, shake_t, down_t, flip_t, fall_t, mag_lp, mag_base, t, last_step;
    float jolt; /* the hardest |a| lately, fading in a third of a second: what started a fall */
} imu_t;

/* a: acceleration in g, device axes: x right, y toward the top of the screen, z out of the screen.
   Returns IMU_* event bits. */
int imu_feed(imu_t *m, float ax, float ay, float az, float dt);
