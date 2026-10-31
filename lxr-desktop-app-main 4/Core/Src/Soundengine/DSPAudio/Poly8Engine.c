/*
 * Poly8Engine.c
 * Six polyphonic note slots driving six independently controlled oscillator layers.
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

/* A MIDI note slot is independent from the six oscillator/layer sections. */
typedef struct {
    uint8_t active, keyDown, note, velocity;
    uint32_t age;
} Poly8NoteSlot;

/* Every sounding note has a complete DSP state in every oscillator section. */
typedef struct {
    float phase, modPhase, env, releaseStart;
    float filter1, filter2;
    EnvStage stage;
} Poly8OscState;

static Poly8NoteSlot noteSlots[POLY8_VOICE_COUNT];
static Poly8OscState oscStates[POLY8_VOICE_COUNT][POLY8_VOICE_COUNT];
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

static void updateFilterCoefficient(uint8_t section) {
    float cutoff = clampf(voiceParams[section].cutoffHz, 20.0f, sampleRate * 0.42f);
    filterG[section] = 1.0f - expf(-P8_TWO_PI * cutoff / sampleRate);
}

static void sanitizeParams(Poly8Params *p) {
    if (p->waveform > POLY8_SINE) p->waveform = POLY8_SAW;
    if (p->fmWaveform > POLY8_SINE) p->fmWaveform = POLY8_SINE;
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
    memset(noteSlots, 0, sizeof(noteSlots));
    memset(oscStates, 0, sizeof(oscStates));
    sampleRate = (sr > 8000.0f && sr < 192000.0f) ? sr : 44140.0f;
    ageCounter = 0;
    masterLevel = 0.12f;
    for (uint8_t section = 0; section < POLY8_VOICE_COUNT; ++section) {
        setVoiceDefaults(&voiceParams[section]);
        updateFilterCoefficient(section);
        pitchRatio[section] = 1.0f;
    }
    for (unsigned i = 0; i < 128; ++i)
        noteFrequencies[i] = noteHz((uint8_t)i);
}

void Poly8_SetVoiceParams(uint8_t section, const Poly8Params *p) {
    if (section >= POLY8_VOICE_COUNT || !p) return;
    voiceParams[section] = *p;
    sanitizeParams(&voiceParams[section]);
    updateFilterCoefficient(section);
    pitchRatio[section] = powf(2.0f,
        (voiceParams[section].pitchSemitones + voiceParams[section].fineCents / 100.0f) / 12.0f);
}

void Poly8_GetVoiceParams(uint8_t section, Poly8Params *p) {
    if (section >= POLY8_VOICE_COUNT || !p) return;
    *p = voiceParams[section];
    p->masterLevel = masterLevel;
}

void Poly8_SetVoiceLevel(uint8_t section, float level) {
    if (section >= POLY8_VOICE_COUNT) return;
    voiceParams[section].voiceLevel = clampf(level, 0.0f, 1.0f);
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
    for (uint8_t section = 0; section < POLY8_VOICE_COUNT; ++section)
        Poly8_SetVoiceParams(section, p);
}

void Poly8_NoteOn(uint8_t note, uint8_t velocity) {
    if (velocity == 0) { Poly8_NoteOff(note); return; }

    Poly8NoteSlot *slot = NULL;
    /* Repeated Note On for the same held key retriggers that key's slot. */
    for (uint8_t i = 0; i < POLY8_VOICE_COUNT; ++i) {
        if (noteSlots[i].active && noteSlots[i].keyDown && noteSlots[i].note == note) {
            slot = &noteSlots[i];
            break;
        }
    }
    if (!slot) {
        for (uint8_t i = 0; i < POLY8_VOICE_COUNT; ++i) {
            if (!noteSlots[i].active) { slot = &noteSlots[i]; break; }
        }
    }
    if (!slot) {
        /* Reuse the oldest released note before stealing a currently held key. */
        for (uint8_t i = 0; i < POLY8_VOICE_COUNT; ++i) {
            if (!noteSlots[i].keyDown && (!slot || noteSlots[i].age < slot->age))
                slot = &noteSlots[i];
        }
    }
    if (!slot) {
        slot = &noteSlots[0];
        for (uint8_t i = 1; i < POLY8_VOICE_COUNT; ++i)
            if (noteSlots[i].age < slot->age) slot = &noteSlots[i];
    }

    const uint8_t slotIndex = (uint8_t)(slot - noteSlots);
    slot->active = 1;
    slot->keyDown = 1;
    slot->note = note;
    slot->velocity = velocity;
    slot->age = ++ageCounter;

    /* Start this note in every oscillator section: one section can play chords. */
    for (uint8_t section = 0; section < POLY8_VOICE_COUNT; ++section) {
        Poly8OscState *state = &oscStates[slotIndex][section];
        memset(state, 0, sizeof(*state));
        state->stage = ENV_ATTACK;
    }
}

void Poly8_NoteOff(uint8_t note) {
    for (uint8_t slot = 0; slot < POLY8_VOICE_COUNT; ++slot) {
        Poly8NoteSlot *n = &noteSlots[slot];
        if (n->active && n->keyDown && n->note == note) {
            n->keyDown = 0;
            for (uint8_t section = 0; section < POLY8_VOICE_COUNT; ++section) {
                Poly8OscState *state = &oscStates[slot][section];
                state->releaseStart = state->env;
                state->stage = ENV_RELEASE;
            }
        }
    }
}

void Poly8_AllNotesOff(void) {
    for (uint8_t slot = 0; slot < POLY8_VOICE_COUNT; ++slot) {
        Poly8NoteSlot *n = &noteSlots[slot];
        if (n->active && n->keyDown) {
            n->keyDown = 0;
            for (uint8_t section = 0; section < POLY8_VOICE_COUNT; ++section) {
                Poly8OscState *state = &oscStates[slot][section];
                state->releaseStart = state->env;
                state->stage = ENV_RELEASE;
            }
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

static float envStep(Poly8OscState *state, const Poly8Params *p) {
    switch (state->stage) {
        case ENV_ATTACK:
            state->env += 1.0f / (p->attackSeconds * sampleRate);
            if (state->env >= 1.0f) { state->env = 1.0f; state->stage = ENV_DECAY; }
            break;
        case ENV_DECAY:
            state->env -= (1.0f - p->sustain) / (p->decaySeconds * sampleRate);
            if (state->env <= p->sustain) {
                state->env = p->sustain;
                state->stage = ENV_SUSTAIN;
            }
            break;
        case ENV_SUSTAIN: state->env = p->sustain; break;
        case ENV_RELEASE:
            state->env -= state->releaseStart / (p->releaseSeconds * sampleRate);
            if (state->env <= 0.00001f) {
                state->env = 0.0f;
                state->stage = ENV_OFF;
            }
            break;
        case ENV_OFF:
        default: state->env = 0.0f; break;
    }
    return state->env;
}

static float renderOscillator(uint8_t slotIndex, uint8_t section) {
    Poly8NoteSlot *note = &noteSlots[slotIndex];
    Poly8OscState *state = &oscStates[slotIndex][section];
    const Poly8Params *p = &voiceParams[section];
    if (!note->active || state->stage == ENV_OFF) return 0.0f;

    float hz = noteFrequencies[note->note] * pitchRatio[section];
    hz = clampf(hz, 1.0f, sampleRate * 0.45f);
    state->phase += hz / sampleRate;
    state->modPhase += (hz * p->fmRatio) / sampleRate;
    if (state->phase >= 1.0f) state->phase -= floorf(state->phase);
    if (state->modPhase >= 1.0f) state->modPhase -= floorf(state->modPhase);

    const float envelope = envStep(state, p);
    float modWave = p->fmWaveform == POLY8_SINE
        ? sinf(P8_TWO_PI * state->modPhase)
        : oscillator(p->fmWaveform, state->modPhase);
    const float modulation = modWave * (p->fmAmount * P8_FM_MAX_INDEX);
    float carrierPhase = state->phase + modulation / P8_TWO_PI;
    carrierPhase -= floorf(carrierPhase);

    const float input = oscillator(p->waveform, carrierPhase)
        - p->resonance * 1.6f * (state->filter1 - state->filter2);
    const float g = filterG[section];
    state->filter1 += g * (input - state->filter1);
    state->filter2 += g * (state->filter1 - state->filter2);

    return state->filter2 * envelope * ((float)note->velocity / 127.0f)
        * p->voiceLevel;
}

float Poly8_RenderSample(void) {
    float mix = 0.0f;
    /* Six MIDI notes x six oscillator sections = 36 independently phased paths. */
    for (uint8_t slot = 0; slot < POLY8_VOICE_COUNT; ++slot) {
        if (!noteSlots[slot].active) continue;
        uint8_t anySectionActive = 0;
        for (uint8_t section = 0; section < POLY8_VOICE_COUNT; ++section) {
            mix += renderOscillator(slot, section);
            if (oscStates[slot][section].stage != ENV_OFF)
                anySectionActive = 1;
        }
        if (!anySectionActive)
            noteSlots[slot].active = 0;
    }
    /* Section faders are applied before summing; master is applied after. */
    return clampf(mix * masterLevel * 0.32f, -1.0f, 1.0f);
}

void Poly8_RenderBlock(int16_t *buffer, uint16_t count) {
    if (!buffer) return;
    for (uint16_t i = 0; i < count; ++i) {
        const float sample = Poly8_RenderSample();
        int32_t value = (int32_t)(sample * 32767.0f);
        if (value > 32767) value = 32767;
        if (value < -32768) value = -32768;
        buffer[i] = (int16_t)value;
    }
}
