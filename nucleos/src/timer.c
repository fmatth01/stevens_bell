#include "ee14lib.h"

// static void enable_timer(TIM_TypeDef* const timer)
// {
//     // Enable the clock for the timer
//     if(timer == TIM1){
//         RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
//     }
//     else if(timer == TIM2){
//         RCC->APB1ENR1 |= RCC_APB1ENR1_TIM2EN;
//     }
//     else if(timer == TIM15){
//         RCC->APB2ENR |= RCC_APB2ENR_TIM15EN;
//     }
//     else if(timer == TIM16){
//         RCC->APB2ENR |= RCC_APB2ENR_TIM16EN;
//     }
// }

// /* Configure a timer in free-running mode so that it can be used to keep track of time.
//  *   timer: which hardware timer to use (TIM1, TIM2, etc.)
//  *   prescaler: value to use for the hardware prescaler, which will determine the tick period
//  */
// EE14Lib_Err timer_config_freerun(TIM_TypeDef* const timer, const unsigned int prescaler)
// {
//     enable_timer(timer);

//     // Top-level control registers are fine with defaults (except for turning it on, later)

//     timer->PSC = prescaler;

//     // Force an update event so the prescaler gets loaded and timer resets
//     timer->EGR |= TIM_EGR_UG;

//     // And enable the timer itself
//     timer->CR1 |= TIM_CR1_CEN;

//     return EE14Lib_Err_OK;
// }

EE14Lib_Err timer_configure(void)
{
    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM2EN;
    TIM2->PSC = 0;
    TIM2->ARR = 1814;           // 80MHz / 44100 - 1
    TIM2->CR2 |= TIM_CR2_MMS_1; // TRGO on update event
    TIM2->EGR |= TIM_EGR_UG;
    TIM2->CR1 |= TIM_CR1_CEN;
    return EE14Lib_Err_OK;
}

uint32_t timer_get_count(TIM_TypeDef *const timer)
{
    return timer->CNT;
}