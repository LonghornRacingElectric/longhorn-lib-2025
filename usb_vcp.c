//
// Created by Dhairya Gupta on 1/19/25.
//

#include <ctype.h>
#include "usb_vcp.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"
#include "stdarg.h"
#include "usart.h"

#define BUFFER_SIZE 64
#define OUT_BUFFER_SIZE 256

volatile uint8_t receivedNotRead = 0;
volatile uint8_t message[BUFFER_SIZE];
volatile uint8_t idx = 0;
uint8_t sendMessage[OUT_BUFFER_SIZE];

void println(char *buffer) {
    size_t len = strlen(buffer);
    char buf[len + 3];
    buf[len + 1] = '\r';
    buf[len + 2] = '\n';
    strcpy(buf, buffer);
#ifdef STM32L4
    CDC_Transmit_FS((uint8_t *) buf, len + 3);
#elif defined(STM32H7)
    CDC_Transmit_HS((uint8_t *) buf, len + 3);
    HAL_UART_Transmit(&huart5, (uint8_t *) buf, len + 3, HAL_MAX_DELAY);
    HAL_Delay(1000); // Delay between transmissions
#endif
}

void usb_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(sendMessage, OUT_BUFFER_SIZE, format, args);
    va_end(args);
    println(sendMessage);
}

void receiveData(uint8_t* data, uint32_t len) {
    for(uint32_t i = 0; i < len; i++) {
        message[idx++] = tolower(data[i]);

        if (data[i] == '\0' || data[i] == '\n') {
            // end of message
            message[idx] = '\0';

            idx = 0;
        }

        if (idx >= BUFFER_SIZE) {
            idx = 0;
            // overflow
        }
    }

    receivedNotRead = 1;

    if(!strcmp(message, DFU_COMMAND)) println("Restarting in DFU mode...");
}


void receive_periodic() {
    if(receivedNotRead) {
        receivedNotRead = 0;
        usb_printf("Received and ingested message.");
    }
}

