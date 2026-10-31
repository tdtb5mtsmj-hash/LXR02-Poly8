/*
 * Poly8Engine.c
 * Six-slot polyphonic virtual-analog/FM engine.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "Soundengine/DSPAudio/Poly8Engine.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

#define P8_PI 3.14159265358979323846f
#define P8_TWO_PI (2.0f * P8_PI)
#define P8_FM_MAX_INDEX 8.0f

typedef enum { ENV_OFF, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE } EnvStage;

typedef struct {
    uint8_t active, keyDown, note, velocity;
    uint32_t age;
    float phase, modPhase, env, releaseStart;
    float filter1, filter2;
    EnvStage stage;
} Poly8Voice;

static Poly8Voice voices[POLY8_VOICE_COUNT];
static Poly8Params voiceParams[POLY8_VOICE_COUNT];
static float masterLevel = 0.12f;
static float sampleRate = 44140.0f;
static uint32_t ageCounter;
static float noteFrequencies[128];
static float filterG[POLY8_VOICE_COUNT];
static float pitchRatio[POLY8_VOICE_COUNT];

static float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static float noteHz(uint8_t note) {
    return 440.0f * powf(2.0f, ((float)note - 69.0f) / 12.0f);
}

static void setVoiceDefaults(Poly8Params *p) {
    p->waveform = POLY8_SAW;
    p->voiceLevel = 1.0f;
    p->attackSeconds = 0.008f;
    p->decaySeconds = 0.12f;
    p->sustain = 0.72f;
    p->releaseSeconds = 0.18f;
    p->cutoffHz = 8000.0f;
    p->resonance = 0.12f;
    p->fmAmount = 0.0f;
    p->fmRatio = 2.0f;
    p->pitchSemitones = 0.0f;
    p->fineCents = 0.0f;
    p->fmWaveform = POLY8_SINE;
    p->masterLevel = 0.12f;
}

static void updateFilterCoefficient(uint8_t voice) {
    float cutoff = clampf(voiceParams[voice].cutoffHz, 20.0f, sampleRate * 0.42f);
    filterG[voice] = 1.0f - expf(-P8_TWO_PI * cutoff / sampleRate);
}

static void sanitizeParams(Poly8Params *p) {
    if (p->waveform > POLY8_SINE) p->waveform = POLY8_SAW;
    if (p->fmWaveform > POLY8_TRIANGLE && p->fmWaveform != POLY8_SINE)
        p->fmWaveform = POLY8_SINE;
    p->voiceLevel = clampf(p->voiceLevel, 0.0f, 1.0f);
    p->attackSeconds = clampf(p->attackSeconds, 0.001f, 10.0f);
    p->decaySeconds = clampf(p->decaySeconds, 0.001f, 10.0f);
    p->sustain = clampf(p->sustain, 0.0f, 1.0f);
    p->releaseSeconds = clampf(p->releaseSeconds, 0.001f, 10.0f);
    p->cutoffHz = clampf(p->cutoffHz, 20.0f, sampleRate * 0.42f);
    p->resonance = clampf(p->resonance, 0.0f, 0.8f);
    p->fmAmount = clampf(p->fmAmount, 0.0f, 1.0f);
    p->fmRatio = clampf(p->fmRatio, 0.125f, 16.0f);
    p->pitchSemitones = clampf(p->pitchSemitones, -48.0f, 48.0f);
    p->fineCents = clampf(p->fineCents, -100.0f, 100.0f);
}

void Poly8_Init(float sr) {
    memset(voices, 0, sizeof(voices));
    sampleRate = (sr > 8000.0f && sr < 192000.0f) ? sr : 44140.0f;
    ageCounter = 0;
    masterLevel = 0.12f;
    for (uint8_t i = 0; i < POLY8_VOICE_COUNT; ++i) {
        setVoiceDefaults(&voiceParams[i]);
        updateFilterCoefficient(i);
        pitchRatio[i] = 1.0f;
    }
    for (unsigned i = 0; i < 128; ++i)
        noteFrequencies[i] = noteHz((uint8_t)i);
}

void Poly8_SetVoiceParams(uint8_t voice, const Poly8Params *p) {
    if (voice >= POLY8_VOICE_COUNT || !p) return;
    voiceParams[voice] = *p;
    sanitizeParams(&voiceParams[voice]);
    updateFilterCoefficient(voice);
    pitchRatio[voice] = powf(2.0f, (voiceParams[voice].pitchSemitones + voiceParams[voice].fineCents / 100.0f) / 12.0f);
}

void Poly8_GetVoiceParams(uint8_t voice, Poly8Params *p) {
    if (voice >= POLY8_VOICE_COUNT || !p) return;
    *p = voiceParams[voice];
    p->masterLevel = masterLevel;
}

void Poly8_SetVoiceLevel(uint8_t voice, float level) {
    if (voice >= POLY8_VOICE_COUNT) return;
    voiceParams[voice].voiceLevel = clampf(level, 0.0f, 1.0f);
}

void Poly8_SetMasterLevel(float level) {
    masterLevel = clampf(level, 0.0f, 0.5f);
}

float Poly8_GetMasterLevel(void) {
    return masterLevel;
}

void Poly8_SetParams(const Poly8Params *p) {
    if (!p) return;
    Poly8_SetMasterLevel(p->masterLevel);
    for (uint8_t i = 0; i < POLY8_VOICE_COUNT; ++i)
        Poly8_SetVoiceParams(i, p);
}

void Poly8_NoteOn(uint8_t note, uint8_t velocity) {
    if (velocity == 0) { Poly8_NoteOff(note); return; }
    Poly8Voice *v = NULL;
    for (unsigned i = 0; i < POLY8_VOICE_COUNT; ++i) {
        if (voices[i].active && voices[i].keyDown && voices[i].note == note) {
            v = &voices[i]; break;
        }
    }
    if (!v) {
        /* Prefer a truly free voice before touching any sounding note. */
        for (unsigned i = 0; i < POLY8_VOICE_COUNT; ++i) {
            if (!voices[i].active) { v = &voices[i]; break; }
        }
    }
    if (!v) {
        /* Reclaim the oldest release-tail voice before stealing a held key. */
        for (unsigned i = 0; i < POLY8_VOICE_COUNT; ++i) {
            if (voices[i].active && !voices[i].keyDown &&
                (!v || voices[i].age < v->age)) {
                v = &voices[i];
            }
        }
    }
    if (!v) {
        /* All six voices are held: use oldest-note voice stealing. */
        v = &voices[0];
        for (unsigned i = 1; i < POLY8_VOICE_COUNT; ++i)
            if (voices[i].age < v->age) v = &voices[i];
    }
    memset(v, 0, sizeof(*v));
    v->active = 1; v->keyDown = 1; v->note = note; v->velocity = velocity;
    v->age = ++ageCounter; v->stage = ENV_ATTACK;
}

void Poly8_NoteOff(uint8_t note) {
    for (unsigned i = 0; i < POLY8_VOICE_COUNT; ++i) {
        Poly8Voice *v = &voices[i];
        if (v->active && v->keyDown && v->note == note) {
            v->keyDown = 0; v->releaseStart = v->env; v->stage = ENV_RELEASE;
        }
    }
}

void Poly8_AllNotesOff(void) {
    for (unsigned i = 0; i < POLY8_VOICE_COUNT; ++i) {
        Poly8Voice *v = &voices[i];
        if (v->active && v->keyDown) {
            v->keyDown = 0; v->releaseStart = v->env; v->stage = ENV_RELEASE;
        }
    }
}

static float oscillator(Poly8Waveform waveform, float phase) {
    switch (waveform) {
        case POLY8_SQUARE: return phase < 0.5f ? 1.0f : -1.0f;
        case POLY8_TRIANGLE: return 1.0f - 4.0f * fabsf(phase - 0.5f);
        case POLY8_SINE: return sinf(P8_TWO_PI * phase);
        case POLY8_SAW:
        default: return 2.0f * phase - 1.0f;
    }
}

static float envStep(Poly8Voice *v, const Poly8Params *p) {
    switch (v->stage) {
        case ENV_ATTACK:
            v->env += 1.0f / (p->attackSeconds * sampleRate);
            if (v->env >= 1.0f) { v->env = 1.0f; v->stage = ENV_DECAY; }
            break;
        case ENV_DECAY:
            v->env -= (1.0f - p->sustain) / (p->decaySeconds * sampleRate);
            if (v->env <= p->sustain) { v->env = p->sustain; v->stage = ENV_SUSTAIN; }
            break;
        case ENV_SUSTAIN: v->env = p->sustain; break;
        case ENV_RELEASE:
            v->env -= v->releaseStart / (p->releaseSeconds * sampleRate);
            if (v->env <= 0.00001f) {
                v->env = 0.0f; v->stage = ENV_OFF; v->active = 0;
            }
            break;
        case ENV_OFF:
        default: v->env = 0.0f; break;
    }
    return v->env;
}

static float renderVoice(uint8_t index) {
    Poly8Voice *v = &voices[index];
    const Poly8Params *p = &voiceParams[index];
    if (!v->active) return 0.0f;

    float hz = noteFrequencies[v->note] * pitchRatio[index];
    hz = clampf(hz, 1.0f, sampleRate * 0.45f);
    v->phase += hz / sampleRate;
    v->modPhase += (hz * p->fmRatio) / sampleRate;
    if (v->phase >= 1.0f) v->phase -= floorf(v->phase);
    if (v->modPhase >= 1.0f) v->modPhase -= floorf(v->modPhase);

    float e = envStep(v, p);
    if (!v->active) return 0.0f;

    float modWave;
    if (p->fmWaveform == POLY8_SINE)
        modWave = sinf(P8_TWO_PI * v->modPhase);
    else
        modWave = oscillator(p->fmWaveform, v->modPhase);
    float mod = modWave * (p->fmAmount * P8_FM_MAX_INDEX);
    float carrierPhase = v->phase + (mod / P8_TWO_PI);
    carrierPhase -= floorf(carrierPhase);
    float input = oscillator(p->waveform, carrierPhase)
        - p->resonance * 1.6f * (v->filter1 - v->filter2);
    float g = filterG[index];
    v->filter1 += g * (input - v->filter1);
    v->filter2 += g * (v->filter1 - v->filter2);
    return v->filter2 * e * ((float)v->velocity / 127.0f) * p->voiceLevel;
}

float Poly8_RenderSample(void) {
    float mix = 0.0f;
    for (uint8_t i = 0; i < POLY8_VOICE_COUNT; ++i)
        mix += renderVoice(i);
    /* Individual levels are applied before summing; master is applied after. */
    return clampf(mix * masterLevel * 0.32f, -1.0f, 1.0f);
}

void Poly8_RenderBlock(int16_t *buffer, uint16_t count) {
    if (!buffer) return;
    for (uint16_t i = 0; i < count; ++i) {
        float sample = Poly8_RenderSample();
        int32_t value = (int32_t)(sample * 32767.0f);
        if (value > 32767) value = 32767;
        if (value < -32768) value = -32768;
        buffer[i] = (int16_t)value;
    }
}
