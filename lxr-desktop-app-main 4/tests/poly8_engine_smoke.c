#include "Soundengine/DSPAudio/Poly8Engine.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define TEST_SAMPLES 8192u

typedef struct {
    float peak;
    float rms;
} RenderStats;

static int render_stats(RenderStats *stats) {
    double sumSquares = 0.0;
    float peak = 0.0f;
    for (unsigned i = 0; i < TEST_SAMPLES; ++i) {
        const float sample = Poly8_RenderSample();
        if (!isfinite(sample)) return 0;
        const float magnitude = fabsf(sample);
        if (magnitude > peak) peak = magnitude;
        sumSquares += (double)sample * (double)sample;
    }
    stats->peak = peak;
    stats->rms = (float)sqrt(sumSquares / (double)TEST_SAMPLES);
    return 1;
}

static float render_one_layer(float voiceLevel, float masterLevel) {
    RenderStats stats = {0};
    Poly8_Init(48000.0f);
    for (uint8_t section = 0; section < POLY8_VOICE_COUNT; ++section)
        Poly8_SetVoiceLevel(section, section == 0 ? voiceLevel : 0.0f);
    Poly8_SetMasterLevel(masterLevel);
    Poly8_NoteOn(60, 127);
    if (!render_stats(&stats)) return -1.0f;
    return stats.rms;
}

int main(void) {
    Poly8Params p;
    RenderStats stats = {0};

    /* Six-note chord must play through one independently enabled layer. */
    Poly8_Init(48000.0f);
    for (uint8_t section = 0; section < POLY8_VOICE_COUNT; ++section)
        Poly8_SetVoiceLevel(section, section == 0 ? 1.0f : 0.0f);
    Poly8_NoteOn(48, 127);
    Poly8_NoteOn(52, 127);
    Poly8_NoteOn(55, 127);
    Poly8_NoteOn(59, 127);
    Poly8_NoteOn(64, 127);
    Poly8_NoteOn(67, 127);
    if (!render_stats(&stats) || stats.rms < 0.0001f) {
        fprintf(stderr, "single-layer six-note polyphony failed (rms=%g)\n", stats.rms);
        return 1;
    }

    /* Individual fader must be a true linear gain, not a crossfade normalization. */
    const float fullLevel = render_one_layer(1.0f, 0.5f);
    const float quarterLevel = render_one_layer(0.25f, 0.5f);
    if (fullLevel <= 0.0f || quarterLevel <= 0.0f ||
        fabsf((quarterLevel / fullLevel) - 0.25f) > 0.015f) {
        fprintf(stderr, "voice fader is not linear (full=%g quarter=%g)\n",
            fullLevel, quarterLevel);
        return 2;
    }

    /* Updating synthesis parameters must not reset a live hardware fader. */
    Poly8_Init(48000.0f);
    Poly8_SetVoiceLevel(0, 0.25f);
    Poly8_GetVoiceParams(0, &p);
    p.cutoffHz = 2400.0f;
    Poly8_SetVoiceParams(0, &p);
    Poly8_GetVoiceParams(0, &p);
    if (fabsf(p.voiceLevel - 0.25f) > 0.0001f) {
        fprintf(stderr, "parameter update changed hardware fader gain (%g)\\n", p.voiceLevel);
        return 7;
    }

    /* The dedicated master fader is applied after the six layer gains. */
    const float masterHalf = render_one_layer(1.0f, 0.5f);
    const float masterQuarter = render_one_layer(1.0f, 0.25f);
    if (masterHalf <= 0.0f || masterQuarter <= 0.0f ||
        fabsf((masterQuarter / masterHalf) - 0.5f) > 0.015f) {
        fprintf(stderr, "master gain is not linear (half=%g quarter=%g)\n",
            masterHalf, masterQuarter);
        return 3;
    }

    /* Muting all six hardware faders must silence the Poly8 output. */
    Poly8_Init(48000.0f);
    for (uint8_t section = 0; section < POLY8_VOICE_COUNT; ++section)
        Poly8_SetVoiceLevel(section, 0.0f);
    Poly8_NoteOn(60, 127);
    if (!render_stats(&stats) || stats.peak > 0.000001f) {
        fprintf(stderr, "all-muted output is not silent (peak=%g)\n", stats.peak);
        return 4;
    }

    /* LFO pitch modulation must produce finite, non-silent output. */
    Poly8_Init(48000.0f);
    Poly8_GetVoiceParams(0, &p);
    p.voiceLevel = 1.0f;
    p.lfoRateHz = 5.0f;
    p.lfoAmount = 1.0f;
    p.lfoWaveform = POLY8_SINE;
    p.lfoTarget = 0;
    Poly8_SetVoiceParams(0, &p);
    for (uint8_t section = 1; section < POLY8_VOICE_COUNT; ++section)
        Poly8_SetVoiceLevel(section, 0.0f);
    Poly8_NoteOn(60, 110);
    if (!render_stats(&stats) || stats.rms < 0.0001f) {
        fprintf(stderr, "LFO-modulated layer failed (rms=%g)\n", stats.rms);
        return 5;
    }

    /* Full six-layer, six-note load must remain finite and bounded. */
    Poly8_Init(48000.0f);
    for (uint8_t section = 0; section < POLY8_VOICE_COUNT; ++section)
        Poly8_SetVoiceLevel(section, 1.0f);
    for (uint8_t note = 48; note < 54; ++note)
        Poly8_NoteOn(note, 127);
    if (!render_stats(&stats) || stats.peak > 1.0f || stats.rms < 0.0001f) {
        fprintf(stderr, "36-path stress render failed (peak=%g rms=%g)\n",
            stats.peak, stats.rms);
        return 6;
    }

    puts("Poly8 smoke test passed: six-note polyphony, linear voice/master gains, mute, LFO and 36-path stress.");
    return 0;
}
