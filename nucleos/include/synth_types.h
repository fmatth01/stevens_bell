#ifndef SYNTH_TYPES_H
#define SYNTH_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define BUFFER_SIZE 128
#define NUM_MODULES 8 // 4x2 grid

// ── Enums ────────────────────────────────────────────────────────────────────

// Source for a buffer (input, param1, or param2).
typedef enum
{
    SRC_FLAT = 0, // constant value stored in paramN_value
    SRC_KNOB,     // tracks knobs[position-1]
    SRC_LEFT,     // buffer from module to the left
    SRC_BOTM,     // buffer from module below (bottom row only reads zeros)
    SRC_WAVE,     // wavetable (OSC / LFO input only)
    SRC_COUNT
} SourceType;

// Module type — index into MODULE_DEFS[].
typedef enum
{
    MOD_EMP = 0, // slot is empty; passes input through unchanged
    MOD_OSC,
    MOD_MIX,
    MOD_ENV,
    MOD_LPF,
    MOD_HPF,
    MOD_LFO,
    MOD_TYPE_COUNT // used to reference the number of module types we have
} ModuleType;

// Which row in the module view is currently highlighted.
typedef enum
{
    SEL_INPUT = 0,
    SEL_PARAM1,
    SEL_PARAM2,
    SEL_COUNT
} SelectedParam;

// ── Module instance ──────────────────────────────────────────────────────────

typedef struct Module
{
    uint8_t type;     // ModuleType
    uint8_t position; // [0..3] bottom row, [4..7] top row

    // Input
    int16_t input_buffer[BUFFER_SIZE];
    uint8_t input_source; // SourceType

    // Param 1
    int16_t param1_buffer[BUFFER_SIZE];
    uint8_t param1_source; // SourceType
    uint8_t param1_value;  // 0..127

    // Param 2
    int16_t param2_buffer[BUFFER_SIZE];
    uint8_t param2_source;
    uint8_t param2_value;

    // Pointer to the next module's input_buffer (or the DAC out buffer for #7).
    int16_t *buffer_out;

    // Per-module scratch state (phase accum, envelope stage, filter history…)
    uint32_t state_a;
    uint32_t state_b;
    int16_t state_c;
    int16_t state_d;
    // we prolly gonna need these later
} Module;

// ── Module definition (one row of the "dictionary") ──────────────────────────
typedef struct ModuleDef
{
    const char *name;         // "OSC" — shown as [OSC] in header
    const char *display_name; // long - form name
    // const char *inp_label; // usually "INP" i dont think we need
    const char *pr1_label; // "POS", "ATT", "CUT", …
    const char *pr2_label; // "FRQ", "DEC", "AMT", …

    uint8_t inp_default_src; // SourceType
    uint8_t pr1_default_src;
    uint8_t pr1_default_val;
    uint8_t pr2_default_src;
    uint8_t pr2_default_val;

    // Lifecycle. Any of these may be NULL. no idea what these do.
    // OHHH each module may have up to all three of these, but don't need any
    // These are the function pointers
    // All functions used by any module ever will be encapsulated here
    void (*init)(Module *m);
    void (*process)(Module *m); // one buffer
    void (*note_on)(Module *m, uint8_t midi_note, uint8_t velocity);
} ModuleDef;

// The registry/dictionary. Defined in module_defs.c.
extern const ModuleDef MODULE_DEFS[MOD_TYPE_COUNT];

// dictionary access i think?
// gets one row of the dictionary/one instance of the struct?
static inline const ModuleDef *module_def(uint8_t type)
{
    return &MODULE_DEFS[type];
}

// ── Synth (top-level state) ──────────────────────────────────────────────────
typedef struct Synth
{
    bool global_view; // true = Global view, false = Module view
    bool muted;       // toggled by Play / Stop

    uint8_t knobs[8];      // MIDI values;
    uint8_t top_slider;    // cc17 — source selector in Module view
    uint8_t bottom_slider; // cc18 — value editor in Module view

    uint8_t selected_module; // 0..7 (bottom left -> top right)
    uint8_t selected_param;  // SelectedParam enum

    // Current note state. Oscillators read this as an implicit v/oct bus.
    uint8_t active_note; // 0 = no note active
    // uint8_t  active_velocity;    // we don't want velocity yet, stretch goal

    Module modules[8]; // [0..3] bottom row, [4..7] top row
} Synth;

// ── API ──────────────────────────────────────────────────────────────────────
void synth_init(Synth *s);
void synth_set_module_type(Synth *s, uint8_t slot, uint8_t type); // slot 1..8
void synth_note_on(Synth *s, uint8_t midi_note, uint8_t velocity);
void synth_note_off(Synth *s, uint8_t midi_note);
void synth_process(Synth *s); // fills DAC out buffer for one tick

#endif // SYNTH_TYPES_H
