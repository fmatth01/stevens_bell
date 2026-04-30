#include "synth_types.h"
#include "wavetable.h"
#include <string.h>

// Process order: bottom row first (left → right), then top row (left → right).
// This guarantees LEFT and BOTM buffers are valid before any module reads them.
// Order is {0, 1, 2, 3, 4, 5, 6, 7}

// Zero buffer used for LEFT/BOTM sources that point off the grid.
static int16_t ZERO_BUFFER[BUFFER_SIZE];

// Final DAC out buffer lives here; top-right module (slot 7) writes into it.
static int16_t DAC_OUT[BUFFER_SIZE];

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
        return NULL;              // bottom row has no bottom neighbor
    return &s->modules[slot - 4]; // top slot s → bottom slot s-4
}

// Set source
static void set_source(Synth *s, Module *m, uint8_t src)
{
    // Set buffer var to the buffer we're changing the source of
    int16_t *buffer = NULL;
    uint8_t *source = NULL; // pointer to module's current source variable
    // Update source for currently selected parameter in module view
    switch (s->selected_param)
    {
    case SEL_INPUT:
        buffer = m->input_buffer;
        source = &m->input_source;
        break;

    case SEL_PARAM1:
        buffer = m->param1_buffer;
        source = &m->param1_source;
        break;

    case SEL_PARAM2:
        buffer = m->param2_buffer;
        source = &m->param2_source;
        break;

    default:
        break;
    }

    // Set the source of the current module to the updated value (src)
    if (*source)
    {
        *source = src;
    }

    switch (src)
    {
    case SRC_LEFT:
    {
        Module *nl = neighbor_left(s, m->position);
        if (nl)
            nl->buffer_out = buffer;
        break;
    }
    case SRC_BOTM:
    {
        Module *nb = neighbor_bottom(s, m->position);
        if (nb)
            nb->buffer_out = buffer;
        break;
    }
    case SRC_WAVE:
    // Assume wavetable loader has already done the appropriate stuff
    case SRC_FLAT:
    // No buffers to redirect
    case SRC_KNOB:
    // Taken care of in fill
    default:
        break;
    }
}

// Fill a destination buffer from the given source. For FLAT/KNOB we write the
// scalar across the whole buffer; for LEFT/BOTM we copy the neighbor's output;
static void fill_buffer(Synth *s, Module *m, int16_t *dst,
                        uint8_t source, uint8_t scalar_value)
{
    switch (source)
    {
    case SRC_FLAT:
    {
        // Scale flat value to 16-bit sample
        // Scale 0..127 → 0..32767, aka ~max_val of int16_t
        int16_t v = (int16_t)(scalar_value * 258); // ~ * 32767/127
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
        // Handled in OSC and LFO process code
        break;
    default:
        // Any other case just set to zeros (protecting from undefined behavior)
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
    // Set entire synth struct to zeros
    memset(s, 0, sizeof(*s));
    // s->global_view = true;
    s->scope_on = true;
    // s->scope_macro = true;
    s->selected_module = 7; // top right corner

    // Set every param1 in top row to output buffer of module below it (vertical arrows)
    s->selected_param = SEL_PARAM1;
    for (uint8_t i = 0; i < NUM_MODULES; i++)
    {
        module_reset(&s->modules[i], MOD_EMP, i);
        set_source(s, &s->modules[i], SRC_BOTM);
    }

    // Set all output buffers to the input buffers of neighbors (horizontal arrows)
    s->selected_param = SEL_INPUT;
    for (uint8_t i = 0; i < NUM_MODULES; i++)
    {
        if (i != 0 && i != 4) // Everything but left col
        {
            set_source(s, &s->modules[i], SRC_LEFT);
        }
    }

    // Slot 7 (top-right) is the final output — wire its buffer_out to DAC_OUT.
    s->modules[7].buffer_out = DAC_OUT;
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
    s->note_v_oct = midi_note;
    s->note_on = true;
    // s->active_velocity = velocity; // stretch goal

    // Dispatch to every module that cares.
    for (int i = 0; i < NUM_MODULES; i++)
    {
        Module *m = &s->modules[i];
        // Grab the definition of the current module
        const ModuleDef *def = module_def(m->type);
        // If the current module has a defined note_on fx pointer, use it
        if (def->note_on)
            def->note_on(m, midi_note, velocity);
    }
}

void synth_note_off(Synth *s, uint8_t midi_note)
{
    (void)midi_note;
    s->note_on = false;
    // note_v_oct intentionally preserved — OSC continues at last pitch without a gate.
    // Envelopes etc. that want a release stage can watch note_on.
}

void synth_update_cc(Synth *s, uint8_t cc_channel, uint8_t cc_val)
{
    switch (cc_channel)
    {
    case 1:
        left_arrow(s);
        break;
    case 2:
        right_arrow(s);
        break;
    case 3:
        bouncing_arrow(s);
        break;
    case 4:
        audio_wave(s);
        break;
    case 5:
        play_button(s);
        break;
    case 6:
        stop_button(s);
        break;
    case 7:
        break; // record — unassigned
    case 8:
        rotating_arrows(s);
        break;
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
        // Knobs
        s->knobs[cc_channel - 9] = cc_val;
        break;
    case 17:
        // Top Slider
        s->top_slider = cc_val;
        break;
    case 18:
        // Bottom Slider
        s->bottom_slider = cc_val;
        break;
    }
}

void synth_process(Synth *s)
{
    if (s->muted)
    {
        // Set DAC output to all zeros if muted.
        memset(DAC_OUT, 0, sizeof(DAC_OUT));
        return;
    }

    // Loop through each module and process it
    for (int i = 0; i < NUM_MODULES; i++)
    {
        // Pointer to module struct - actual state of module
        Module *m = &s->modules[i];
        // Pointer to module definition - names, process function pointers, etc.
        const ModuleDef *def = module_def(m->type);

        // Resolve this module's three incoming buffers before processing.
        fill_buffer(s, m, m->input_buffer, m->input_source, 0);
        fill_buffer(s, m, m->param1_buffer, m->param1_source, m->param1_value);
        fill_buffer(s, m, m->param2_buffer, m->param2_source, m->param2_value);

        // Process the module if it has a process function (not an empty module)
        if (def->process)
            def->process(m);
    }
    // DAC_OUT now holds one buffer of 16-bit samples ready to push to the DAC.
}

int16_t *synth_get_output(void)
{
    return DAC_OUT;
}

// Unpacks the packets put into the circular buffer by the UART
// MIDI sends messages in three-byte sequences: Status, Data1, Data2
void synth_unpack_packet(Synth *s, uint8_t b)
{
    // Only set to zero at compile time, otherwise persist like globals do
    // Basically flags for our messages
    static uint8_t msg_type = 0, data1 = 0, num_data = 0;

    if (b >> 7)
    { // status byte
        msg_type = (b >> 4) & 0x7;
        data1 = 0;
        num_data = 0;
    }
    else if (num_data == 0)
    { // first data byte
        data1 = b & 0x7F;
        num_data = 1;
    }
    else
    { // second data byte — full message ready
        uint8_t data2 = b & 0x7F;
        num_data = 0;
        if (msg_type == 0b001)
            synth_note_on(s, data1, data2);
        else if (msg_type == 0b000)
            synth_note_off(s, data1);
        else if (msg_type == 0b011)
            synth_update_cc(s, data1, data2);
    }
}

void left_arrow(Synth *s)
{
    if (s->scope_on)
    {
        if (s->scope_skip_idx < SCOPE_SKIP_MAX_IDX)
            s->scope_skip_idx++; // more skipped frames = slower refresh
    }
    else if (s->global_view)
    {
        // Decrement selected module
        if (s->selected_module > 0)
            s->selected_module--;
    }
    else
    {
        // Decrement selected param
        if (s->selected_param > 0)
            s->selected_param--;
    }
}

void right_arrow(Synth *s)
{
    if (s->scope_on)
    {
        if (s->scope_skip_idx > 0)
            s->scope_skip_idx--; // fewer skipped frames = faster refresh
    }
    else if (s->global_view)
    {
        // Increment selected module
        if (s->selected_module < 7)
            s->selected_module++;
    }
    else
    {
        // Increment selected param
        if (s->selected_param < SEL_COUNT - 1)
            s->selected_param++;
    }
}

void bouncing_arrow(Synth *s)
{
    if (s->scope_on)
    {
        // Toggle macro/micro mode
        s->scope_macro = !s->scope_macro;
    }
    else
    {
        uint8_t next = (s->modules[s->selected_module].type + 1) % MOD_TYPE_COUNT;
        synth_set_module_type(s, s->selected_module, next);
    }
}

void audio_wave(Synth *s)
{
    s->scope_on = !s->scope_on;
}

void play_button(Synth *s)
{
    s->muted = false;
}

void stop_button(Synth *s)
{
    s->muted = true;
}

void rotating_arrows(Synth *s)
{
    s->global_view = !s->global_view;
}

void top_slider(Synth *s)
{
    // If module view is on and scope mode is not on
    // (Scope behavior handled by display)
    if (!s->global_view && !s->scope_on)
    {
        // Pointer to currently selected module
        Module *m = &s->modules[s->selected_module];
        // Change source of currently selected parameter
        // (SRC_COUNT - 1) to prevent SRC_WAVE from being accessible
        if (s->selected_param == SEL_INPUT)
        {
            // Only the inputs of oscillators and LFOs are allowed to have
            // wavetables as their source
            if (m->type == MOD_OSC || MOD_LFO)
            {
                m->input_source = s->top_slider * SRC_COUNT / 128;
            }
            else
            {
                m->input_source = s->top_slider * (SRC_COUNT - 1) / 128;
            }
        }
        else if (s->selected_param == SEL_PARAM1)
        {
            m->param1_source = s->top_slider * (SRC_COUNT - 1) / 128;
        }
        else if (s->selected_param == SEL_PARAM2)
        {
            m->param2_source = s->top_slider * (SRC_COUNT - 1) / 128;
        }
    }
}

void bottom_slider(Synth *s)
{
    // If module view is on and scope mode is not on
    // (Scope behavior handled by display)
    if (!s->global_view && !s->scope_on)
    {
        // Pointer to currently selected module
        Module *m = &s->modules[s->selected_module];
        // Select a different wavetable in oscillator modules
        if ((m->type == MOD_OSC || MOD_LFO) && s->selected_param == SEL_INPUT)
        {
            // Update state_c (selected wavetable) with new slider CC value
            // Value between 0..(TABLE_COUNT - 1), corrosponding to indecies
            // In the table array
            m->input_value = s->bottom_slider * TABLE_COUNT / 128;
        }
        // Set a different flat value for any parameter in any module
        else if (s->selected_param == SEL_INPUT && m->input_source == SRC_FLAT)
        {
            m->input_value = s->bottom_slider;
        }
        else if (s->selected_param == SEL_PARAM1 && m->param1_source == SRC_FLAT)
        {
            m->param1_value = s->bottom_slider;
        }
        else if (s->selected_param == SEL_PARAM2 && m->param2_source == SRC_FLAT)
        {
            m->param2_value = s->bottom_slider;
        }
    }
}

// ── Display test presets ──────────────────────────────────────────────────────
// Call one of these from main.c after synth_init() to load a known state.
// Each preset sets scope_on=false and configures global_view as noted.

// All 7 module types placed in the grid — good for checking global view labels
// and selection rect.
//   Bottom row (0-3): ---  OSC  MIX  ENV
//   Top row    (4-7): LPF  HPF  LFO  ---
// Horizontal arrows visible on all non-leftmost slots (SRC_LEFT default).
void synth_preset_all_types(Synth *s)
{
    synth_init(s);
    synth_set_module_type(s, 1, MOD_OSC);
    synth_set_module_type(s, 2, MOD_MIX);
    synth_set_module_type(s, 3, MOD_ENV);
    synth_set_module_type(s, 4, MOD_LPF);
    synth_set_module_type(s, 5, MOD_HPF);
    synth_set_module_type(s, 6, MOD_LFO);
    s->scope_on = false;
    s->global_view = true;
    s->selected_module = 7;
}

// LFO (slot 0) → OSC (slot 1) chain with a note playing.
// Shows horizontal arrow between slots 0→1.
// Slot 4 reads from slot 0 via BOTM to show a vertical arrow on col 0.
//   Bottom row: LFO  OSC  ---  ---
//   Top row:    ---  ---  ---  ---
void synth_preset_lfo_osc(Synth *s)
{
    synth_init(s);
    // synth_set_module_type(s, 3, MOD_LFO);
    synth_set_module_type(s, 7, MOD_OSC);
    s->modules[7].param2_value = 64; // mid pitch
    // s->modules[7].param1_source = SRC_BOTM; // wavetable from lfo
    s->scope_on = true;
    s->global_view = true;
    s->scope_macro = false;
    s->selected_module = 7;
    synth_note_on(s, 69, 127); // A4
}

void synth_preset_mixed_routing(Synth *s)
{
    synth_init(s);
    synth_set_module_type(s, 0, MOD_LFO);
    synth_set_module_type(s, 1, MOD_OSC);
    synth_set_module_type(s, 2, MOD_ENV);
    synth_set_module_type(s, 5, MOD_MIX);
    synth_set_module_type(s, 6, MOD_LPF);
    s->modules[1].input_source = SRC_LEFT; // OSC ← LFO
    s->modules[2].input_source = SRC_LEFT; // ENV ← OSC
    s->modules[5].input_source = SRC_BOTM; // MIX ← OSC (vertical)
    s->modules[6].input_source = SRC_BOTM; // LPF ← ENV (vertical)
    s->scope_on = false;
    s->global_view = true;
    s->selected_module = 5;
}

// Enter module view for a specific slot with scope off.
void synth_inspect_module(Synth *s, uint8_t slot)
{
    if (slot >= NUM_MODULES)
        return;
    s->selected_module = slot;
    s->global_view = false;
    s->scope_on = false;
    s->selected_param = SEL_INPUT;
}