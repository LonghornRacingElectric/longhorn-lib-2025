//
// Created by Dhairya Gupta on 3/28/25.
//

#ifndef VCU_FIRMWARE_2025_NIGHT_CAN_H
#define VCU_FIRMWARE_2025_NIGHT_CAN_H

#include "main.h" // Includes HAL and FDCAN/STD CAN handle definition
#include "stdbool.h"

#ifdef STM32H733xx
#define NIGHT_CAN_HANDLE_TYPE FDCAN_HandleTypeDef
#define NIGHT_CAN_TxHeaderTypeDef FDCAN_TxHeaderTypeDef
#elif defined(STM32L4)
#define NIGHT_CAN_HANDLE_TYPE CAN_HandleTypeDef
#define NIGHT_CAN_TxHeaderTypeDef CAN_TxHeaderTypeDef
#endif

typedef struct CANPacket {
    uint8_t data[8];
    uint8_t dlc;
    uint32_t id;
    float period;
    float _timer;
} CANPacket;

typedef struct CANReceivedPacket {
    bool isRecent;
    uint8_t dlc;
    uint8_t data[8];
    float ageSinceRx;
    float timeLimit;
    bool isTimeout;
} CANReceivedPacket;

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

void can_periodic();

void night_can_init(NIGHT_CAN_HANDLE_TYPE *can);

/**
 * Add a CAN outbox to be sent periodically.\n
 * The period is the rate at which CAN packets of this ID are sent. \n
 * @param id ID of the CAN packet you want to add
 * @param period in seconds
 * @param outbox Pointer to CanOutbox struct
 */
void can_addOutbox(uint32_t id, float period, CANPacket *outbox);

/**
 * Add a range CAN outboxes to be sent periodically.\n
 * The period is the rate at which CAN packets of these IDs are sent. \n
 * @param idLow ID of the CAN packet you want to add
 * @param idHigh ID of the CAN packet you want to add
 * @param period in seconds
 * @param outboxes Pointer to array of CanOutbox structs
 */
void can_addOutboxes(uint32_t idLow, uint32_t idHigh, float period, CANPacket *outboxes);


/**
 * @brief Read an integral value from the packet's data buffer.
 * Reads sizeof(T) bytes starting from start_byte and interprets them as type T.
 * @param T The integer type (e.g., int8_t, uint16_t, int32_t) to read.
 * @param packet_ptr Pointer to the CANPacket structure.
 * @param start_byte The starting byte index within the 'data' buffer (0-7).
 * @return The value read from the buffer, interpreted as type T.
 * @warning Assumes packet_ptr is valid. Relies on direct pointer casting.
 * Ensure start_byte is valid for the size of T and reading type T at that
 * offset respects memory alignment rules for the target architecture.
 */
#define can_readInt(T, packet_ptr, start_byte) \
    ( *( (T *)( (packet_ptr)->data + (start_byte) ) ) )

/**
 * @brief Write an integral value to the packet's data buffer.
 * Writes sizeof(T) bytes representing 'value' starting at start_byte.
 * @param T The integer type (e.g., int8_t, uint16_t, int32_t) of the value being written.
 * @param packet_ptr Pointer to the CANPacket structure.
 * @param start_byte The starting byte index within the 'data' buffer (0-7).
 * @param value The integral value to write.
 * @warning Assumes packet_ptr is valid. Relies on direct pointer casting and assignment.
 * Ensure start_byte is valid for the size of T and writing type T at that
 * offset respects memory alignment rules. Ensure 'value' fits within type T.
 * Does NOT automatically update the packet_ptr->dlc.
 */
#define can_writeInt(T, packet_ptr, start_byte, value) \
    ( *( (T *)( (packet_ptr)->data + (start_byte) ) ) = (value) )

/**
 * @brief Write a floating point value to the packet's data buffer.
 * Scales the float by the inverse of precision, converts to integer type T,
 * and writes it starting at start_byte.
 * @param T The target integer type (e.g., int16_t, uint16_t) to store the value as.
 * @param packet_ptr Pointer to the CANPacket structure.
 * @param start_byte The starting byte index within the 'data' buffer (0-7).
 * @param value The floating-point value to write.
 * @param precision The scaling factor (resolution). value is divided by precision.
 * @warning Assumes packet_ptr is valid. Relies on direct pointer casting.
 * Ensure start_byte is valid for the size of T and writing type T at that
 * offset respects memory alignment rules. Does NOT automatically update packet_ptr->dlc.
 */
#define can_writeFloat(T, packet_ptr, start_byte, value, precision) \
    ( *( (T *)( (packet_ptr)->data + (start_byte) ) ) = (T)( (value) / (precision) ) )

/**
 * @brief Read a floating-point value from the packet's data buffer.
 * Reads an integer of type T from the buffer and scales it by 'precision'
 * to reconstruct the floating-point value.
 * @param T The underlying integer type (e.g., int16_t, uint16_t) stored in the buffer.
 * @param packet_ptr Pointer to the CANPacket structure.
 * @param start_byte The starting byte index within the 'data' buffer (0-7).
 * @param precision The scaling factor (resolution) used when the float was originally stored.
 * @return The reconstructed floating-point value.
 * @warning Assumes packet_ptr is valid. Relies on direct pointer casting for the integer read.
 * Ensure start_byte is valid for the size of T and reading type T at that
 * offset respects memory alignment rules.
 */
#define can_readFloat(T, packet_ptr, start_byte, precision) \
    ( (float)( *( (T *)( (packet_ptr)->data + (start_byte) ) ) * (precision) ) )


#endif //VCU_FIRMWARE_2025_NIGHT_CAN_H
