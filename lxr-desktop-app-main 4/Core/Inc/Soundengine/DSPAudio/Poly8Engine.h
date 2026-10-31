/*
 * Poly8Engine.h
 * Six-slot polyphonic virtual-analog/FM engine.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef LXR_POLY8_ENGINE_H
#define LXR_POLY8_ENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POLY8_VOICE_COUNT 6

typedef enum {
    POLY8_SAW = 0,
    POLY8_SQUARE = 1,
    POLY8_TRIANGLE = 2,
    POLY8_SINE = 3
} Poly8Waveform;

typedef struct {
    Poly8Waveform waveform;
    float voiceLevel;
    float attackSeconds;
    float decaySeconds;
    float sustain;
    float releaseSeconds;
    float cutoffHz;
    float resonance;
    float fmAmount;       /* 0..1, modulation index scaled internally */
    float fmRatio;        /* modulator frequency / carrier frequency */
    float pitchSemitones; /* per-voice coarse pitch offset */
    float fineCents;      /* per-voice fine pitch offset */
    Poly8Waveform fmWaveform;
    float lfoRateHz;      /* per-layer LFO rate, 0.05..20 Hz */
    float lfoAmount;      /* per-layer LFO depth, 0..1 */
    uint8_t lfoWaveform;  /* 0=sine, 1=triangle, 2=saw, 3=square */
    uint8_t lfoTarget;    /* 0=pitch, 1=cutoff, 2=FM amount */
    float masterLevel;    /* used by the legacy Poly8_SetParams API */
} Poly8Params;

void Poly8_Init(float sampleRate);
/* Legacy setter: applies the supplied voice parameters to all six slots. */
void Poly8_SetParams(const Poly8Params *params);
void Poly8_SetVoiceParams(uint8_t voice, const Poly8Params *params);
void Poly8_GetVoiceParams(uint8_t voice, Poly8Params *params);
void Poly8_SetVoiceLevel(uint8_t voice, float level);
void Poly8_SetMasterLevel(float level);
float Poly8_GetMasterLevel(void);
void Poly8_NoteOn(uint8_t midiNote, uint8_t velocity);
void Poly8_NoteOff(uint8_t midiNote);
void Poly8_AllNotesOff(void);
float Poly8_RenderSample(void);
void Poly8_RenderBlock(int16_t *buffer, uint16_t count);

#ifdef __cplusplus
}
#endif
#endif
