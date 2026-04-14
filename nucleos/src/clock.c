#include "stm32l4xx.h"

// cube
void SystemClock_Config(void) {

    // 1. Enable HSI16 and wait until ready
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY));

    // 2. Set flash latency for 80 MHz (4 wait states at VDD=3.3V, Vcore range 1)
    //    Also enable prefetch and instruction cache
    FLASH->ACR = FLASH_ACR_LATENCY_4WS
               | FLASH_ACR_PRFTEN
               | FLASH_ACR_ICEN
               | FLASH_ACR_DCEN;
    while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_4WS);

    // 3. Make sure Vcore is in Range 1 (required for 80 MHz)
    //    PWR clock must be enabled first
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
    PWR->CR1 = (PWR->CR1 & ~PWR_CR1_VOS) | PWR_CR1_VOS_0; // Range 1
    while (PWR->SR2 & PWR_SR2_VOSF);                       // Wait for regulator

    // 4. Configure PLL
    //    Disable PLL first
    RCC->CR &= ~RCC_CR_PLLON;
    while (RCC->CR & RCC_CR_PLLRDY);

    // PLLCFGR: source=HSI16, PLLM=1(/1), PLLN=10(x10), PLLR=2(/2)
    // PLLM bits [6:4]: 000 = /1
    // PLLN bits [14:8]: 0b0001010 = 10
    // PLLR bits [26:25]: 00 = /2
    RCC->PLLCFGR = RCC_PLLCFGR_PLLSRC_HSI       // HSI16 as source
                 | (0  << RCC_PLLCFGR_PLLM_Pos)  // PLLM = /1
                 | (10 << RCC_PLLCFGR_PLLN_Pos)  // PLLN = x10
                 | (0  << RCC_PLLCFGR_PLLR_Pos)  // PLLR = /2
                 | RCC_PLLCFGR_PLLREN;            // Enable PLLR output

    // 5. Enable PLL and wait
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY));

    // 6. Set AHB, APB1, APB2 prescalers (all /1 = 80 MHz)
    RCC->CFGR = RCC_CFGR_HPRE_DIV1    // AHB  = SYSCLK / 1
              | RCC_CFGR_PPRE1_DIV1   // APB1 = SYSCLK / 1
              | RCC_CFGR_PPRE2_DIV1;  // APB2 = SYSCLK / 1

    // 7. Switch SYSCLK to PLL
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);

    // 8. Update SystemCoreClock variable
    SystemCoreClock = 80000000UL;
}

void enable_DAC(void) {
    RCC->APB1ENR1 |= RCC_APB1ENR1_DAC1EN;
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    GPIOA->MODER |= GPIO_MODER_MODE4; // Analog Mode
    GPIOA->PUPDR &= ~GPIO_PUPDR_PUPD4; // No pull-up/down  
    DAC1->CR |= DAC_CR_EN1;
    DAC1->DHR12R1 = 4095; 
}

