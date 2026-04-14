#include "stm32l4xx.h"
#include "basic.h"
#include "clock.h"

// 
// change sample rate to 44.1 khz !!!!!!

//System Clock Configuration The system Clock is configured as follow : 
    // System Clock source = PLL (MSI)
    // SYSCLK(Hz) = 80000000 HCLK(Hz) = 80000000
    // AHB Prescaler = 1
    // APB1 Prescaler = 1
    // APB2 Prescaler = 1
    // MSI Frequency(Hz) = 8000000
    // PLL_M = 1
    // PLL_N = 20
    // PLL_R = 2
    // PLL_P = 7
    // PLL_Q = 4
    // Flash Latency(WS) = 4
volatile unsigned int counter;

void  SysTick_Handler(void) {
    counter++;
}

void delay_ms(uint32_t ms) {
    uint32_t start = counter;
    while ((counter - start) < ms);
}

void SysTick_initialize(void) {
    // TODO: figure out what each line of code in this function does
    SysTick->CTRL = 0;
    SysTick->LOAD = (80000000 / 500) - 1; // TODO: fill this in with an appropriate value
    // This sets the priority of the interrupt to 15 (2^4 - 1), which is the
    // largest supported value (aka lowest priority)
    NVIC_SetPriority (SysTick_IRQn, (1<<__NVIC_PRIO_BITS) - 1);
    SysTick->VAL = 0;
    SysTick->CTRL |= SysTick_CTRL_CLKSOURCE_Msk;
    //SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;
}

int main(void) {
    SystemClock_Config();
    SysTick_initialize();  
    enable_DAC();
    // timer_config_pwm(TIM2, 1000);
    while(1) {
        for (int i = 0; i < 4000; i++) {
            while (!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk));
            uint32_t sample = ((int32_t)audio_data[i] + 32768) >> 4;
            DAC1->DHR12R1 = sample;
        }
    }
    return 0;
}