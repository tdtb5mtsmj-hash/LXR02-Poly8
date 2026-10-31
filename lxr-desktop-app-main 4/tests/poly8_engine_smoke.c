#include "Soundengine/DSPAudio/Poly8Engine.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define TEST_SAMPLES 4096

static int render_peak(float *peakOut) {
    float peak = 0.0f;
    for (unsigned i = 0; i < TEST_SAMPLES; ++i) {
        const float sample = Poly8_RenderSample();
        if (!isfinite(sample)) return 0;
        const float magnitude = fabsf(sample);
        if (magnitude > peak) peak = magnitude;
    }
    *peakOut = peak;
    return 1;
}

int main(void) {
    Poly8Params p;
    float peak = 0.0f;

    Poly8_Init(48000.0f);
    for (uint8_t section = 0; section < POLY8_VOICE_COUNT; ++section)
        Poly8_SetVoiceLevel(section, section == 0 ? 1.0f : 0.0f);

    /* One oscillator section must play a six-note chord by itself. */
    Poly8_NoteOn(48, 127);
    Poly8_NoteOn(52, 127);
    Poly8_NoteOn(55, 127);
    Poly8_NoteOn(59, 127);
    Poly8_NoteOn(64, 127);
    Poly8_NoteOn(67, 127);
    if (!render_peak(&peak) || peak < 0.0001f) {
        fprintf(stderr, "single-layer six-note polyphony failed (peak=%g)\n", peak);
        return 1;
    }
    for (uint8_t note = 48; note <= 67; ++note)
        Poly8_NoteOff(note);
    if (!render_peak(&peak)) return 2;

    /* LFO pitch modulation must produce finite, non-silent output. */
    Poly8_Init(48000.0f);
    Poly8_GetVoiceParams(0, &p);
    p.voiceLevel = 1.0f;
    p.lfoRateHz = 5.0f;
    p.lfoAmount = 1.0f;
    p.lfoWaveform = 0;
    p.lfoTarget = 0;
    Poly8_SetVoiceParams(0, &p);
    for (uint8_t section = 1; section < POLY8_VOICE_COUNT; ++section)
        Poly8_SetVoiceLevel(section, 0.0f);
    Poly8_NoteOn(60, 110);
    if (!render_peak(&peak) || peak < 0.0001f) {
        fprintf(stderr, "LFO-modulated layer failed (peak=%g)\n", peak);
        return 3;
    }

    puts("Poly8 smoke test passed: six-note chord on one oscillator layer; LFO path finite.");
    return 0;
}
