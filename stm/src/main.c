#include <stdio.h>
#include <stdint.h>
#include <stm32l432xx.h>
#include "ee14lib.h"
// #include "uart.c"

/******* NOTES ********
    - using blocking functions
    - USART1: PA9 (D1) (TX), PA10 (D0) (RX)
    - not using any form of printf because we just want it to send over the wires not go to terminal
*/

/**** MIDI NOTES *******
    - Status byte: 1xxxnnnn
        - we only care about xxx == 000, 001, 011
    - Data byte: 0xxxxxxx
    - Note off (xxx = 000)
        - data byte 1: what note was released (60 is middle C)
        - data byte 2: release velocity
    - Note on (xxx = 001)
        - data byte 1: what note was pressed (60 is middle C)
        - data byte 2: press velocity
    - control change (xxx = 011)
        - data byte 1: what controller is being adjusted
        - data byte 2: set value of controller
*/

#define NOTE_OFF 0b000
#define NOTE_ON 0b001
#define CONTROL_CHANGE 0b011

// This function is called by printf() to handle the text string
// We want it to be sent over the serial terminal, so we just delegate to that function
int _write(int file, char *data, int len)
{
    return serial_write_nonblocking(USART2, data, len);
}

#define MIDI_QUEUE_SIZE 500

typedef struct
{
    uint8_t msg_type;
    uint8_t data1;
    uint8_t data2;
} MIDI_msg;

typedef struct
{
    MIDI_msg buf[MIDI_QUEUE_SIZE];
    volatile uint8_t head; // push to this end
    volatile uint8_t tail; // pull from this end
} MIDI_queue;

void midi_push(MIDI_queue *q, MIDI_msg m)
{
    uint8_t next = (q->head + 1) % MIDI_QUEUE_SIZE;

    if (next == q->tail)
    {
        // buffer full so drop message (or overwrite)
        return;
    }

    q->buf[q->head] = m;
    q->head = next;
}

uint8_t midi_pop(MIDI_queue *q, MIDI_msg *m)
{
    if (q->head == q->tail)
        return 0; // empty

    *m = q->buf[q->tail];
    q->tail = (q->tail + 1) % MIDI_QUEUE_SIZE;
    return 1;
}

// This is used by snprintf(); see below.
#define LINE_BUFFER_LEN 100
#define BUFFER_FULL_PIN D6

int main()
{
    host_serial_init(9600); // USART2 -> PC terminal
    usart1_init(9600);      // USART1 -> Pi

    // Set up interrupt for UART parsing pi data
    // Register not empty - trigger interrupt when byte received
    USART1->CR1 |= USART_CR1_RXNEIE;
    // Set NVIC priority and enable
    NVIC_SetPriority(USART1_IRQn, 2);
    NVIC_EnableIRQ(USART1_IRQn);

    // USE FOR PRINTING
    // char outbuf[LINE_BUFFER_LEN];
    // int len = 0;

    // model: “I collect data bytes until I have 2 for the current status”
    int msg_type = NOTE_OFF;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
    uint8_t num_data_bytes = 0;
    uint8_t b;

    MIDI_queue queue = {0};

    // interprets byte by byte not 3 byte msg by msg
    while (1)
    {
        b = serial_read(USART1);

        // status byte -- reset everything
        if ((b >> 7) & 0b1)
        {
            msg_type = (b >> 4) & 0b111;

            data1 = 0;
            data2 = 0;
            num_data_bytes = 0;

            // data byte - figure out which one
        }
        else
        {
            if (num_data_bytes == 0)
            {
                data1 = b & 0b01111111;
                num_data_bytes++;
            }
            else
            {
                data2 = b & 0b01111111;
                num_data_bytes = 2;
            }
        }

        // add full message to queue
        if (num_data_bytes == 2)
        {
            midi_push(&queue, (MIDI_msg){msg_type, data1, data2});
            // if (msg_type == NOTE_ON) {
            //     len = snprintf(outbuf, LINE_BUFFER_LEN, "ON: %d %d\n", data1, data2);
            // } else if (msg_type == NOTE_OFF) {
            //     len = snprintf(outbuf, LINE_BUFFER_LEN, "OFF: %d %d\n", data1, data2);
            // } else if (msg_type == CONTROL_CHANGE) {
            //     len = snprintf(outbuf, LINE_BUFFER_LEN, "CC: %d %d\n", data1, data2);
            // }

            // serial_write_nonblocking(USART2, outbuf, len);

            num_data_bytes = 0;
        }
    }
}
