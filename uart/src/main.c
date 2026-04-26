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


#define STATUS_BIT 1
#define DATA_BIT 0

#define NOTE_OFF 0b000
#define NOTE_ON 0b001
#define CTRL_CHANGE 0b011


// This function is called by printf() to handle the text string
// We want it to be sent over the serial terminal, so we just delegate to that function
int _write(int file, char *data, int len) {
    serial_write(USART1, data, len);
    return len;
}

// This is used by snprintf(); see below.
#define LINE_BUFFER_LEN 100
#define BUFFER_FULL_PIN D6  

int main() {
    host_serial_init(9600);   // USART2 -> PC terminal
    usart1_init(9600);        // USART1 -> Pi

    // char linebuf[LINE_BUFFER_LEN];
    char outbuf[LINE_BUFFER_LEN];
    // int idx = 0;

    while (1) {
        uint8_t byte = (uint8_t) serial_read(USART1);   // blocks until a byte arrives on PA10
        int len = snprintf(outbuf, LINE_BUFFER_LEN, "Got: %d\n", byte);
        serial_write(USART2, outbuf, len);  // print to PC terminal

        // buffer it up until newline
        //save room in buffer for the "\0" character
        //NOTE: check "\n" because that is whats sent from python
        // if (c == '\n' || idx >= LINE_BUFFER_LEN - 1) {
        //     //trigger LED if buffer fills so data lost
        //     gpio_write(BUFFER_FULL_PIN, idx >= LINE_BUFFER_LEN - 1);

        //     linebuf[idx] = '\0'; //null terminate so valid c-string
        //     int len = snprintf(outbuf, LINE_BUFFER_LEN, "Got: %s\n", linebuf);
        //     serial_write(USART2, outbuf, len);  // print to PC terminal
        //     idx = 0;


        //     gpio_write(BUFFER_FULL_PIN, 0);
        // } else {
        //     linebuf[idx++] = c;
        // }
    }
}
