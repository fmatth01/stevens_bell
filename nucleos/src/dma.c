#include "stm32l4xx.h"
#include "synth_types.h"

/* Double-buffered audio DMA
 * Total buffer : 2 × BUFFER_SIZE = 256 samples
 * Half A       : indices 0..127  — filled on TCIF (transfer complete)
 * Half B       : indices 128..255 — filled on HTIF (half transfer)
 * Sample rate  : 44100 Hz (TIM2 ARR=1814, PSC=0, 80MHz)
 * DMA1 Channel 3, CSELR=6 (DAC_CH1)
 */

uint16_t dma_buffer[BUFFER_SIZE * 2];

// Callback — defined in main.c or synth.c, fills one half
extern void audio_callback(uint16_t *buffer, uint16_t length);

void dma_configure(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;

    DMA1_Channel3->CCR = 0;
    DMA1_Channel3->CPAR = (uint32_t)&DAC1->DHR12R1;
    DMA1_Channel3->CMAR = (uint32_t)dma_buffer;
    DMA1_Channel3->CNDTR = BUFFER_SIZE * 2;

    DMA1_Channel3->CCR =
        DMA_CCR_CIRC      // circular
        | DMA_CCR_DIR     // memory -> peripheral
        | DMA_CCR_MINC    // increment memory
        | DMA_CCR_PSIZE_0 // 16-bit peripheral
        | DMA_CCR_MSIZE_0 // 16-bit memory
        | DMA_CCR_PL_1    // high priority
        | DMA_CCR_HTIE    // half transfer interrupt enable
        | DMA_CCR_TCIE    // transfer complete interrupt enable
        | DMA_CCR_EN;

    // Route DAC_CH1 to DMA1 Channel 3
    DMA1_CSELR->CSELR &= ~DMA_CSELR_C3S;
    DMA1_CSELR->CSELR |= (6 << DMA_CSELR_C3S_Pos);

    // Enable DMA1 Channel3 interrupt in NVIC
    // MUST be a higher priority than the UART midi reading interrupt!
    NVIC_SetPriority(DMA1_Channel3_IRQn, 1);
    NVIC_EnableIRQ(DMA1_Channel3_IRQn);
}

// Interrupt definition
void DMA1_Channel3_IRQHandler(void)
{
    if (DMA1->ISR & DMA_ISR_HTIF3)
    {
        DMA1->IFCR = DMA_IFCR_CHTIF3;            // clear half transfer flag
        audio_callback(dma_buffer, BUFFER_SIZE); // fill first half
    }
    if (DMA1->ISR & DMA_ISR_TCIF3)
    {
        DMA1->IFCR = DMA_IFCR_CTCIF3;                          // clear transfer complete flag
        audio_callback(dma_buffer + BUFFER_SIZE, BUFFER_SIZE); // fill second half
    }
}