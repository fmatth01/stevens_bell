#include <stdint.h>

#ifndef TIMER_H
#define TIMER_H

void enable_timer(TIM_TypeDef* const timer);
EE14Lib_Err timer_config_freerun(TIM_TypeDef* const timer, const unsigned int prescaler);
uint32_t timer_get_count(TIM_TypeDef* const timer);

#endif