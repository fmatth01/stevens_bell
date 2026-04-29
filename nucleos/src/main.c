#include "stm32l4xx.h"
#include "basic.h"
#include "clock.h"
#include "dma.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "ee14lib.h"
#include "synth_types.h"
#include "display.h"

// Definition of the Circular Buffer for reading midi data
#define MIDI_BUF_SIZE 64
static volatile uint8_t midi_buf[MIDI_BUF_SIZE];
static volatile uint8_t midi_head = 0, midi_tail = 0;

static Synth synth;
static volatile bool needs_redraw = true;

void audio_callback(uint16_t *buffer, uint16_t length)
{
    synth_process(&synth);
    int16_t *out = synth_get_output();
    for (uint16_t i = 0; i < length; i++)
    {
        // Shift into unsigned range and reduce to 12 bits
        buffer[i] = ((int32_t)out[i] + 32768) >> 4;
    }
}

// Interrupt Code for UART reading from pi
// Pushes raw data packets into the circular buffer
// so that the interrupt runs as fast as possible
void USART1_IRQHandler(void)
{
    // Take raw bytes from Receive Data Register (bottom half)
    // and turn off RXNE flag to indicate the bytes were read
    uint8_t b = USART1->RDR & 0xFF;
    // Get next index of circular buffer, looping around
    uint8_t next = (midi_head + 1) % MIDI_BUF_SIZE;
    // If the MIDI buffer is full, just drop the packet, otherwise store
    if (next != midi_tail)
        midi_buf[midi_head] = b;
    // Update next
    midi_head = next;
}

int main(void)
{
    // Initialization
    SystemClock_Config();
    display_init();
    enable_DAC();

    synth_init(&synth);

    audio_callback(dma_buffer, BUFFER_SIZE);
    audio_callback(dma_buffer + BUFFER_SIZE, BUFFER_SIZE);

    dma_configure();
    // timer_configure();

    update_display(&synth); // initial draw

    // ── Pick one to test the display ──
    // synth_preset_all_types(&synth);     // global view: all module names in grid
    synth_preset_lfo_osc(&synth); // global view: H+V arrows, note playing
    // synth_preset_mixed_routing(&synth); // global view: both arrow types

    // Or drop into module view for a specific slot:
    // synth_inspect_module(&synth, 1);   // module view for slot 1 (OSC)

    // Main Loop
    while (1)
    {
        // Drain MIDI circular buffer — redraw if any packet changes synth state
        while (midi_head != midi_tail)
        {
            uint8_t b = midi_buf[midi_tail];
            midi_tail = (midi_tail + 1) % MIDI_BUF_SIZE;
            synth_unpack_packet(&synth, b);
            needs_redraw = true;
        }

        if (needs_redraw)
        {
            update_display(&synth);
            needs_redraw = false;
        }
    }
    return 0;
}