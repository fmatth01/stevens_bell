#include "synth_types.h"

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
    // TODO: phase-accumulator step driven by state_b (note) + param2_value (FRQ knob).
    //       Sample from wavetable buffer at position = param1_value / 127.
    (void)m;
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
    m->state_c = 0;
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
    // TODO: like OSC but at sub-audio rate; does not respond to notes.
    (void)m;
}
