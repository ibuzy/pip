/* The microphone: a sound level meter in dB and "someone is blowing on Pip". Pure C, no platform code:
   the host feeds it samples (firmware: the ES7210; simulator: made-up noise). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

void mic_feed(const int16_t *s, int n, int rate);
bool mic_on(void);       /* samples are coming */
void mic_stop(void);     /* the host switched the microphone off: no level, no breath until samples come again */
/* everything it hears (true: breath, music - Pip on screen), or the level alone (false: the noise meter app - no beat
   tracking, no spectrum, no breath, a fraction of the work) */
void mic_listen(bool all);
float mic_db(void);      /* about dB SPL, fast (125 ms) */
/* debug, since the last call: the loudest block (dBFS), its low share, the share of time heard as breath, the quietest
   block, the room's level, the spread and zero crossings a second around the loudest */
void mic_blow_stats(float out[8]);
float mic_peak_db(void); /* the loudest 25 ms block (dB, the meter's scale, unsmoothed) since the last call: knocks */
int mic_trace(char *out, int size); /* debug: every block since the last call as text */
bool mic_blowing(void);  /* loud, low and steady: breath on the mic */
/* Music: a beat that keeps time (65..180 per minute, in the kick or the hats, a voice over it or not) or notes held and
   moving (a song's intro with no drums yet, singing, a piano), for a few seconds - talk, a door, a passing car, a fan's
   hum are neither. mic_music: 0 none, else how sure (0..1); mic_beat_phase: 0 on a beat .. 1 just before the next;
   mic_bpm: the tempo (0 none). */
float mic_music(void);
float mic_beat_phase(void);
float mic_bpm(void);
