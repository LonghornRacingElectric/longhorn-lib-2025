//
// Created by Dhairya Gupta on 1/19/25.
//
#include <stdint.h>
#include "main.h"
#ifndef VCU_FIRMWARE_2025_USB_VCP_H
#define VCU_FIRMWARE_2025_USB_VCP_H

void usb_init();

static int dfu_enable = 0;

void println(char *buffer);
void usb_printf(const char *format, ...);
void receiveData(uint8_t* data, uint32_t len);
void receive_periodic();
extern volatile uint8_t receivedNotRead;


#define DFU_COMMAND "update"


#endif //VCU_FIRMWARE_2025_USB_VCP_H
