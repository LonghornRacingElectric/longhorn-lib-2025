//
// Created by Dhairya Gupta on 1/19/25.
//

#include "usb_vcp.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"

void println(char *buffer) {
    size_t len = strlen(buffer);
#ifdef STM32L4
    CDC_Transmit_FS((uint8_t *) buffer, len);
    CDC_Transmit_FS((uint8_t *) "\n", 1);
    CDC_Transmit_FS((uint8_t *) "\r", 1);
#elif defined(STM32H7)
    CDC_Transmit_HS((uint8_t *) buffer, len);
    CDC_Transmit_HS((uint8_t *) "\n", 1);
    CDC_Transmit_HS((uint8_t *) "\r", 1);
#endif
}
