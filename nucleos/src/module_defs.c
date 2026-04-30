#include "synth_types.h"
#include "ee14lib.h"
#include "dma.h"
#include "math.h"
#include <string.h> 

int _write(int file, char *data, int len) {
    serial_write(USART2, data, len);
    return len;
}

extern const int16_t audio_data[]; 
#define AUDIO_NUM_SAMPLES 1024U
//#define AUDIO_SAMPLE_RATE 1024U
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
static void env_note_off(Module *m, uint8_t n, uint8_t v);
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
        .note_off = env_note_off,
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

// !!!!!!!!!! needs to respond to midi notes
static void osc_process(Module *m) {
    if (!m->buffer_out) return;

    float freq;

    if (m->state_b > 0) {
        // MIDI note → frequency: A4 = 69 = 440 Hz
        freq = 440.0f * powf(2.0f, ((float)m->state_b - 69.0f) / 12.0f);
    } else {
        // no note playing — use param2_value as fallback
        freq = 20.0f * powf(2.0f, (m->param2_value / 127.0f) * 8.97f);
    }

    uint32_t step = (uint32_t)(freq * (float)AUDIO_NUM_SAMPLES * 65536.0f / 44100.0f);
    if (step == 0) step = 1;

    for (int i = 0; i < BUFFER_SIZE; i++) {
        int32_t lfo = m->input_buffer[i];
        int32_t depth = m->param1_value;  // remove the /4, use full 0..127
        //int32_t mod_step = (int32_t)step + (lfo * (int32_t)depth) / 256;
        int32_t mod_step = (int32_t)step + (lfo * (int32_t)depth) / 8;
        if (mod_step < 1) mod_step = 1;

        uint32_t idx  = (m->state_a >> 16) % AUDIO_NUM_SAMPLES;
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
#define ENV_IDLE     0
#define ENV_ATTACK   1
#define ENV_DECAY    2
#define ENV_SUSTAIN  3
#define ENV_RELEASE  4

#define ENV_SUSTAIN_LEVEL  80   // 0..127, hardcoded sustain level
#define ENV_RELEASE_SPEED  1    // hardcoded release speed

static void env_init(Module *m)
{
    m->state_a = ENV_IDLE;
    float zero = 0.0f;
    memcpy(&m->state_b, &zero, sizeof(float));
    m->state_c = 0;
}
static void env_note_on(Module *m, uint8_t n, uint8_t v)
{
    (void)n; (void)v;
    m->state_a = ENV_ATTACK;  // start attack from current level
    // don't reset level — allows legato-style retriggering
}
// Then:
static void env_note_off(Module *m, uint8_t n, uint8_t v)
{
    (void)n; (void)v;
    if (m->state_a != 0)  // if not idle
        m->state_a = 4;   // ENV_RELEASE
}
// Stages

static void env_process(Module *m)
{
    if (!m->buffer_out) return;

    static float level = 0.0f;

    // param1 controls attack speed: 0 = very slow, 127 = fast
    float attack_rate  = 0.00001f + (m->param1_value / 127.0f) * 0.001f;

    // param2 controls decay speed: 0 = very slow, 127 = fast
    float decay_rate   = 0.00001f + (m->param2_value / 127.0f) * 0.001f;

    // hardcoded sustain and release
    float sustain_level = 0.6f;
    float release_rate  = 0.00005f;

    for (int i = 0; i < BUFFER_SIZE; i++) {
        if (m->state_a == 1) {          // ENV_ATTACK — triggered by note on
            level += attack_rate * (1.0f - level);
            if (level >= 0.999f) {
                level = 1.0f;
                m->state_a = 2;         // move to decay
            }
        } else if (m->state_a == 2) {  // ENV_DECAY — triggered after attack completes
            level += decay_rate * (sustain_level - level);
            if (level <= sustain_level + 0.001f) {
                level = sustain_level;
                m->state_a = 3;         // move to sustain
            }
        } else if (m->state_a == 3) {  // ENV_SUSTAIN — holds until note off
            level = sustain_level;
        } else if (m->state_a == 4) {  // ENV_RELEASE — triggered by note off
            level *= (1.0f - release_rate);
            if (level <= 0.001f) {
                level = 0.0f;
                m->state_a = 0;         // back to idle
            }
        } else {                        // ENV_IDLE
            level = 0.0f;
        }

        float sample = (float)m->input_buffer[i] * level;
        if (sample >  32767.0f) sample =  32767.0f;
        if (sample < -32768.0f) sample = -32768.0f;
        m->buffer_out[i] = (int16_t)sample;
    }
}

// ── LPF / HPF ────────────────────────────────────────────────────────────────

static void lpf_process(Module *m) {
    if (!m->buffer_out) return;

    float t = m->param1_value / 127.0f;
    float alpha = 0.0001f + t * t * 0.1f;
    float wet = m->param2_value / 127.0f;
    float dry = 1.0f - wet;

    float prev;
    memcpy(&prev, &m->state_a, sizeof(float));

    for (int i = 0; i < BUFFER_SIZE; i++) {
        float x0 = (float)m->input_buffer[i];

        prev = alpha * x0 + (1.0f - alpha) * prev;

        if (prev >  32767.0f) prev =  32767.0f;
        if (prev < -32768.0f) prev = -32768.0f;

        float out = dry * x0 + wet * prev;
        if (out >  32767.0f) out =  32767.0f;
        if (out < -32768.0f) out = -32768.0f;
        m->buffer_out[i] = (int16_t)out;
    }

    memcpy(&m->state_a, &prev, sizeof(float));
}

static void hpf_process(Module *m) {
    if (!m->buffer_out) return;

    float alpha = 0.01f + (m->param1_value / 127.0f) * 0.98f;
    float wet = m->param2_value / 127.0f;

    float prev;
    memcpy(&prev, &m->state_a, sizeof(float));

    for (int i = 0; i < BUFFER_SIZE; i++) {
        float x0 = (float)m->input_buffer[i];

        prev = alpha * x0 + (1.0f - alpha) * prev;

        if (prev >  32767.0f) prev =  32767.0f;
        if (prev < -32768.0f) prev = -32768.0f;

        float hpf = x0 - prev;

        float out = x0 + wet * (hpf - x0);
        if (out >  32767.0f) out =  32767.0f;
        if (out < -32768.0f) out = -32768.0f;
        m->buffer_out[i] = (int16_t)out;
    }

    memcpy(&m->state_a, &prev, sizeof(float));
}
// ── LFO ──────────────────────────────────────────────────────────────────────
static void lfo_init(Module *m) { m->state_a = 0; }

static void lfo_process(Module *m) {
    if (!m->buffer_out) return;

    float freq = 0.01f * powf(2.0f, (m->param2_value / 127.0f) * 10.97f);
    uint32_t step = (uint32_t)(freq * (float)AUDIO_NUM_SAMPLES * 65536.0f / 44100.0f);
    if (step == 0) step = 1;

    for (int i = 0; i < BUFFER_SIZE; i++) {
        uint32_t idx  = (m->state_a >> 16) % AUDIO_NUM_SAMPLES;
        uint32_t next = (idx + 1) % AUDIO_NUM_SAMPLES;
        uint32_t frac = m->state_a & 0xFFFF;
        int32_t s0 = audio_data[idx];
        int32_t s1 = audio_data[next];
        // output raw int16 — OSC handles the depth scaling
        m->buffer_out[i] = (int16_t)(s0 + (((s1 - s0) * (int32_t)frac) >> 16));
        m->state_a += step;
    }
}