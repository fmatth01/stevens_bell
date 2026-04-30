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
#include "timer.h"

//TODO - DELETE - FOR ERICA DEBUGGING
int _write(int file, char *data, int len) {
    serial_write(USART2, data, len);
    return len;
}

// Definition of the Circular Buffer for reading midi data
#define MIDI_BUF_SIZE 64
static volatile uint8_t midi_buf[MIDI_BUF_SIZE];
static volatile uint8_t midi_head = 0, midi_tail = 0;

#define BAUD 9600

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

// Interrupt Code for UART reading from pi (occurs every time a byte is sent)
// Pushes raw data packets into the circular buffer
// so that the interrupt runs as fast as possible
//note: don't put print statements in here because serial_write is blocking and 
//  may not clear flags fast enough
//ISSUE: we cannot guarantee all 3 bytes of the message are going to be sent

/* IDEAS
- we need to know if the byte coming in is a status byte
- we also need to know when we have received a full message
- one way is to keep track of an array of size 3
    - fill the array with the bytes
    - if you receive a status byte, clear the array and put the status byte at the front
    - if its a data byte, append to the array
- INTERRUPT: if the array has 3 bytes in it, then interrupt
    - the interrupt will push to the circular buffer, and clear ONLY THE DATA BITS
        - not the status bit incase of a prolonged CC hold (where just data bits come in)

*/
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
    enable_timer(TIM2);
    int last_draw = 0;
    int now = 0;

    host_serial_init(BAUD); //TODO - DELETE - FOR ERICA DEBUGGING

    //to receive bytes from pi
    usart1_midi_init(BAUD);

    //indicate to user that the program is running
    gpio_config_mode(A6, OUTPUT);
    gpio_write(A6, 1);

    // Main Loop
    while (1)
    {
        // Drain MIDI circular buffer — redraw if any packet changes synth state
        while (midi_head != midi_tail)
        {
            uint8_t b = midi_buf[midi_tail];
            printf("RAWt: %02X\n", b);
            midi_tail = (midi_tail + 1) % MIDI_BUF_SIZE;
            synth_unpack_packet(&synth, b);
            needs_redraw = true;
        }

        now = timer_get_count(TIM2);
        if (needs_redraw && (now - last_draw > 100))
        {
            update_display(&synth);
            needs_redraw = false;
            last_draw = now;
        }
    }
    return 0;
}