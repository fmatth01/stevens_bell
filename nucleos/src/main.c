#include "stm32l4xx.h"
#include "basic.h"
#include "clock.h"
#include "dma.h"
#include "synth_types.h"

static Synth synth;

void audio_callback(uint16_t *buffer, uint16_t length) {
    synth_process(&synth);
    int16_t *out = synth_get_output();
    for (uint16_t i = 0; i < length; i++) {
        buffer[i] = ((int32_t)out[i] + 32768) >> 4;
    }
}

int main(void) {
    SystemClock_Config();
    enable_DAC();

    synth_init(&synth);

    // slot 0 = LFO
    synth_set_module_type(&synth, 0, MOD_LFO);
    synth.modules[0].param2_value = 100;  // very slow LFO
    synth.modules[0].buffer_out = synth.modules[1].input_buffer;

    // slot 1 = OSC
    synth_set_module_type(&synth, 1, MOD_OSC);
    synth.modules[1].param2_value = 63;
    synth.modules[1].param1_value = 127;  // maximum depth
    synth.modules[1].input_source = SRC_LEFT;
    synth_wire_slot1_to_dac(&synth);

    synth_note_on(&synth, 69, 127);

    audio_callback(dma_buffer, BUFFER_SIZE);
    audio_callback(dma_buffer + BUFFER_SIZE, BUFFER_SIZE);

    dma_configure();
    timer_configure();

    while (1) {}
    return 0;
}