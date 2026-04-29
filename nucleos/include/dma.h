#ifndef DMA_H
#define DMA_H

#include <stdint.h>
#include "synth_types.h"

extern uint16_t dma_buffer[BUFFER_SIZE * 2];  // exposed for pre-fill in main

void dma_configure(void);

#endif