//
// Created by Dhairya Gupta on 1/19/25.
//

#include "usb_vcp.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"

static void println(uint16_t size, uint8_t *buffer) {
    CDC_Transmit_HS(buffer, size);
}
