#include "stm32l4xx.h"
#include "basic.h"
#include "clock.h"
#include "dma.h"
#include "synth_types.h"
#include "ee14lib.h"
#include <string.h>

static Synth synth;
static int16_t osc_out[BUFFER_SIZE];
static int16_t env_out[BUFFER_SIZE];

void audio_callback(uint16_t *buffer, uint16_t length) {
    static int32_t phase = 0;
    for (int i = 0; i < BUFFER_SIZE; i++) {
        osc_out[i] = (int16_t)(phase - 32768);
        phase = (phase + 512) % 65536;
    }

    memcpy(synth.modules[0].input_buffer, osc_out, sizeof(int16_t) * BUFFER_SIZE);

    synth.modules[0].buffer_out = env_out;
    const ModuleDef *def = module_def(synth.modules[0].type);
    if (def->process) def->process(&synth.modules[0]);

    for (uint16_t i = 0; i < length; i++) {
        buffer[i] = ((int32_t)env_out[i] + 32768) >> 4;
    }
}

int main(void) {
    SystemClock_Config();
    enable_DAC();

    synth_init(&synth);
    synth_set_module_type(&synth, 0, MOD_ENV);

    // slow attack, slow decay so you can clearly hear each stage
    synth.modules[0].param1_value = 5;   // very slow attack
    synth.modules[0].param2_value = 5;   // very slow decay

    audio_callback(dma_buffer, BUFFER_SIZE);
    audio_callback(dma_buffer + BUFFER_SIZE, BUFFER_SIZE);

    dma_configure();
    timer_configure();

    while (1) {
        // ── NOTE ON ──────────────────────────────
        // triggers attack then decay then sustain
        synth_note_on(&synth, 69, 127);
        for (volatile int d = 0; d < 80000000; d++);  // hold ~8 seconds

        // ── NOTE OFF ─────────────────────────────
        // triggers release
        synth_note_off(&synth, 69);
        for (volatile int d = 0; d < 80000000; d++);  // wait ~8 seconds for silence
    }

    return 0;
}