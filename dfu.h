//
// Created by Dhairya Gupta on 3/1/25.
//

#ifndef VCU_FIRMWARE_2025_DFU_H
#define VCU_FIRMWARE_2025_DFU_H

#include "gpio.h"

static uint16_t boot0pin;
static GPIO_TypeDef *boot0GPIO;

void dfu_init(GPIO_TypeDef *boot0, uint16_t bootPin);

/* DANGEROUS!! This will pull up boot0, you'll have to upload code */
void boot_to_dfu();

#endif //VCU_FIRMWARE_2025_DFU_H
