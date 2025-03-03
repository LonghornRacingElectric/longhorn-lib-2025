//
// Created by Dhairya Gupta on 3/3/25.
//

#include "timer.h"
#include "tim.h"

void lib_timer_init() {
    lib_timer_prevcycle = HAL_GetTick();
}

uint32_t lib_timer_ms_elapsed() {
    uint32_t cur = HAL_GetTick();

    if(cur < lib_timer_prevcycle) {
        lib_timer_prevcycle = cur;
        return 0;
    }

    uint32_t timeElapsed = cur - lib_timer_prevcycle;
    lib_timer_prevcycle = cur;
    return timeElapsed;
}
