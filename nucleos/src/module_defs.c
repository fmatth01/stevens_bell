#include "synth_types.h"
#ifndef NATIVE_TEST
#include "ee14lib.h"
#include "dma.h"
#endif
// #include "basic.h"
#include "math.h"

extern const int16_t audio_data[];
#define AUDIO_NUM_SAMPLES 1024U
// #define AUDIO_SAMPLE_RATE 1024U
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
        .display_name = "Empty", // TODO: fill in real long-form names
        .pr1_label = "---",
        .pr2_label = "---",
        .inp_default_src = SRC_LEFT,
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
        .display_name = "LFO",
        .pr1_label = "POS",
        .pr2_label = "FRQ",
        .inp_default_src = SRC_WAVE,
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
}
static void osc_note_on(Module *m, uint8_t n, uint8_t v)
{
    (void)v;
    m->state_b = n; // hold current MIDI note → v/oct under the hood
}

static void osc_process(Module *m)
{
    if (!m->buffer_out)
        return;

    float freq = 20.0f * powf(2.0f, (m->param2_value / 127.0f) * 8.97f);
    uint32_t step = (uint32_t)(freq * (float)AUDIO_NUM_SAMPLES * 65536.0f / 44100.0f);
    if (step == 0)
        step = 1;

    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        // always read LFO from input_buffer
        // if no LFO connected, input_buffer will just be zeros (from fill_buffer default)
        int32_t lfo = m->input_buffer[i];
        int32_t depth = m->param1_value / 2; // back to subtle
        int32_t mod_step = (int32_t)step + (lfo * (int32_t)depth) / 256;
        if (mod_step < 1)
            mod_step = 1;

        uint32_t idx = (m->state_a >> 16) % AUDIO_NUM_SAMPLES;
        uint32_t next = (idx + 1) % AUDIO_NUM_SAMPLES;
        uint32_t frac = m->state_a & 0xFFFF;
        int32_t s0 = audio_data[idx];
        int32_t s1 = audio_data[next];
        m->buffer_out[i] = (int16_t)(s0 + (((s1 - s0) * (int32_t)frac) >> 16));
        m->state_a += (uint32_t)mod_step;
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
    if (!m->buffer_out)
        return;

    float freq = 0.01f * powf(2.0f, (m->param2_value / 127.0f) * 10.97f);
    uint32_t step = (uint32_t)(freq * (float)AUDIO_NUM_SAMPLES * 65536.0f / 44100.0f);
    if (step == 0)
        step = 1;

    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        uint32_t idx = (m->state_a >> 16) % AUDIO_NUM_SAMPLES;
        uint32_t next = (idx + 1) % AUDIO_NUM_SAMPLES;
        uint32_t frac = m->state_a & 0xFFFF;
        int32_t s0 = audio_data[idx];
        int32_t s1 = audio_data[next];
        // output raw int16 — OSC handles the depth scaling
        m->buffer_out[i] = (int16_t)(s0 + (((s1 - s0) * (int32_t)frac) >> 16));
        m->state_a += step;
    }
}
