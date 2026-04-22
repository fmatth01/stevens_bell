#include "synth_types.h"
#include <string.h>

// Process order: bottom row first (left → right), then top row (left → right).
// This guarantees LEFT and BOTM buffers are valid before any module reads them.
// Order is {0, 1, 2, 3, 4, 5, 6, 7}

// Zero buffer used for LEFT/BOTM sources that point off the grid.
static int16_t ZERO_BUFFER[BUFFER_SIZE];

// Final DAC out buffer lives here; top-right module (slot 7) writes into it.
static int16_t DAC_OUT[BUFFER_SIZE];

// bottom[3] has no right chain neighbor; top[7] reads its output via SRC_BOTM.
static int16_t BOTM3_OUT[BUFFER_SIZE];

// ── Helpers ──────────────────────────────────────────────────────────────────

// Neighbor lookup. slot is 0..7. Returns NULL if off-grid.
static Module *neighbor_left(Synth *s, uint8_t slot)
{
    // LEFT means "previous in the same row."
    if (slot == 0 || slot == 4)
        return NULL; // leftmost column
    return &s->modules[slot - 1];
}
static Module *neighbor_bottom(Synth *s, uint8_t slot)
{
    if (slot < 4)
        return NULL; // bottom row has no bottom neighbor
    return &s->modules[slot - 4]; // top slot s → bottom slot s-4
}

// Fill a destination buffer from the given source. For FLAT/KNOB we write the
// scalar across the whole buffer; for LEFT/BOTM we copy the neighbor's output;
// for WAVE we expect the wavetable subsystem to have pre-filled it,
// so we leave it alone.
static void fill_buffer(Synth *s, Module *m, int16_t *dst,
                        uint8_t source, uint8_t scalar_value)
{
    switch (source)
    {
    case SRC_FLAT:
    {
        // Scale flat value to 16-bit sample
        // Scale 0..127 → 0..32767, aka ~max_val of int16_t
        int16_t v = (int16_t)(scalar_value * 258); // ~ *32767/127
        for (int i = 0; i < BUFFER_SIZE; i++)
            dst[i] = v; // Fill every element in the buffer with the same value
        break;
    }
    case SRC_KNOB:
    {
        // Get value of appropriate knob in knobs array
        uint8_t k = s->knobs[m->position];
        // Scale flat value to 16-bit sample
        // Scale 0..127 → 0..32767, aka ~max_val of int16_t
        int16_t v = (int16_t)(k * 258);
        for (int i = 0; i < BUFFER_SIZE; i++)
            dst[i] = v; // Fill every element in the buffer with the same value
        break;
    }
    case SRC_LEFT:
    {
        /* THIS SHOULD BE REWRITTEN to change the output buffer pointer of the
        module to the left to be the appropriate buffer of this module.
        this might be a little more complex but should save some memory and a
        bunch of memcpy calls every time, since it's just directly written. */
        Module *nb = neighbor_left(s, m->position);
        const int16_t *src = nb ? nb->buffer_out : ZERO_BUFFER;
        memcpy(dst, src, sizeof(int16_t) * BUFFER_SIZE);
        break;
    }
    case SRC_BOTM:
    {
        /* same rewrite for this one */
        Module *nb = neighbor_bottom(s, m->position);
        const int16_t *src = nb ? nb->buffer_out : ZERO_BUFFER;
        memcpy(dst, src, sizeof(int16_t) * BUFFER_SIZE);
        break;
    }
    case SRC_WAVE:
        // Assume wavetable loader has already filled this buffer.
        break;
    default:
        // Fill with zeros. rewrite possible: could also be case where left/botm doesn't work
        memset(dst, 0, sizeof(int16_t) * BUFFER_SIZE);
        break;
    }
}

// Reset a single module to its type's defaults.
static void module_reset(Module *m, uint8_t type, uint8_t position)
{
    const ModuleDef *def = module_def(type);
    memset(m, 0, sizeof(*m));
    m->type = type;
    m->position = position;
    m->input_source = def->inp_default_src;
    m->param1_source = def->pr1_default_src;
    m->param1_value = def->pr1_default_val;
    m->param2_source = def->pr2_default_src;
    m->param2_value = def->pr2_default_val;
    if (def->init)
        def->init(m);
}

// ── Public API ───────────────────────────────────────────────────────────────

void synth_init(Synth *s)
{
    memset(s, 0, sizeof(*s));
    s->global_view = true;
    s->selected_module = 7; // top right corner
    s->selected_param = SEL_INPUT;

    for (uint8_t i = 0; i < NUM_MODULES; i++)
    {
        module_reset(&s->modules[i], MOD_EMP, i);
        Module *m = &s->modules[i];
        if (i < 3)           // bottom row 0,1,2 → chain right; top row reads via SRC_BOTM
            m->buffer_out = s->modules[i + 1].input_buffer;
        else if (i == 3)     // bottom-right: no right neighbor; top[7] reads this via SRC_BOTM
            m->buffer_out = BOTM3_OUT;
        else if (i < 7)      // top row 4,5,6 → chain right
            m->buffer_out = s->modules[i + 1].input_buffer;
        else                 // top-right (7) → DAC
            m->buffer_out = DAC_OUT;
    }
}

void synth_set_module_type(Synth *s, uint8_t slot, uint8_t type)
{
    if (slot >= NUM_MODULES || type >= MOD_TYPE_COUNT)
        return;
    // What do these do?
    int16_t *preserved_out = s->modules[slot].buffer_out;
    module_reset(&s->modules[slot], type, slot);
    s->modules[slot].buffer_out = preserved_out; // keep wiring
}

void synth_note_on(Synth *s, uint8_t midi_note, uint8_t velocity)
{
    s->active_note = midi_note;
    // s->active_velocity = velocity; // stretch goal
    // Dispatch to every module that cares.
    for (int i = 0; i < NUM_MODULES; i++)
    {
        Module *m = &s->modules[i];
        const ModuleDef *def = module_def(m->type);
        if (def->note_on)
            def->note_on(m, midi_note, velocity);
    }
}

void synth_note_off(Synth *s, uint8_t midi_note)
{
    (void)midi_note;
    s->active_note = 0;
    // Envelopes etc. that want a release stage can watch active_note.
}

void synth_process(Synth *s)
{
    if (s->muted)
    {
        // Set DAC output to all zeros if muted.
        memset(DAC_OUT, 0, sizeof(DAC_OUT));
        return;
    }

    for (int i = 0; i < NUM_MODULES; i++)
    {
        Module *m = &s->modules[i];
        const ModuleDef *def = module_def(m->type);

        // Resolve this module's three incoming buffers before processing.
        fill_buffer(s, m, m->input_buffer, m->input_source, 0);
        fill_buffer(s, m, m->param1_buffer, m->param1_source, m->param1_value);
        fill_buffer(s, m, m->param2_buffer, m->param2_source, m->param2_value);

        if (def->process)
            def->process(m);
    }
    // DAC_OUT now holds one buffer of 16-bit samples ready to push to the DAC.
}
