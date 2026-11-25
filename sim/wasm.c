/* WebAssembly host for the web simulator (sim/web/index.html): the page drives time, touch, IMU and buttons. */
#include "fakephone.h"
#include "lvgl.h"
#include "mic.h"
#include "pet.h"
#include "pip.h"
#include "shell.h"
#include "theme.h"
#include "voice.h"
#include <emscripten.h>
#include <stdio.h>
#include <string.h>

#define S 466

static uint16_t fb[S * S];
static struct { bool down; int x, y; } ptr;
static struct { int n; int16_t x[3], y[3]; bool palm; } fing; /* the page's "Жесты": two, three fingers, a palm */

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) { (void)a; (void)px; lv_display_flush_ready(d); }

static void read_cb(lv_indev_t *i, lv_indev_data_t *d)
{
    (void)i;
    pip_fingers(fing.palm ? 0 : fing.n, fing.x, fing.y, fing.palm); /* every finger, as the board's touch_read */
    d->point.x = ptr.x;
    d->point.y = ptr.y;
    d->state = ptr.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

EMSCRIPTEN_KEEPALIVE void sim_init(unsigned seed)
{
    lv_init();
    lv_rand_set_seed(seed);
    lv_display_t *disp = lv_display_create(S, S);
    lv_display_set_buffers(disp, fb, NULL, sizeof fb, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_cb);
    shell_create();
    fake_phone_init();
    /* the battery comes from the page (its "Батарея" card, sim_battery), like the power chip's first reading */
}

EMSCRIPTEN_KEEPALIVE uint16_t *sim_fb(void) { return fb; }
EMSCRIPTEN_KEEPALIVE void sim_frame(int ms) { lv_tick_inc(ms); lv_timer_handler(); }
EMSCRIPTEN_KEEPALIVE void sim_touch(int x, int y, int down) { ptr.x = x; ptr.y = y; ptr.down = down; }
/* the page's gestures: n fingers (0..2; the board's panel reports two at most) at these points, or a palm (n is then ignored); the first finger is the pointer too */
EMSCRIPTEN_KEEPALIVE void sim_fingers(int n, int x0, int y0, int x1, int y1, int x2, int y2, int palm)
{
    fing.n = n < 0 ? 0 : n > 3 ? 3 : n, fing.palm = palm != 0;
    fing.x[0] = (int16_t)x0, fing.y[0] = (int16_t)y0, fing.x[1] = (int16_t)x1, fing.y[1] = (int16_t)y1, fing.x[2] = (int16_t)x2, fing.y[2] = (int16_t)y2;
    if (fing.palm) ptr.x = 233, ptr.y = 233, ptr.down = 1;
    else if (fing.n) ptr.x = x0, ptr.y = y0, ptr.down = 1;
    else ptr.down = 0;
}
EMSCRIPTEN_KEEPALIVE void sim_imu(float ax, float ay, float az, float dt) { shell_imu(ax, ay, az, dt); }
EMSCRIPTEN_KEEPALIVE int sim_display_on(void) { return pip_display_on(); }
EMSCRIPTEN_KEEPALIVE void sim_button(int id) { shell_button(id); }
EMSCRIPTEN_KEEPALIVE void sim_button_up(int id) { shell_button_up(id); }
EMSCRIPTEN_KEEPALIVE void sim_key(int k) { pip_key((char)k); }
EMSCRIPTEN_KEEPALIVE void sim_time(double unix_s, int tz_min) { shell_time((int64_t)unix_s, tz_min); }
EMSCRIPTEN_KEEPALIVE void sim_time_rebase(double unix_s) { pip_pet_rebase((int64_t)unix_s); } /* "Вернуть реальное время" */
EMSCRIPTEN_KEEPALIVE void sim_battery(int pct, int charging) { shell_battery(pct, charging != 0); } /* the page's battery and cable */
EMSCRIPTEN_KEEPALIVE void sim_wake_up(void) { pip_wake_up(); } /* switched on: the power-on intro */
EMSCRIPTEN_KEEPALIVE void sim_drained(void) { pip_drained(); }        /* run flat at 0%: food and energy lost */
EMSCRIPTEN_KEEPALIVE void sim_drained_wake(void) { pip_drained_wake(); } /* switched on after that: the intro ends angry */
EMSCRIPTEN_KEEPALIVE void sim_birthday(int day, int month) { pip_birthday(day, month); } /* the panel's "bday d m" */
EMSCRIPTEN_KEEPALIVE int sim_clock_is_known(void) { return shell_clock_is_known(); }
EMSCRIPTEN_KEEPALIVE void sim_clock_known(int known) { shell_clock_known(known != 0); } /* 0: as after a power-off with no phone yet */
EMSCRIPTEN_KEEPALIVE void sim_action(int act) { pip_action(act); }
EMSCRIPTEN_KEEPALIVE void sim_auto(int on) { pip_auto(on); }
/* persistence: JS copies the pet state to localStorage and back through this buffer */
static uint8_t pet_buf[256];
EMSCRIPTEN_KEEPALIVE uint8_t *sim_pet_buf(void) { return pet_buf; }
EMSCRIPTEN_KEEPALIVE int sim_pet_save(void)
{
    size_t n;
    const void *p = pip_pet_state(&n);
    memcpy(pet_buf, p, n);
    return (int)n;
}
EMSCRIPTEN_KEEPALIVE int sim_pet_restore(int n) { return pip_pet_restore(pet_buf, (size_t)n); }
/* dead: the page keeps the days he lived (as the board's NVS "dead"); only "Новый питомец" clears it */
EMSCRIPTEN_KEEPALIVE int sim_dead_days(void) { return pip_dead_days(); }
EMSCRIPTEN_KEEPALIVE void sim_set_dead(int days) { pip_set_dead(days); }
/* audio: the page's AudioContext pulls Pip's voice at its own sample rate */
static int16_t audio_buf[8192];
EMSCRIPTEN_KEEPALIVE void sim_voice_init(int rate) { voice_init(rate); }
EMSCRIPTEN_KEEPALIVE int16_t *sim_audio(int n) { voice_render(audio_buf, n > 8192 ? 8192 : n); return audio_buf; }
EMSCRIPTEN_KEEPALIVE void sim_say(int sound) { voice_say(sound); }
EMSCRIPTEN_KEEPALIVE int sim_voice_count(void) { return VOICE_COUNT; }
EMSCRIPTEN_KEEPALIVE const char *sim_voice_name(int i) { return voice_name(i); }
EMSCRIPTEN_KEEPALIVE void sim_volume(float v) { voice_set_volume(v); }
/* the computer's microphone (or a headset's): the page fills the buffer with 16-bit samples, then feeds them in */
static int16_t mic_buf[8192];
EMSCRIPTEN_KEEPALIVE int16_t *sim_mic_buf(void) { return mic_buf; }
EMSCRIPTEN_KEEPALIVE void sim_mic_feed(int n, int rate) /* as the board: nobody listening, the mic is off */
{
    int mode = shell_mic();
    if (!mode) { if (mic_on()) mic_stop(); return; }
    mic_listen(mode == 2);
    mic_feed(mic_buf, n > 8192 ? 8192 : n, rate);
}
EMSCRIPTEN_KEEPALIVE void sim_mic_stop(void) { mic_stop(); }
EMSCRIPTEN_KEEPALIVE float sim_get_volume(void) { return voice_volume(); }
EMSCRIPTEN_KEEPALIVE void sim_set_theme(int t) { shell_set_theme(t); }
EMSCRIPTEN_KEEPALIVE int sim_theme(void) { return theme_index(); }
EMSCRIPTEN_KEEPALIVE void sim_set_eyes(int i) { pip_eyes(i); }
EMSCRIPTEN_KEEPALIVE void sim_set_care(int i) { pip_care(i); }
EMSCRIPTEN_KEEPALIVE void sim_set_chat(int i) { pip_chat(i); }
EMSCRIPTEN_KEEPALIVE int sim_care(void) { return pip_care_get(); }
EMSCRIPTEN_KEEPALIVE int sim_eyes(void) { return pip_eyes_get(); }
EMSCRIPTEN_KEEPALIVE int sim_bright(void) { return shell_brightness_get(); } /* the Яркость app: the page dims the screen by it */
EMSCRIPTEN_KEEPALIVE void sim_set_bright(int pct) { shell_brightness(pct); }
EMSCRIPTEN_KEEPALIVE int sim_lit(void) { return strcmp(shell_where(), "torch") ? shell_brightness_get() : 100; } /* the torch: full */
EMSCRIPTEN_KEEPALIVE int sim_mood_count(void) { return PIP_MOOD_COUNT; }
EMSCRIPTEN_KEEPALIVE void sim_set_mood(int i) { pip_set_mood(i); }
EMSCRIPTEN_KEEPALIVE const char *sim_mood_name(int i) { return i >= 0 && i < PIP_MOOD_COUNT ? pip_mood_name(i) : ""; }
/* for trying out the dirt and the rare cards without waiting a day for them */
EMSCRIPTEN_KEEPALIVE void sim_set_clean(float v) { ((pet_t *)pip_pet())->clean = v < 0 ? 0 : v > 1 ? 1 : v; }
EMSCRIPTEN_KEEPALIVE void sim_rare(int tz_min) { pet_rare_now(pip_pet(), tz_min, -1); }
EMSCRIPTEN_KEEPALIVE void sim_power(int seconds) { pip_power(seconds); } /* the sleep timer, the panel's "power s" */
EMSCRIPTEN_KEEPALIVE int sim_power_after(void) { return pip_power_after(); }
EMSCRIPTEN_KEEPALIVE void sim_feature(int f, int on) { pip_feature(f, on != 0); } /* PIP_F_*, the panel's "feat i 0|1" */
EMSCRIPTEN_KEEPALIVE int sim_feature_on(int f) { return pip_feature_on(f); }
EMSCRIPTEN_KEEPALIVE void sim_home(void) { shell_home(); } /* back to his face from wherever (the panel's "Помыть") */

/* the fake iPhone: 'L' link 0/1/2, 'N' notification, 'C' call 1/0, 'M' music 1/0; 'X' the fake computer 0/1/2 */
EMSCRIPTEN_KEEPALIVE void sim_phone(int kind, int arg)
{
    fake_phone((char)kind, arg);
}
EMSCRIPTEN_KEEPALIVE const char *sim_phone_last(void) { return fake_phone_last(); }
EMSCRIPTEN_KEEPALIVE const char *sim_link_report(void) { return fake_link_report(); }
EMSCRIPTEN_KEEPALIVE int sim_phone_shots(void) { return fake_phone_shots(); }

EMSCRIPTEN_KEEPALIVE const char *sim_status(void)
{
    static char buf[560];
    pip_info_t in;
    pip_get_info(&in);
    snprintf(buf, sizeof buf,
             "{\"mood\":\"%s\",\"reaction\":\"%s\",\"steps\":%d,\"tilt\":[%.2f,%.2f],\"shake\":%.2f,\"faceDown\":%s,"
             "\"food\":%.3f,\"energy\":%.3f,\"fun\":%.3f,\"love\":%.3f,\"walk\":%.3f,\"clean\":%.3f,\"dayMood\":%.2f,"
             "\"asleep\":%s,\"auto\":%s,\"hour\":%d,\"days\":%d,\"screen\":\"%s\",\"washing\":%s,\"trust\":%.3f,\"grudge\":%.3f,\"dead\":%d}",
             in.mood, in.reaction, in.steps, in.tilt_x, in.tilt_y, in.shake, in.face_down ? "true" : "false",
             in.food, in.energy, in.fun, in.love, in.walk, in.clean, in.day_mood, in.asleep ? "true" : "false",
             in.auto_mood ? "true" : "false", in.hour, in.days, shell_where(), in.washing ? "true" : "false",
             ((const pet_t *)pip_pet())->trust, ((const pet_t *)pip_pet())->grudge, pip_dead_days());
    return buf;
}
