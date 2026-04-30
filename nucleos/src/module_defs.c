#include "synth_types.h"
#ifndef NATIVE_TEST
#include "ee14lib.h"
#include "dma.h"
#endif
// #include "basic.h"
#include "math.h"
#include "wavetable.h"

extern const int16_t audio_data[];
#define WINDOW_SIZE 128U
#define AUDIO_SAMPLE_RATE 44100U
// ── Forward declarations for the per-type functions ──────────────────────────
// Each module type provides 0..3 of the following: init, process, note_on.

// OSC
static void osc_init(Module *m);
static void osc_process(Module *m);
static void osc_note_on(Module *m, uint8_t n, uint8_t v);

// LFO
static void lfo_init(Module *m);
static void lfo_process(Module *m);

// MIX
static void mix_process(Module *m);

// ENV
static void env_init(Module *m);
static void env_process(Module *m);
static void env_note_on(Module *m, uint8_t n, uint8_t v);

// Filters
static void lpf_process(Module *m);
static void hpf_process(Module *m);

// Empty Module
static void empty_process(Module *m);

// ── The registry ────────────────────────────────────────────────────────────
// Designated initializers ([MOD_OSC] = {...}) keep entries aligned with the
// enum even if someone reorders things later.
const ModuleDef MODULE_DEFS[MOD_TYPE_COUNT] = {
    [MOD_EMP] = {
        .name = "---",
        .display_name = "Empty",
        .pr1_label = "---",
        .pr2_label = "---",
        .inp_default_src = SRC_LEFT,
        .inp_default_val = 0,
        .pr1_default_src = SRC_FLAT,
        .pr1_default_val = 0,
        .pr2_default_src = SRC_FLAT,
        .pr2_default_val = 0,
        .init = NULL,
        .process = empty_process,
        .note_on = NULL,
    },
    [MOD_OSC] = {
        .name = "OSC",
        .display_name = "Oscillator",
        .pr1_label = "POS",
        .pr2_label = "FRQ",
        .inp_default_src = SRC_WAVE,
        .inp_default_val = 0,
        .pr1_default_src = SRC_KNOB,
        .pr1_default_val = 0,
        .pr2_default_src = SRC_FLAT,
        .pr2_default_val = 63,
        .init = osc_init,
        .process = osc_process,
        .note_on = osc_note_on,
    },
    [MOD_MIX] = {
        .name = "MIX",
        .display_name = "Mixer",
        .pr1_label = "OTH",
        .pr2_label = "MIX",
        .inp_default_src = SRC_LEFT,
        .inp_default_val = 0,
        .pr1_default_src = SRC_FLAT,
        .pr1_default_val = 0,
        .pr2_default_src = SRC_KNOB,
        .pr2_default_val = 63,
        .init = NULL,
        .process = mix_process,
        .note_on = NULL,
    },
    [MOD_ENV] = {
        .name = "ENV",
        .display_name = "Envelope",
        .pr1_label = "ATT",
        .pr2_label = "DEC",
        .inp_default_src = SRC_FLAT,
        .inp_default_val = 127,
        .pr1_default_src = SRC_FLAT,
        .pr1_default_val = 63,
        .pr2_default_src = SRC_KNOB,
        .pr2_default_val = 63,
        .init = env_init,
        .process = env_process,
        .note_on = env_note_on,
    },
    [MOD_LPF] = {
        .name = "LPF",
        .display_name = "Low Pass",
        .pr1_label = "CUT",
        .pr2_label = "AMT",
        .inp_default_src = SRC_LEFT,
        .inp_default_val = 0,
        .pr1_default_src = SRC_FLAT,
        .pr1_default_val = 63,
        .pr2_default_src = SRC_KNOB,
        .pr2_default_val = 63,
        .init = NULL,
        .process = lpf_process,
        .note_on = NULL,
    },
    [MOD_HPF] = {
        .name = "HPF",
        .display_name = "High Pass",
        .pr1_label = "CUT",
        .pr2_label = "AMT",
        .inp_default_src = SRC_LEFT,
        .inp_default_val = 0,
        .pr1_default_src = SRC_FLAT,
        .pr1_default_val = 63,
        .pr2_default_src = SRC_KNOB,
        .pr2_default_val = 63,
        .init = NULL,
        .process = hpf_process,
        .note_on = NULL,
    },
    [MOD_LFO] = {
        .name = "LFO",
        .display_name = "Low Freq Osc",
        .pr1_label = "POS",
        .pr2_label = "FRQ",
        .inp_default_src = SRC_WAVE,
        .inp_default_val = 0,
        .pr1_default_src = SRC_KNOB,
        .pr1_default_val = 0,
        .pr2_default_src = SRC_FLAT,
        .pr2_default_val = 63,
        .init = lfo_init,
        .process = lfo_process,
        .note_on = NULL,
    },
};

// ═══════════════════════════════════════════════════════════════════════════
// Stub processing functions. Fill in real DSP later; these let everything
// compile and let you exercise the state machine before DSP is ready.
// ═══════════════════════════════════════════════════════════════════════════

static void empty_process(Module *m)
{
    // Copy input buffer straight to output.
    if (!m->buffer_out)
        return;
    for (int i = 0; i < BUFFER_SIZE; i++)
        m->buffer_out[i] = m->input_buffer[i];
}

// ── OSC ──────────────────────────────────────────────────────────────────────
static void osc_init(Module *m)
{
    m->state_a = 0; // phase accumulator
    m->state_b = 0; // current pitch in v/oct units (set on note_on)
    // m->state_c = 0; // selected wavetable
}
static void osc_note_on(Module *m, uint8_t n, uint8_t v)
{
    (void)v;
    m->state_b = n; // hold current MIDI note → v/oct under the hood
}

static void osc_process(Module *m)
{
    // Guard for nullptr
    if (!m->buffer_out)
        return;

    // Midi note as number of semitones above or below A4
    int16_t note_delta = m->state_b - 69;

    // Shift note_delta by frequency knob
    // (centered at 0, +/- 36 steps in either direction)
    note_delta += ((m->param2_value - 64) * 36 / 127);

    // Calculate frequency of note using 440 * 2^(n/12), where n is
    // the number of semitones above or below 440hz = Note A4 = Midi note 69
    float freq = 440.0f * powf(2.0f, ((float)note_delta / 12.0f));

    // Calculate step value between samples
    // freq * window_size / 44100 = samples to step per tick/DAC call
    // scale everything by 2^16 and store for later to retain decimals
    uint32_t step = (uint32_t)(freq * (float)WINDOW_SIZE * 65536.0f / 44100.0f);

    // Guard against step == 0 since we must always step a little bit
    if (step == 0)
    {
        step = 1;
    }

    // Loop through to fill buffer
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        // Get current and next indecies, and fraction of current phase
        uint32_t idx = (m->state_a >> 16) % WINDOW_SIZE;
        uint32_t next = (idx + 1) % WINDOW_SIZE;
        uint32_t phase_frac = m->state_a & 0xFFFF;

        // If source is set to wavetable, manually throw the correct samples
        // in the input buffer
        int32_t s0, s1;
        if (m->input_source == SRC_WAVE)
        {
            // Grab the wavetable instance we're using
            const Wavetable *wt = get_wavetable(m->input_value);

            // Scale window start by POS value (param1)
            uint32_t window_start = (wt->length - WINDOW_SIZE) * m->param1_value / 128;

            // Get samples direct from wavetable
            s0 = wt->samples[idx + window_start];
            s1 = wt->samples[next + window_start];
        }
        else
        {
            // Just get actual samples from input buffer
            s0 = m->input_buffer[idx % BUFFER_SIZE];
            s1 = m->input_buffer[next % BUFFER_SIZE];
        }

        // Linearly interpolate the samples using phase fraction
        m->buffer_out[i] = (int16_t)(s0 + (((s1 - s0) * (int32_t)phase_frac) >> 16));

        // Increase phase by step
        m->state_a += step % (WINDOW_SIZE << 16);
    }
}
// ── MIX ──────────────────────────────────────────────────────────────────────
static void mix_process(Module *m)
{
    // MIX blends input_buffer and param1_buffer. param2_value controls ratio.
    // value 63/64 → 50/50 mix; 0 → all input; 127 → all param1.
    if (!m->buffer_out)
        return;
    int32_t mix = m->param2_value; // 0..127
    int32_t inv = 127 - mix;
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        int32_t a = m->input_buffer[i] * inv;
        int32_t b = m->param1_buffer[i] * mix;
        m->buffer_out[i] = (int16_t)((a + b) / 127);
    }
}

// ── ENV ──────────────────────────────────────────────────────────────────────
static void env_init(Module *m)
{
    m->state_a = 0;
    m->state_c = 0; // attack decay
}
static void env_note_on(Module *m, uint8_t n, uint8_t v)
{
    (void)n;
    (void)v;
    m->state_a = 1; // stage = attacking
    m->state_c = 0; // current envelope level
}
static void env_process(Module *m)
{
    // TODO: advance envelope stage using param1_value (attack) and param2_value (decay).
    //       Write input_buffer * env_level → buffer_out.
    (void)m;
}

// ── LPF / HPF ────────────────────────────────────────────────────────────────
static void lpf_process(Module *m) { /* TODO: biquad LP, param1=cutoff, param2=wet/dry */ (void)m; }
static void hpf_process(Module *m) { /* TODO: biquad HP */ (void)m; }

// ── LFO ──────────────────────────────────────────────────────────────────────
static void lfo_init(Module *m) { m->state_a = 0; }

static void lfo_process(Module *m)
{
    // if (!m->buffer_out)
    //     return;

    // float freq = 0.01f * powf(2.0f, (m->param2_value / 127.0f) * 10.97f);
    // uint32_t step = (uint32_t)(freq * (float)WINDOW_SIZE * 65536.0f / 44100.0f);
    // if (step == 0)
    //     step = 1;

    // for (int i = 0; i < BUFFER_SIZE; i++)
    // {
    //     uint32_t idx = (m->state_a >> 16) % WINDOW_SIZE;
    //     uint32_t next = (idx + 1) % WINDOW_SIZE;
    //     uint32_t frac = m->state_a & 0xFFFF;
    //     int32_t s0 = audio_data[idx];
    //     int32_t s1 = audio_data[next];
    //     // output raw int16 — OSC handles the depth scaling
    //     m->buffer_out[i] = (int16_t)(s0 + (((s1 - s0) * (int32_t)frac) >> 16));
    //     m->state_a += step;
    // }

    // Guard for nullptr
    if (!m->buffer_out)
        return;

    // Shift note_delta by frequency knob
    // (centered at 0, +/- 36 steps in either direction)
    int16_t note = ((m->param2_value - 64) * 36 / 127) - 72;

    // Calculate frequency of note using 440 * 2^(n/12), where n is
    // the number of semitones above or below 440hz = Note A4 = Midi note 69
    // LFO EDIT: note is dropped by 72 semitones (6 octaves) to hit A-2
    float freq = 440.0f * powf(2.0f, (float)note / 12.0f);

    // Calculate step value between samples
    // freq * window_size / 44100 = samples to step per tick/DAC call
    // scale everything by 2^16 and store for later to retain decimals
    uint32_t step = (uint32_t)(freq * (float)WINDOW_SIZE * 65536.0f / 44100.0f);

    // Guard against step == 0 since we must always step a little bit
    if (step == 0)
    {
        step = 1;
    }

    // Loop through to fill buffer
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        // Get current and next indecies, and fraction of current phase
        uint32_t idx = (m->state_a >> 16) % WINDOW_SIZE;
        uint32_t next = (idx + 1) % WINDOW_SIZE;
        uint32_t phase_frac = m->state_a & 0xFFFF;

        // If source is set to wavetable, manually throw the correct samples
        // in the input buffer
        int32_t s0, s1;
        if (m->input_source == SRC_WAVE)
        {
            // Grab the wavetable instance we're using
            const Wavetable *wt = get_wavetable(m->input_value);

            // Scale window start by POS value (param1)
            uint32_t window_start = (wt->length - WINDOW_SIZE) * m->param1_value / 128;

            // Get samples direct from wavetable
            s0 = wt->samples[idx + window_start];
            s1 = wt->samples[next + window_start];
        }
        else
        {
            // Just get actual samples from input buffer
            s0 = m->input_buffer[idx % BUFFER_SIZE];
            s1 = m->input_buffer[next % BUFFER_SIZE];
        }

        // Linearly interpolate the samples using phase fraction
        m->buffer_out[i] = (int16_t)(s0 + (((s1 - s0) * (int32_t)phase_frac) >> 16));

        // Increase phase by step
        m->state_a += step % (WINDOW_SIZE << 16);
    }
}
