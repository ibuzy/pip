/* Voice checks: every sound is audible, as loud as the rest, ends, never drives the soft clip hard (no rasp, nothing
   that grates), and sits where it should in pitch. With a path, also writes all of them in a row to a WAV to listen to.
   Run by sim/check.py. */
#include "voice.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c, msg, ...) do { if (!(c)) { fprintf(stderr, "FAIL: " msg "\n", ##__VA_ARGS__); exit(1); } } while (0)
#define RATE 16000
static int16_t pcm[RATE * 480]; /* every sound in a row with gaps, and room after them for the grids rendered past the end */

/* renders one sound; returns its length in samples, and its zero-crossing pitch estimate */
static int bb_kind = -1; /* render(VOICE_BABBLE, ...) asks for this kind of phrase */
static float bb_v, bb_a;
static int render(int sound, int16_t *out, float *pitch)
{
    if (sound == VOICE_BABBLE) voice_babble(bb_kind < 0 ? BABBLE_SAY : bb_kind, bb_v, bb_a);
    else voice_say(sound);
    int n = 0, crossings = 0, voiced = 0;
    while (voice_busy() && n < RATE * 5) {
        voice_render(out + n, 160);
        for (int i = n; i < n + 160; i++) {
            if (abs(out[i]) > 800) voiced++;
            if (i > 0 && (out[i - 1] < 0) != (out[i] < 0) && abs(out[i]) + abs(out[i - 1]) > 400) crossings++;
        }
        n += 160;
    }
    *pitch = voiced ? crossings / 2.0f / (voiced / (float)RATE) : 0;
    return n;
}

/* how loud it sounds: the RMS of its sounding 10 ms windows (those within 12 dB of its loudest), so the gaps between
   syllables and the fade-outs do not count */
static float loudness(const int16_t *x, int n)
{
    /* heard through a small speaker: what is under ~300 Hz barely comes out, so it is filtered off first */
    float win[RATE * 5 / 160], peak = 0, hp = 0, prev = 0, a = 1 / (1 + 6.2832f * 300 / RATE);
    int w = n / 160;
    for (int k = 0; k < w; k++) {
        double e = 0;
        for (int i = 0; i < 160; i++) {
            float s = x[k * 160 + i];
            hp = a * (hp + s - prev), prev = s;
            e += (double)hp * hp;
        }
        win[k] = sqrtf((float)(e / 160)), peak = fmaxf(peak, win[k]);
    }
    double e = 0;
    int m = 0;
    for (int k = 0; k < w; k++)
        if (win[k] > peak * 0.25f) e += (double)win[k] * win[k], m++;
    return m ? sqrtf((float)(e / m)) : 0;
}

int main(int argc, char **argv)
{
    voice_init(RATE);
    voice_levels(VOICE_STEPS, VOICE_STEPS); /* the sounds themselves, at full: the interface's default is 60% */
    float pitch[VOICE_COUNT], loud[VOICE_COUNT], lo = 1e9f, hi = 0;
    int total = 0;
    for (int s = 0; s < VOICE_COUNT; s++) {
        int n = render(s, pcm + total, &pitch[s]);
        loud[s] = loudness(pcm + total, n);
        for (int r = 0; r < 3; r++) { /* he never says it the same way twice: an average of a few */
            float p;
            int m = render(s, pcm + total + n, &p);
            loud[s] += loudness(pcm + total + n, m);
        }
        loud[s] /= 4;
        int peak = 0;
        for (int i = 0; i < n; i++) peak = abs(pcm[total + i]) > peak ? abs(pcm[total + i]) : peak;
        printf("%-7s %5.2f s  loudness %5.0f  pitch ~%4.0f Hz  peak %.2f\n", voice_name(s), n / (float)RATE, loud[s], pitch[s], peak / 32000.0f);
        /* the output is y / (1 + |y|): 0.7 is y = 2.3 going in, about where the squashing starts to be heard as a rasp */
        CHECK(peak < 0.7f * 32000, "%s peaks at %.2f: it drives the soft clip into a rasp (see level[] and its filter)", voice_name(s), peak / 32000.0f);
        CHECK(loud[s] > 1500, "%s is too quiet", voice_name(s));
        CHECK(n < RATE * 3, "%s does not end", voice_name(s));
        lo = fminf(lo, loud[s]), hi = fmaxf(hi, loud[s]);
        total += n;
        memset(pcm + total, 0, RATE / 2 * 2); /* half a second between sounds */
        total += RATE / 2;
    }
    printf("loudness from %.0f to %.0f (x%.2f)\n", lo, hi, hi / lo);
    /* the sounds whose shape is rolled anew each time: never a rasp over many of them, at any tone */
    const int rolled[] = {VOICE_BURP, VOICE_TOOT};
    for (int k = 0; k < 2; k++) {
        int worst = 0;
        for (int r = 0; r < 600; r++) {
            voice_tone(0, r % 3 == 0 ? 1 : r % 3 == 1 ? 0.8f : 1.25f);
            float p;
            int m = render(rolled[k], pcm, &p);
            for (int i = 0; i < m; i++) worst = abs(pcm[i]) > worst ? abs(pcm[i]) : worst;
        }
        voice_tone(0, 1);
        printf("%-7s worst peak of 600: %.2f\n", voice_name(rolled[k]), worst / 32000.0f);
        CHECK(worst < 0.7f * 32000, "%s peaks at %.2f once in a while: a rasp", voice_name(rolled[k]), worst / 32000.0f);
    }
    /* his talk over its whole range: every kind, glum to glad, calm to worked up - each as loud as the rest, never a rasp */
    static const char *const kinds[BABBLE_KINDS] = {"say", "ask", "exclaim", "complain", "sob", "woozy", "giggly", "interj"};
    float mid = sqrtf(lo * hi), blo = 1e9f, bhi = 0;
    for (int k = 0; k < BABBLE_KINDS; k++) {
        float sum = 0, kmin = 1e9f, kmax = 0;
        int cnt = 0;
        for (float vv = -1; vv <= 1.01f; vv += 0.5f)
            for (float aa = 0; aa <= 1.01f; aa += 0.5f) {
                bb_kind = k, bb_v = vv, bb_a = aa;
                float l = 0, p;
                int peak = 0;
                for (int r = 0; r < 4; r++) {
                    int n = render(VOICE_BABBLE, pcm + total, &p);
                    l += loudness(pcm + total, n);
                    for (int i = 0; i < n; i++) peak = abs(pcm[total + i]) > peak ? abs(pcm[total + i]) : peak;
                    CHECK(n < RATE * 3, "babble %s does not end", kinds[k]);
                }
                l /= 4, sum += l, cnt++, kmin = fminf(kmin, l), kmax = fmaxf(kmax, l);
                CHECK(peak < 0.7f * 32000, "babble %s (v %.1f, a %.1f) peaks at %.2f: a rasp", kinds[k], vv, aa, peak / 32000.0f);
            }
        printf("babble %-8s loudness %5.0f (%5.0f..%5.0f)\n", kinds[k], sum / cnt, kmin, kmax);
        blo = fminf(blo, kmin), bhi = fmaxf(bhi, kmax);
    }
    printf("babble from %.0f to %.0f against the sounds' %.0f\n", blo, bhi, mid);
    CHECK(blo > mid / 1.35f && bhi < mid * 1.35f, "the babble should be about as loud as the other sounds (%.0f..%.0f vs %.0f): see babble_level[]", blo, bhi, mid);
    bb_kind = -1;
    /* coloured by how he feels (voice_tone): up or down, brisk or slow - still no rasp, still about as loud */
    for (float sh = -3; sh <= 3.01f; sh += 3)
        for (float tp = 0.8f; tp <= 1.26f; tp += 0.45f) {
            voice_tone(sh, tp);
            for (int s2 = 0; s2 < VOICE_BABBLE; s2++) {
                float p;
                int n = render(s2, pcm + total, &p), peak = 0;
                for (int i = 0; i < n; i++) peak = abs(pcm[total + i]) > peak ? abs(pcm[total + i]) : peak;
                float l = loudness(pcm + total, n);
                CHECK(peak < 0.72f * 32000, "%s toned (%+.0f st, x%.2f) peaks at %.2f", voice_name(s2), sh, tp, peak / 32000.0f);
                CHECK(l > mid / 1.6f && l < mid * 1.6f, "%s toned (%+.0f st, x%.2f) is %.0f against %.0f", voice_name(s2), sh, tp, l, mid);
            }
        }
    voice_tone(0, 1);
    CHECK(hi / lo < 1.3f, "the sounds should be about as loud as each other (x%.2f): see level[] in ui/voice.c", hi / lo);
    CHECK(pitch[VOICE_HAPPY] > pitch[VOICE_SAD] * 1.4f, "happy should sit well above sad");
    CHECK(pitch[VOICE_GROWL] < pitch[VOICE_HAPPY] * 0.7f, "a growl is low");
    for (int k = VOICE_NOTE0; k < VOICE_NOTE3; k++) CHECK(pitch[k + 1] > pitch[k] * 1.1f, "the notes climb: %s under %s", voice_name(k), voice_name(k + 1));
    voice_set_volume(0);
    int16_t quiet[1600];
    voice_say(VOICE_HAPPY);
    bool any = false;
    for (int i = 0; i < 20; i++) any |= voice_render(quiet, 1600);
    CHECK(!any, "mute is silent");

    if (argc > 1) { /* 16-bit mono WAV */
        FILE *f = fopen(argv[1], "wb");
        uint32_t data = (uint32_t)total * 2, riff = 36 + data, fmt = 16, rate = RATE, bps = RATE * 2;
        uint16_t pcmfmt = 1, ch = 1, align = 2, bits = 16;
        fwrite("RIFF", 1, 4, f), fwrite(&riff, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f), fwrite(&fmt, 4, 1, f);
        fwrite(&pcmfmt, 2, 1, f), fwrite(&ch, 2, 1, f), fwrite(&rate, 4, 1, f), fwrite(&bps, 4, 1, f);
        fwrite(&align, 2, 1, f), fwrite(&bits, 2, 1, f), fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f);
        fwrite(pcm, 2, (size_t)total, f);
        fclose(f);
    }
    puts("voice_test ok");
    return 0;
}
