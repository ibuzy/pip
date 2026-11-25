#include "mic.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* dB SPL = dBFS + this. ponytail: a guess from a typical MEMS mic (-26 dBFS at 94 dB SPL) and the gain the
   firmware sets; calibrate against a phone meter app held next to it and change the one number. */
#define MIC_SPL_OFFSET 96.0f
#define HP_HZ 120.0f        /* rumble and DC out: what a studio calls silence is not the building's hum */
#define LOW_HZ 300.0f       /* breath on a mic is mostly below this */
/* Breath, tuned on the board (blown on, with pauses and in one long go; music, talk, knocks, claps near it): a breath is
   -18..-32 dBFS in a room at -78 with 30..50% of it under LOW_HZ; music and talk 8..23% low; knocks are loud but short.
   Blown on very close it overloads the mic: -4..-7 dBFS, broadband (BLOW_LOUD) */
#define BLOW_DBFS -40.0f    /* that loud... */
#define BLOW_RISE 18.0f     /* ...this far over the room (so in a loud room there is no blowing)... */
#define BLOW_LOW 0.25f      /* ...with this much of it low (or over BLOW_LOUD)... */
#define BLOW_LOUD -10.0f
#define BLOW_WIN 8          /* ...the last blocks (0.2 s) since it rose... */
#define BLOW_SPREAD 12.0f   /* ...within this many dB: steady (a breath goes at one pace)... */
#define BLOW_S 0.25f        /* ...this long */
#define BLOW_MAX 12.0f      /* s: nobody breathes out longer without a pause (8 s at most): a hair dryer goes on */

/* Music, heard two ways, either is enough:

   A beat. Every 10 ms an onset strength in each of three bands: how sharply it just got louder than it lately was (log
   energy) - the kick (under ~150 Hz, under a man's voice), the middle (chords, plucks, the voice) and the top (hats,
   snares) - kept for BT_N frames. Twice a second, from 3 s of it on, each band's curve is autocorrelated, normalized by
   its own variance, and each beat length of 65..180 a minute is scored by its meter: the beat, two beats and a bar lining
   up together (a syncopated beat that skips counts still shows by its bar). Judged in the kick, in the top and over the
   three together, the best counts: a voice over the beat clutters the middle, not the kick or the hats, so rap over a
   beat or a singer over drums no longer hides it (the middle alone, where talk lives, does not count). A mild prior for
   ~125 a minute settles a half or a double. And a beat keeps its tempo and its place: one that has not for a second yet
   counts half (a chance likeness in talk wanders off).

   Tones. Every 64 ms a 512-point spectrum (the sound taken down to ~8 kHz): its peaks well over their neighbours are
   tones, followed from one spectrum to the next (a bin either way: vibrato). Music is notes held for a while nearly all
   the time (a voice singing, piano, pads, a bass line), and new ones keep coming (a melody, the chords); talk holds a
   tone only through a vowel and its pitch never sits still, then a consonant or a pause; a fan's or a fridge's hum holds
   one for ever (the bins it sits in are left out). So: the share of the sound, over the last ~2.5 s, in tones held at
   least TONE_MIN spectra - an intro with no drums yet, a ballad, a piano, a song sung.

   Either way the tempo and the phase come from the beat curves (the best lag of the four, so he dances in time with
   what is there, even while it is only a voice and chords). Either alone, or both halfway (a loose beat with chords or a
   voice held over it: hip-hop's gaps). Switching on takes BT_HOLD evaluations in a row, off BT_HOLD_OFF: a break in a
   song or a change of beat does not stop the dance.

   The cost: a few multiply-adds a sample; three bands' autocorrelations twice a second (~400 k multiply-adds each time)
   and 16 FFTs of 512 points a second (~25 k flops each): about a percent of one core, next to the I2S it needs anyway,
   and nothing at all while the mic is closed (eco, a dark screen). ~25 KB, allocated once (on the board that goes to
   PSRAM: internal RAM is the scarce one). */
#define BT_FPS 100
#define BT_N 600                         /* 6 s */
#define BT_MIN 300                       /* judged from 3 s of it */
#define BT_LAG_LO (BT_FPS * 60 / 180)    /* 180 per minute */
#define BT_LAG_HI (BT_FPS * 60 / 65)     /* 65 per minute */
#define BT_LAGS (2 * (BT_LAG_HI - BT_LAG_LO) + 1) /* beat lengths looked at, in half frames */
#define ACF_HI (4 * BT_LAG_HI + 1)       /* the longest lag a meter reads: a bar at the slowest */
#define BT_ON 0.30f                      /* the meter's score that is music on its own */
#define BT_OFF 0.16f                     /* under this (and no tones either) it is over */
#define BT_HOLD 3                        /* evaluations in a row (half a second each) to switch on */
#define BT_HOLD_OFF 5                    /* ...and off */
#define BT_QUIET_DBFS -55.0f             /* quieter than this is no music worth dancing to */
#define BT_SCALE 2048.0f                 /* the curves are kept in int16 */
#define FT_N 512                         /* the spectrum: 512 points... */
#define FT_FPS ((float)FT_HZ / FT_N)     /* spectra a second */
#define FT_HZ 8000                       /* ...of the sound taken down to about this rate (64 ms, 15.6 Hz a bin) */
#define FT_LO_HZ 120                     /* tones looked for from here... */
#define FT_HI_HZ 3800                    /* ...to here */
#define FT_PEAK 10.0f                    /* a tone: this much over a bin three away (10 dB) */
#define TONE_MIN 3                       /* a tone counts held this many spectra in a row (~190 ms: longer than a spoken vowel's steady bit) */
#define TONE_STEADY 0.6f                 /* a bin with a tone more than this share of the last ~4 s is a hum, not a tune */
#define TONE_ON 0.40f                    /* the share of the sound in held tones lately that is music... */
#define TONE_OFF 0.28f                   /* ...and is no longer */
#define BOTH_ON 1.4f                     /* the beat's and the tones' shares of their own lines, added, that is music... */
#define BOTH_OFF 0.9f                    /* ...and is no longer */

typedef struct {
    int16_t env[3][BT_N];                /* the onset curves x BT_SCALE, rings */
    float x[3][BT_N];                    /* at an evaluation: the curves in order, their mean out */
    float r[3][ACF_HI - BT_LAG_LO + 1];  /* at an evaluation: each curve's autocorrelation */
    float meter[4][BT_LAGS];             /* each band's meter and the three together, a beat length a half frame apart */
    float re[FT_N], im[FT_N], tw[FT_N];  /* the spectrum being made; sin(2 pi k / FT_N) */
    int16_t in[FT_N];                    /* the sound for the next one */
    uint8_t run[FT_N / 2];               /* each bin: spectra in a row it has had a tone (or its neighbour had), 0 none */
    uint16_t steady[FT_N / 2];           /* each bin: how often it has had a tone lately (~4 s), 65535 always */
} heavy_t;
static heavy_t *hv;

static struct {
    int at, filled, sub, frame, hold;    /* ring write index; frames in it; samples into this frame; frames since start */
    int frame_n;                         /* samples a frame at the rate coming in */
    float lp150, lp150b, lp2k, e_all_frame; /* the band split: under ~150 Hz (twice over for the kick: steeper, less of a
                                            voice), under ~2 kHz; this frame's energy */
    float e[3], ref[3];                  /* this frame's energy in each band; the log energy each lately had */
    float level;                         /* dBFS lately */
    float o1[3], o2[3];                  /* the last two onsets of each band: each is stored blurred with its neighbours */
    int lag, beat_frame;                 /* the beat: half frames a beat, and the frame of one */
    float music;                         /* 0 none, else how sure */
    int ft_n, ft_dec, ft_sub;            /* samples gathered for the spectrum; the rate's step down to ~8 kHz; samples into a step */
    float ft_acc, ft_peak;               /* the step's sum; the loudest spectrum lately (falling) */
    float tone;                          /* the share of the sound in held tones lately */
    int ft_frames;                       /* spectra since the mic came on (counted to ~1.5 s) */
    int cand_lag, cand_at, same;         /* the last evaluation's best lag and a beat of it; evaluations in a row it held */
    bool tones;                          /* tones say music (with their own hysteresis) */
} bt;

/* the beat --------------------------------------------------------------------------------------------------------- */

static void acf_all(const float *x, int n, float *r) /* the curve against itself at every lag a meter reads, per pair */
{
    for (int l = BT_LAG_LO; l <= ACF_HI; l++) {
        float c = 0;
        if (l < n - 50) /* fewer pairs than that: mostly chance */
            for (int i = l; i < n; i++) c += x[i] * x[i - l];
        r[l - BT_LAG_LO] = l < n - 50 ? c / (float)(n - l) : 0;
    }
}

static float acf_half(const float *r, int h) /* at h half frames: a beat at 160 a minute is 37.5 frames */
{
    int l = h >> 1;
    return h & 1 ? fmaxf(r[l - BT_LAG_LO], r[l + 1 - BT_LAG_LO]) : r[l - BT_LAG_LO]; /* the curves are blurred over 3 frames */
}

static float lag_prior(int h) /* a mild pull to ~125 a minute: of a beat and its double, the likelier */
{
    float oct = log2f(120.0f * BT_FPS / h / 125) / 0.9f;
    return expf(-0.5f * oct * oct);
}

static void beat_eval(void)
{
    if (bt.filled < BT_MIN) return;
    int n = bt.filled, old = (bt.at - n + BT_N) % BT_N;
    float var[3];
    for (int b = 0; b < 3; b++) { /* the ring in order, the mean out; its autocorrelation */
        float *x = hv->x[b], mean = 0, v = 0;
        for (int i = 0, j = old; i < n; i++, j = j + 1 == BT_N ? 0 : j + 1) mean += x[i] = hv->env[b][j] / BT_SCALE;
        mean /= n;
        for (int i = 0; i < n; i++) x[i] -= mean, v += x[i] * x[i];
        var[b] = v / n;
        acf_all(x, n, hv->r[b]);
    }
    /* each band's meter at every beat length (in half frames), normalized by its own variance (a silent band scores
       nothing), and the three together */
    for (int h = 2 * BT_LAG_LO; h <= 2 * BT_LAG_HI; h++) {
        float sum = 0;
        for (int b = 0; b < 3; b++) {
            const float *r = hv->r[b];
            float s = var[b] > 1e-6f ? (acf_half(r, h) + 0.5f * r[h - BT_LAG_LO] + 0.25f * r[2 * h - BT_LAG_LO]) / (1.75f * var[b]) : 0;
            hv->meter[b][h - 2 * BT_LAG_LO] = s, sum += s;
        }
        hv->meter[3][h - 2 * BT_LAG_LO] = sum / 3;
    }
    float conf = 0;
    int lag = 0, by = 3;
    for (int c = 0; c < 4; c++) {
        if (c == 1) continue; /* the middle alone is where talk lives: it counts only with the others */
        float best = -1, best_s = 0;
        int bl = 0;
        for (int i = 0; i < BT_LAGS; i++) {
            float sc = hv->meter[c][i], w = sc * lag_prior(i + 2 * BT_LAG_LO);
            if (w > best) best = w, best_s = sc, bl = i + 2 * BT_LAG_LO;
        }
        if (c == 3) best_s *= 1.15f; /* all three agreeing is worth a little more than one band alone */
        if (best_s > conf || !lag) conf = best_s, lag = bl, by = c;
    }
    if (bt.level < BT_QUIET_DBFS) conf = 0, bt.tones = false;
    /* the phase: the offset (frames before now) where onsets every beat add up most, in the band(s) that keep it */
    float gather = -1;
    int off = 0;
    for (int o = 0; 2 * o < lag; o++) {
        float g = 0;
        for (int k = 0, i; (i = n - 1 - o - (k * lag + 1) / 2) >= 0; k++) g += by < 3 ? hv->x[by][i] : hv->x[0][i] + hv->x[1][i] + hv->x[2][i];
        if (g > gather) gather = g, off = o;
    }
    int beat_at = bt.frame - off;
    /* a beat keeps its tempo and its place: the best beat length now within ~5% of the last one's (or its double or
       half), and its beats where the last one's said they would fall. A chance likeness in talk or a door's bangs lands
       somewhere else every time. */
    int pl = bt.cand_lag, tol = lag / 20 + 1, per = lag < pl ? lag : pl, d = per ? ((2 * (beat_at - bt.cand_at)) % per + per) % per : 0;
    bool same = pl && (abs(lag - pl) <= tol || abs(lag - 2 * pl) <= tol || abs(2 * lag - pl) <= tol) && (d <= per / 8 + 1 || per - d <= per / 8 + 1);
    bt.cand_lag = lag, bt.cand_at = beat_at, bt.same = same ? bt.same + 1 : 0;
    if (bt.same < 2) conf *= 0.5f; /* a beat that has not kept its tempo and place for a second yet is only half one */
    /* each alone, or the two halfway each: a loose beat (hip-hop's gaps) with chords or a voice held over it */
    float both = conf / BT_ON + bt.tone / TONE_ON;
    bool on = bt.music > 0;
    bool want = on ? conf >= BT_OFF || bt.tones || both > BOTH_OFF : conf > BT_ON || bt.tones || both > BOTH_ON;
    bt.hold = want != on ? bt.hold + 1 : 0;
    if (bt.hold >= (on ? BT_HOLD_OFF : BT_HOLD)) on = !on, bt.hold = 0;
    if (!on) { bt.music = 0, bt.lag = 0; return; }
    float sure = fmaxf(conf / BT_ON, bt.tones ? bt.tone / TONE_ON : 0) * 0.5f;
    bt.music = sure > 1 ? 1 : sure < 0.01f ? 0.01f : sure, bt.lag = lag, bt.beat_frame = beat_at;
}

static void beat_frame(void)
{
    for (int b = 0; b < 3; b++) {
        float l = logf(bt.e[b] + 1e-9f), o = l > bt.ref[b] ? l - bt.ref[b] : 0;
        bt.ref[b] += (l - bt.ref[b]) * 0.5f, bt.e[b] = 0; /* "lately": the last frame or two */
        /* stored a frame late, blurred 1-2-1 with its neighbours: a beat that falls between two frames (160 a minute is
           37.5 of them) still lines up with itself at a whole lag */
        float v = (bt.o2[b] + 2 * bt.o1[b] + o) * 0.25f * BT_SCALE;
        hv->env[b][bt.at] = (int16_t)(v > 32767 ? 32767 : v), bt.o2[b] = bt.o1[b], bt.o1[b] = o;
    }
    float db = 10 * log10f(bt.e_all_frame / bt.frame_n + 1e-12f);
    bt.level += (db - bt.level) * 0.02f; /* about the last half second, dBFS */
    bt.e_all_frame = 0;
    bt.at = (bt.at + 1) % BT_N, bt.filled += bt.filled < BT_N;
    if (++bt.frame % (BT_FPS / 2) == 0) beat_eval();
}

/* the tones ---------------------------------------------------------------------------------------------------------- */

static void fft(float *re, float *im) /* in place, radix 2; hv->tw holds sin(2 pi k / FT_N) */
{
    for (int i = 1, j = 0; i < FT_N; i++) {
        int bit = FT_N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float t = re[i]; re[i] = re[j], re[j] = t; t = im[i], im[i] = im[j], im[j] = t; }
    }
    for (int len = 2; len <= FT_N; len <<= 1) {
        int step = FT_N / len, half = len >> 1;
        for (int i = 0; i < FT_N; i += len)
            for (int k = 0; k < half; k++) {
                float c = hv->tw[(k * step + FT_N / 4) % FT_N], s = -hv->tw[k * step]; /* e^(-2 pi i k / len) */
                float *ar = re + i + k, *ai = im + i + k, *br = re + i + k + half, *bi = im + i + k + half;
                float tr = *br * c - *bi * s, ti = *br * s + *bi * c;
                *br = *ar - tr, *bi = *ai - ti, *ar += tr, *ai += ti;
            }
    }
}

static void tone_frame(int rate)
{
    float *re = hv->re, *im = hv->im;
    for (int i = 0; i < FT_N; i++) /* Hann: sin^2(pi i / N) = (1 - cos(2 pi i / N)) / 2 */
        re[i] = hv->in[i] * (1 - hv->tw[(i + FT_N / 4) % FT_N]) * 0.5f, im[i] = 0;
    fft(re, im);
    float hz = (float)rate / bt.ft_dec / FT_N; /* a bin */
    int lo = (int)(FT_LO_HZ / hz) + 3, hi = (int)(FT_HI_HZ / hz);
    if (hi > FT_N / 2 - 4) hi = FT_N / 2 - 4;
    float all = 0, held = 0;
    for (int k = lo - 3; k <= hi + 3; k++) re[k] = re[k] * re[k] + im[k] * im[k]; /* power */
    uint8_t *run = hv->run, left = 0; /* the run of the bin below, as it was before this spectrum */
    for (int k = lo; k <= hi; k++) {
        float p = re[k];
        all += p;
        uint8_t was = run[k];
        /* over the bin three away on at least one side (the other may be the next note of a chord) */
        bool tone = p > re[k - 1] && p >= re[k + 1] && p >= FT_PEAK * fminf(re[k - 3], re[k + 3]);
        if (tone) {
            uint8_t r = left > was ? left : was;
            r = run[k + 1] > r ? run[k + 1] : r; /* the tone may have moved a bin */
            run[k] = r < 255 ? r + 1 : 255;
            /* held long enough, and not where a tone always is (a bin either way: a hum wavers between two) */
            if (run[k] >= TONE_MIN && hv->steady[k - 1] + hv->steady[k] + hv->steady[k + 1] < TONE_STEADY * 65535) held += p + re[k - 1] + re[k + 1];
        } else run[k] = 0;
        hv->steady[k] += ((tone ? 65535 : 0) - hv->steady[k]) / (int)(1.5f * FT_FPS); /* ~1.5 s */
        left = was;
    }
    /* how loud this one is next to the loudest lately: a pause between words counts as no tone at all */
    bt.ft_peak = all > bt.ft_peak ? all : bt.ft_peak * (1 - 0.46f / FT_FPS); /* falls ~2 dB a second */
    float h = all > bt.ft_peak * 0.003f && all > 0 ? fminf(1, held / all) : 0; /* within 25 dB */
    if (bt.ft_frames < 1.5f * FT_FPS) bt.ft_frames++, h = 0; /* not before a hum has had time to show as one */
    bt.tone += (h - bt.tone) / (2.5f * FT_FPS); /* ~2.5 s */
    bt.tones = bt.tone > (bt.tones ? TONE_OFF : TONE_ON);
}

/* the meter and breath ----------------------------------------------------------------------------------------------- */

static struct {
    bool on, blowing;
    float dc, hp_lp, low;    /* filter states */
    float e, e_raw, e_low;   /* energy of the current 25 ms block: for the meter, and for breath (all of it, the low part);
                                float, not double: the ESP32-S3 does double in software, and this runs 16000 times a second */
    int n, block;
    float power, db;         /* smoothed */
    float peak;              /* the loudest block since mic_peak_db() was asked, unsmoothed */
    float blow_t, calm_t;
    float floor;             /* the room's level (dBFS): down at once, up slowly, held while blown on */
    float win[BLOW_WIN];     /* the last blocks' levels: how steady it is */
    int wi, run, zc;         /* run: blocks over the room in a row; zc: sign changes of the meter's sound in this block (hiss: many; a voice: few) */
    bool pos, floor_set;
} m = {.db = 0};
static bool full = true; /* mic_listen: everything, or the level alone */
static struct { float dbfs, low, t, on, min, spread, zcr, floor; } dbg = {-99, 0, 0, 0, 0, 0, 0, 0}; /* for the debug link */
/* every block since asked (debug "mictrace 1" only): made on first use, 2 KB so it lands in PSRAM on the board
   (smaller ones stay in internal RAM, the scarce one) */
typedef struct { struct { int8_t db, f; uint8_t low, zc, on; } b[64]; int n; } trace_t;
static trace_t *trace;
int mic_trace(char *out, int size) /* "db/low%/zcr(x40)/floor/on" a block; tuning breath on the board */
{
    if (!out) { if (trace) trace->n = 0; return 0; }
    if (!trace && !(trace = calloc(1, sizeof *trace > 2048 ? sizeof *trace : 2048))) return 0;
    int k = 0;
    for (int i = 0; i < trace->n && k < size - 24; i++)
        k += snprintf(out + k, size - k, " %d/%d/%d/%d/%d", trace->b[i].db, trace->b[i].low, trace->b[i].zc, trace->b[i].f, trace->b[i].on);
    trace->n = 0;
    return k;
}

void mic_feed(const int16_t *s, int n, int rate)
{
    if (!hv && (hv = calloc(1, sizeof *hv))) /* no room for it: the meter and breath still work, music never shows */
        for (int i = 0; i < FT_N; i++) hv->tw[i] = sinf(6.2831853f * i / FT_N);
    float a_dc = 1 - expf(-2 * 3.14159f * 20 / rate), a_hp = 1 - expf(-2 * 3.14159f * HP_HZ / rate), a_low = 1 - expf(-2 * 3.14159f * LOW_HZ / rate);
    float a_150 = 1 - expf(-2 * 3.14159f * 150 / rate), a_2k = 1 - expf(-2 * 3.14159f * 2000 / rate);
    m.block = rate / 40;
    bt.frame_n = rate / BT_FPS;
    bt.ft_dec = rate / FT_HZ > 1 ? rate / FT_HZ : 1;
    m.on = true;
    for (int i = 0; i < n; i++) {
        float x = s[i] / 32768.0f;
        m.dc += (x - m.dc) * a_dc;
        float z = x - m.dc;          /* breath lives low: only the DC goes */
        m.hp_lp += (x - m.hp_lp) * a_hp;
        float y = x - m.hp_lp;       /* the meter leaves the rumble out */
        m.low += (z - m.low) * a_low;
        m.e += y * y, m.e_raw += z * z, m.e_low += m.low * m.low;
        if ((y > 0) != m.pos) m.pos = y > 0, m.zc++;
        if (hv && full) {
            /* the beat's bands: the kick under ~150 Hz (two poles, DC out), the middle to ~2 kHz, the top over it */
            bt.lp150 += (z - bt.lp150) * a_150, bt.lp150b += (bt.lp150 - bt.lp150b) * a_150, bt.lp2k += (z - bt.lp2k) * a_2k;
            float mid = bt.lp2k - bt.lp150, top = z - bt.lp2k;
            bt.e[0] += bt.lp150b * bt.lp150b, bt.e[1] += mid * mid, bt.e[2] += top * top, bt.e_all_frame += y * y;
            if (++bt.sub >= bt.frame_n) bt.sub = 0, beat_frame();
            /* the spectrum's sound: ~8 kHz, a plain average of each step (the band it looks at ends well under 4 kHz) */
            bt.ft_acc += z;
            if (++bt.ft_sub >= bt.ft_dec) {
                float v = bt.ft_acc / bt.ft_sub * 32767;
                hv->in[bt.ft_n] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v), bt.ft_acc = 0, bt.ft_sub = 0;
                if (++bt.ft_n == FT_N) bt.ft_n = 0, tone_frame(rate);
            }
        }
        if (++m.n < m.block) continue;
        /* a block: the level (RMS against full scale, so a full-scale sine is -3 dBFS), smoothed like a meter's "fast" */
        float p = m.e / m.n, low = m.e_raw > 0 ? m.e_low / m.e_raw : 0, dt = (float)m.n / rate;
        m.power += (p - m.power) * (1 - expf(-dt / 0.125f));
        m.db = 10 * log10f(m.power + 1e-12f) + MIC_SPL_OFFSET;
        if (m.db < 0) m.db = 0;
        float peak = 10 * log10f(p + 1e-12f) + MIC_SPL_OFFSET; /* this block alone: a knock is over in a block or two,
                                                                  the meter's 125 ms takes ~10 dB off it */
        if (peak > m.peak) m.peak = peak;
        float block_dbfs = 10 * log10f(m.e_raw / m.n + 1e-12f);
        if (!full) { m.e = m.e_raw = m.e_low = 0, m.n = 0, m.zc = 0; continue; } /* the level alone: no breath to look for */
        /* breath: far over the room, steady (talk, music and knocks jump about) and low (or overloading the mic) */
        if (!m.floor_set && block_dbfs > -95) m.floor_set = true, m.floor = block_dbfs; /* the room as first heard */
        float zcr = m.zc / dt, lo = block_dbfs, hi = block_dbfs;
        bool over = block_dbfs > BLOW_DBFS && block_dbfs - m.floor > BLOW_RISE;
        m.run = over ? m.run + 1 : 0;
        m.win[m.wi] = block_dbfs, m.wi = (m.wi + 1) % BLOW_WIN;
        for (int k = 1; k < (m.run < BLOW_WIN ? m.run : BLOW_WIN); k++) { /* steady since it rose: the quiet before is no spread */
            float v = m.win[(m.wi - 1 - k + 2 * BLOW_WIN) % BLOW_WIN];
            lo = fminf(lo, v), hi = fmaxf(hi, v);
        }
        bool breath = over && hi - lo < BLOW_SPREAD && (low > BLOW_LOW || block_dbfs > BLOW_LOUD);
        if (breath) m.blow_t += dt, m.calm_t = 0;
        else if ((m.calm_t += dt) > 0.15f) m.blow_t = 0; /* a breath flutters: short gaps do not end it */
        if (m.blow_t > BLOW_MAX) m.floor = block_dbfs, m.blow_t = 0; /* nobody breathes out this long: a noise, the room now */
        m.blowing = m.blow_t > BLOW_S;
        if (!m.blowing) m.floor += (block_dbfs - m.floor) * (block_dbfs < m.floor ? 0.5f : dt / (over ? 20 : 2)); /* over it: a breath not
                                                                                                                 yet sure, or a noise come on (slowly: BLOW_MAX takes it) */
        if (block_dbfs > dbg.dbfs) dbg.dbfs = block_dbfs, dbg.low = low, dbg.spread = hi - lo, dbg.zcr = zcr;
        dbg.min = fminf(dbg.min, block_dbfs), dbg.floor = m.floor;
        dbg.t += dt, dbg.on += m.blowing ? dt : 0;
        if (trace && trace->n < 64) {
            int i = trace->n++;
            trace->b[i].db = (int8_t)fmaxf(-99, block_dbfs), trace->b[i].low = (uint8_t)(low * 99), trace->b[i].zc = (uint8_t)fminf(255, zcr / 40),
            trace->b[i].f = (int8_t)fmaxf(-99, m.floor), trace->b[i].on = breath | m.blowing << 1;
        }
        m.e = m.e_raw = m.e_low = 0, m.n = 0, m.zc = 0;
    }
}

bool mic_on(void) { return m.on; }
float mic_music(void) { return bt.music; }
float mic_bpm(void) { int lag = __atomic_load_n(&bt.lag, __ATOMIC_RELAXED); return lag ? 120.0f * BT_FPS / lag : 0; }
float mic_beat_phase(void)
{
    int lag = __atomic_load_n(&bt.lag, __ATOMIC_RELAXED); /* read once: the mic's task may zero it meanwhile (a divide by 0 panics) */
    if (!lag) return 0;
    int d = bt.frame - bt.beat_frame;
    return (float)(((2 * d % lag) + lag) % lag) / lag; /* in half frames */
}
void mic_stop(void) /* no half block left over, nothing of the last song */
{
    memset(&bt, 0, sizeof bt);
    if (hv) memset(hv->env, 0, sizeof hv->env), memset(hv->run, 0, sizeof hv->run), memset(hv->steady, 0, sizeof hv->steady);
    m.on = m.blowing = m.floor_set = false, m.run = 0, m.blow_t = 0, m.power = 0, m.db = 0, m.e = m.e_raw = m.e_low = 0, m.n = 0;
}
void mic_listen(bool all)
{
    if (all == full) return;
    full = all;
    if (all) return;
    /* down to the level alone: nothing half heard is left to finish later (a breath, a song's beat) */
    memset(&bt, 0, sizeof bt);
    if (hv) memset(hv->env, 0, sizeof hv->env), memset(hv->run, 0, sizeof hv->run), memset(hv->steady, 0, sizeof hv->steady);
    m.blowing = m.floor_set = false, m.run = 0, m.blow_t = m.calm_t = 0;
}
float mic_db(void) { return m.db; }
float mic_peak_db(void)
{
    float p = m.peak;
    m.peak = 0;
    return p;
}
bool mic_blowing(void) { return m.blowing; }
void mic_blow_stats(float out[8])
{
    out[0] = dbg.dbfs, out[1] = dbg.low, out[2] = dbg.t > 0 ? dbg.on / dbg.t : 0, out[3] = dbg.min, out[4] = dbg.floor,
    out[5] = dbg.spread, out[6] = dbg.zcr, out[7] = 0;
    dbg.dbfs = -99, dbg.low = 0, dbg.t = dbg.on = 0, dbg.min = 0;
}
