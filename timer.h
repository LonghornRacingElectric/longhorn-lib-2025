//
// Created by Dhairya Gupta on 3/3/25.
//

#ifndef VCU_FIRMWARE_2025_TIMER_H
#define VCU_FIRMWARE_2025_TIMER_H
#include <stdint.h>

void lib_timer_init();

uint32_t lib_timer_delta_ms();
uint32_t lib_timer_elapsed_ms();


#endif //VCU_FIRMWARE_2025_TIMER_H
