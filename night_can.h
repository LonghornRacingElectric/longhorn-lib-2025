//
// Created by Dhairya Gupta on 3/28/25.
//

#ifndef VCU_FIRMWARE_2025_NIGHT_CAN_H
#define VCU_FIRMWARE_2025_NIGHT_CAN_H

#include "main.h" // Includes HAL and FDCAN/STD CAN handle definition

#ifdef STM32H733xx
#define NIGHT_CAN_HANDLE_TYPE FDCAN_HandleTypeDef
#define NIGHT_CAN_TxHeaderTypeDef FDCAN_TxHeaderTypeDef
#elif defined(STM32L4)
#define CAN_HANDLE_TYPE CAN_HandleTypeDef
#endif

/**
  * @brief  Sends a standard CAN Data Frame message.
  * @param  p_hfdcan Pointer to the FDCAN handle (e.g., &hfdcan1).
  * @param  id The Standard CAN ID (11-bit).
  * @param  dlc The Data Length Code (use FDCAN_DLC_BYTES_0 to FDCAN_DLC_BYTES_8).
  * @param  pData Pointer to the data buffer (array of uint8_t) containing the payload.
  * The buffer must contain at least 'dlc' bytes.
  * @retval HAL status (HAL_OK, HAL_ERROR, HAL_BUSY). The caller should check this.
  */
HAL_StatusTypeDef send_CAN_data(NIGHT_CAN_HANDLE_TYPE* p_can, uint32_t id, uint32_t dlc, uint8_t* pData);

#endif //VCU_FIRMWARE_2025_NIGHT_CAN_H
