//
// Created by Dhairya Gupta on 1/19/25.
//
#include <stdint.h>
#ifndef VCU_FIRMWARE_2025_USB_VCP_H
#define VCU_FIRMWARE_2025_USB_VCP_H

void println(char *buffer);
void receiveData(uint8_t* data, uint32_t len);


#endif //VCU_FIRMWARE_2025_USB_VCP_H
