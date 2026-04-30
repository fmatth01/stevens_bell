#include "ee14lib.h"

EE14Lib_Err timer_configure(void) {
    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM2EN;
    TIM2->PSC = 0;
    TIM2->ARR = 1814;             // 80MHz / 44100 - 1
    TIM2->CR2 |= TIM_CR2_MMS_1;  // TRGO on update event
    TIM2->EGR |= TIM_EGR_UG;
    TIM2->CR1 |= TIM_CR1_CEN;
    return EE14Lib_Err_OK;
}

uint32_t timer_get_count(TIM_TypeDef* const timer)
{
    return timer->CNT;
}