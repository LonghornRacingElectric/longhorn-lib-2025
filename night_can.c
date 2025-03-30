//
// Created by Dhairya Gupta on 3/28/25.
//

#include "night_can.h"
#include "usb_vcp.h"

static NIGHT_CAN_HANDLE_TYPE *nightCanHandleTypeDef;

HAL_StatusTypeDef send_CAN_data(FDCAN_HandleTypeDef* p_can, uint32_t id, uint32_t dlc, uint8_t* pData) {
    NIGHT_CAN_TxHeaderTypeDef  TxHeader;
#ifdef STM32H733xx
    // 1. Configure Transmission Header
    TxHeader.Identifier = id;                     // Set the CAN ID passed to the function
    TxHeader.IdType = FDCAN_STANDARD_ID;            // Standard ID type
    TxHeader.TxFrameType = FDCAN_DATA_FRAME;      // Data frame
    TxHeader.DataLength = dlc;                    // Set the data length passed to the function
    TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    TxHeader.BitRateSwitch = FDCAN_BRS_OFF;         // Assuming Classic CAN (no bitrate switch)
    TxHeader.FDFormat = FDCAN_CLASSIC_CAN;        // Assuming Classic CAN format
    TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS; // No Tx events stored by default


    // Input validation (optional but recommended)
    if (pData == NULL && dlc != FDCAN_DLC_BYTES_0) {
        return HAL_ERROR; // Data pointer is null but DLC > 0
    }

    // 2. Add the message to the Tx FIFO Queue
    //    The HAL function copies the data from the buffer pointed to by pData.
    return HAL_FDCAN_AddMessageToTxFifoQ(p_can, &TxHeader, &pData[0]);
#endif
}

void night_can_init(NIGHT_CAN_HANDLE_TYPE *can) {
    nightCanHandleTypeDef = can;

#ifdef STM32H7
    HAL_FDCAN_Start(nightCanHandleTypeDef);
#elif defined (STM32L4)
    HAL_CAN_Start(nightCanHandleTypeDef);
#endif
}