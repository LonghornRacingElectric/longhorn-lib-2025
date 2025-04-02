/**
 ******************************************************************************
 * @file    can_driver.c
 * @brief   Source file for abstracted CAN driver for STM32H7 and STM32L4.
 * Supports multiple CAN instances.
 ******************************************************************************
 */

#include "night_can.h" // Include the header defining structures and prototypes
#include <string.h>     // For memcpy
#include <stddef.h>     // For NULL
#include "timer.h"

// --- Static Variables for Instance Management ---

// Static array to hold pointers to active driver instances
static NightCANInstance* night_can_instances[MAX_CAN_INSTANCES] = {NULL};

// number of instances of the can
static uint32_t night_active_instances = 0;


// --- Private Helper Functions ---

/**
 * Finds the driver instance associated with a given HAL handle.
 * @param hcan Pointer to the HAL CAN handle (use FDCAN if on H7).
 * @retval Pointer to the found NightCANDriverInstance, or NULL if not found.
 */
static NightCANInstance* find_instance(NIGHTCAN_HANDLE_TYPEDEF *hcan) {
    if (!hcan) return NULL;

    // return the instance of night can
    for (uint32_t i = 0; i < night_active_instances; ++i) {
        if (night_can_instances[i] != NULL && night_can_instances[i]->hcan == hcan) {
            return night_can_instances[i];
        }
    }
    return NULL; // Instance not found for this HAL handle
}

/**
 * @brief Checks if the receive buffer for a specific instance is full.
 * @param instance Pointer to the driver instance.
 * @retval true if full, false otherwise.
 */
static bool is_rx_buffer_full(NightCANInstance *instance) {
    if (!instance) return true; // Treat NULL instance as full
    // Check if head is one position behind tail (considering wrap-around)
    return ((instance->rx_buffer_head + 1) % CAN_RX_BUFFER_SIZE) == instance->rx_buffer_tail;
}

/**
 * @brief Checks if the receive buffer for a specific instance is empty.
 * @param instance Pointer to the driver instance.
 * @retval true if empty, false otherwise.
 */
static bool is_rx_buffer_empty(NightCANInstance *instance) {
    if (!instance) return true; // Treat NULL instance as empty
    // Check if head and tail indices are the same
    return instance->rx_buffer_head == instance->rx_buffer_tail;
}

/**
 * @brief Adds a received message to the ring buffer of a specific instance.
 * @param instance Pointer to the driver instance.
 * @param rx_header Pointer to the received message header.
 * @param rx_data Pointer to the received data payload.
 */
static void add_to_rx_buffer(NightCANInstance *instance, NIGHTCAN_RX_HANDLETYPEDEF *rx_header, uint8_t *rx_data) {
    // Basic check for valid instance and header
    if (!instance || !rx_header || !rx_data) return;

    // Check if the instance's buffer is full
    if (is_rx_buffer_full(instance)) {
        // buffer overflowed so we shall discard the newest message
        instance->rx_overflow_count++;

        // this will overwrite the older packet, burt also dangerous because we might miss something
        instance->rx_buffer_tail = (instance->rx_buffer_tail + 1) % CAN_RX_BUFFER_SIZE;
        return;
    }

    // Get pointer to the next available slot in the instance's buffer
    NightCANReceivePacket *packet = &instance->rx_buffer[instance->rx_buffer_head];

#ifdef STM32L4xx
    packet->id = (rx_header->IDE == CAN_ID_STD) ? rx_header->StdId : rx_header->ExtId;
    packet->ide = rx_header->IDE;
    packet->rtr = rx_header->RTR;
    packet->dlc = rx_header->DLC;
    packet->timestamp_ms = lib_timer_elapsed_ms();
    packet->filter_index = rx_header->FilterMatchIndex;
    // Ensure we don't copy more data than the buffer holds or DLC indicates
    uint8_t len_to_copy = (packet->dlc > 8) ? 8 : packet->dlc;
    memcpy(packet->data, rx_data, len_to_copy);

    // Advance head index for the instance's buffer (with wrap-around)
    instance->rx_buffer_head = (instance->rx_buffer_head + 1) % CAN_RX_BUFFER_SIZE;
#elif defined(STM32H733xx)
    packet->id = rx_header->Identifier;
    packet->ide = rx_header->IdType;
    packet->dlc = rx_header->DataLength;
    packet->timestamp_ms = lib_timer_elapsed_ms(); // Use HAL tick for timestamp
    packet->filter_index = rx_header->FilterIndex;
    // Ensure we don't copy more data than the buffer holds or DLC indicates
    uint8_t len_to_copy = (packet->dlc > 8) ? 8 : packet->dlc;
    memcpy(packet->data, rx_data, len_to_copy);

    // Advance head index for the instance's buffer (with wrap-around)
    instance->rx_buffer_head = (instance->rx_buffer_head + 1) % CAN_RX_BUFFER_SIZE;
#endif
}

/**
 * @brief Sends a CAN packet immediately using HAL for a specific instance.
 * @param instance Pointer to the driver instance.
 * @param packet Pointer to the NightCANPacket to send.
 * @retval CANDriverStatus status code.
 */
static CANDriverStatus send_immediate(NightCANInstance *instance, NightCANPacket *packet) {
    // Check for valid instance and packet pointers
    if (!instance || !instance->initialized || !packet) {
        return CAN_INSTANCE_NULL; // Or CAN_INVALID_PARAM
    }

    if (!instance->hcan) {
        return CAN_ERROR; // no handle
    }

    NIGHTCAN_TX_HANDLETYPEDEF tx_header;

#ifdef STM32L4xx
    // Prepare the HAL transmit header from our packet structure
    tx_header.DLC = packet->dlc;
    tx_header.RTR = packet->rtr;
    tx_header.IDE = packet->ide;
    if (packet->ide == CAN_ID_STD) {
        tx_header.StdId = packet->id;
    } else {
        tx_header.ExtId = packet->id;
    }
#elif defined(STM32H733xx)
    // fro fdcan in h7 chip wahooo
    tx_header.DataLength = packet->dlc;
    tx_header.IdType = packet->ide;
    tx_header.Identifier = packet->id;
    tx_header.IdType = FDCAN_STANDARD_ID;            // Standard ID type
    tx_header.TxFrameType = FDCAN_DATA_FRAME;      // Data frame
    tx_header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    tx_header.BitRateSwitch = FDCAN_BRS_OFF;
    tx_header.FDFormat = FDCAN_CLASSIC_CAN;
    tx_header.TxEventFifoControl = FDCAN_NO_TX_EVENTS; // No Tx events stored by default
#endif

    // --- Platform-Specific HAL Calls ---
#if defined(STM32H733xx) // Use the specific define from can_driver.h
    // FDCAN (H7) specific handling
    // Assume HAL_FDCAN_AddMessageToTxFifoQ handles queuing/mailbox checking internally
    tx_header.TxFrameType = FDCAN_DATA_FRAME; // Adjust if needed (e.g., FDCAN_BRS_ON)
    HAL_StatusTypeDef hal_status = HAL_FDCAN_AddMessageToTxFifoQ(instance->hcan, &tx_header, packet->data);
#elif defined(STM32L4xx) // Use the specific define from can_driver.h
    // bxCAN (L4) specific handling
        // Check available mailboxes before attempting to send
        if (HAL_CAN_GetTxMailboxesFreeLevel(instance->hcan) == 0) {
            return CAN_BUSY; // No free mailboxes
        }
        HAL_StatusTypeDef hal_status = HAL_CAN_AddTxMessage(instance->hcan, &tx_header, packet->data, &tx_mailbox);
    #else
        #error "Unsupported STM32 series for CAN transmission in can_driver.c"
        return CAN_ERROR; // Should not reach here if header check passed
#endif


    // Check the result of the HAL transmission function
    if (hal_status == HAL_OK) {
        return CAN_OK;
    } else if (hal_status == HAL_BUSY) {
        // This might indicate FIFO full (FDCAN) or mailboxes full (bxCAN)
        return CAN_BUSY;
    } else {
        // Other HAL error (timeout, parameter error, etc.)
        return CAN_ERROR;
    }
}


// --- Public API Functions ---

/**
 * @brief Initializes a CAN peripheral instance and the driver state for it.
 */
CANDriverStatus CAN_Init(NightCANInstance *instance, NIGHTCAN_HANDLE_TYPEDEF *hcan, uint32_t default_filter_id_1, uint32_t default_filter_id_2) {
    // Check for valid pointers and available instance slots
    if (!instance) return CAN_INVALID_PARAM;
    if (!hcan) return CAN_INVALID_PARAM;
    if (night_active_instances >= MAX_CAN_INSTANCES) {
        return CAN_MAX_INSTANCES_REACHED;
    }

    // --- Initialize Instance Structure ---
    memset(instance, 0, sizeof(NightCANInstance)); // Clear the structure
    instance->hcan = hcan;
    instance->initialized = false; // Mark as not initialized until setup succeeds
    // Buffers/counts are already zeroed by memset


    // add this to our instances
    night_can_instances[night_active_instances] = instance;
    night_active_instances++;

    // to set up filter, bascially only using this on L4
    NIGHTCAN_FILTERTYPEDEF sFilterConfig;

#if defined(STM32H733xx)

#elif defined(STM32L4xx)
        sFilterConfig.FilterBank = 0;                     // Filter bank index (0 to 13 for single CAN)
        sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK; // Mask mode
        sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;// 32-bit scale
        // Mask calculation for standard ID in 32-bit mask mode
        sFilterConfig.FilterIdHigh = (default_filter_id << 5) & 0xFFFF;
        sFilterConfig.FilterIdLow = 0x0000; // RTR, IDE bits = 0
        sFilterConfig.FilterMaskIdHigh = (default_filter_mask << 5) & 0xFFFF;
        sFilterConfig.FilterMaskIdLow = 0x0000 | CAN_ID_EXT | CAN_RTR_REMOTE; // Mask IDE and RTR bits if needed (0 = don't care)

        sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0; // Assign to FIFO0
        sFilterConfig.FilterActivation = ENABLE;          // Enable this filter
        sFilterConfig.SlaveStartFilterBank = 14;          // Relevant for dual CAN instances, set to 14 for single CAN

        if (HAL_CAN_ConfigFilter(instance->hcan, &sFilterConfig) != HAL_OK) {
             g_active_instances--;
             g_can_instances[g_active_instances] = NULL;
            return CAN_ERROR; // Error configuring filter
        }
    #else
        #error "Unsupported STM32 series for CAN filter configuration in can_driver.c"
        g_active_instances--;
        g_can_instances[g_active_instances] = NULL;
        return CAN_ERROR;
#endif

    // --- Activate CAN Notifications ---
    // Common notifications: Rx FIFO 0/1 message pending
#if defined(STM32H733xx)
//    if (HAL_FDCAN_ActivateNotification(instance->hcan, FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO1_NEW_MESSAGE, 0) != HAL_OK) {
//        night_active_instances--;
//        night_can_instances[night_active_instances] = NULL;
//        return CAN_ERROR;
//    }
#elif defined(STM32L4xx)
    // bxCAN Notifications
        if (HAL_CAN_ActivateNotification(instance->hcan, CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_RX_FIFO1_MSG_PENDING) != HAL_OK) {
             g_active_instances--;
             g_can_instances[g_active_instances] = NULL;
            return CAN_ERROR;
        }

        HAL_CAN_Start(hcan);
         // Activate error notifications (optional but recommended)
         HAL_CAN_ActivateNotification(instance->hcan, CAN_IT_ERROR_WARNING | CAN_IT_ERROR_PASSIVE | CAN_IT_BUSOFF | CAN_IT_LAST_ERROR_CODE | CAN_IT_ERROR);
#endif


    // --- Start CAN Peripheral ---
#if defined(STM32H733xx)
    if (HAL_FDCAN_Start(instance->hcan) != HAL_OK) {
        night_active_instances--;
        night_can_instances[night_active_instances] = NULL;
        return CAN_ERROR;
    }
#elif defined(STM32L4xx)
    if (HAL_CAN_Start(instance->hcan) != HAL_OK) {
             g_active_instances--;
             g_can_instances[g_active_instances] = NULL;
            return CAN_ERROR;
        }
#endif

    instance->initialized = true;
    return CAN_OK;
}

/**
 * @brief Adds a CAN packet to the transmission schedule or sends it immediately for a specific instance.
 */
CANDriverStatus CAN_AddTxPacket(NightCANInstance *instance, NightCANPacket *packet) {
    // error checking
    if (!instance || !instance->initialized) return CAN_INSTANCE_NULL;
    if (!packet) return CAN_INVALID_PARAM;

    if (packet->dlc > 8) {
        return CAN_INVALID_PARAM; // Invalid data length for classic CAN
    }

    // 0 interval means its nota scheduled packet
    if (packet->tx_interval_ms == 0) {
        // Send immediately (one-shot)
        return send_immediate(instance, packet);
    } else {
        // check if we have space to schedule this (we should be fine).
        if (instance->tx_schedule_count >= CAN_TX_SCHEDULE_SIZE) {
            return CAN_BUFFER_FULL; // Schedule is full for this instance
        }

        // make sure this packet isnt already scheudled for this can bus -- if it is, juyst uypdate the inverval
        for (uint32_t i = 0; i < instance->tx_schedule_count; i++) {
            if (instance->tx_schedule[i] == packet) {
                // Already scheduled, update interval and return OK
                instance->tx_schedule[i]->tx_interval_ms = packet->tx_interval_ms;
                instance->tx_schedule[i]->_is_scheduled = true;
                return CAN_OK;
            }
        }

        // add this shire to the scheudle.
        packet->_last_tx_time_ms = lib_timer_elapsed_ms();
        packet->_is_scheduled = true;
        instance->tx_schedule[instance->tx_schedule_count++] = packet;
        return CAN_OK;
    }
}

/**
 * @brief Removes a previously scheduled periodic packet from the transmission schedule for a specific instance.
 */
CANDriverStatus CAN_RemoveScheduledTxPacket(NightCANInstance *instance, NightCANPacket *packet) {
    // error check
    if (!instance || !instance->initialized) return CAN_INSTANCE_NULL;
    if (!packet) return CAN_INVALID_PARAM;

    // look for packetm if we find it, send scheudled to false and remove from the transmit queue
    for (uint32_t i = 0; i < instance->tx_schedule_count; ++i) {
        if (instance->tx_schedule[i] == packet) {
            instance->tx_schedule[i]->_is_scheduled = false;

            for (uint32_t j = i; j < instance->tx_schedule_count - 1; ++j) {
                instance->tx_schedule[j] = instance->tx_schedule[j + 1];
            }

            instance->tx_schedule[instance->tx_schedule_count - 1] = NULL;
            instance->tx_schedule_count--;
            return CAN_OK;
        }
    }

    return CAN_NOT_FOUND; // Packet not found in this instance's schedule
}


/**
 * @brief Retrieves the oldest received CAN packet from the buffer for a specific instance.
 */
CANDriverStatus CAN_GetReceivedPacket(NightCANInstance *instance, NightCANReceivePacket *received_packet) {
    // Check for valid instance and output pointer
    if (!instance || !instance->initialized) return CAN_INSTANCE_NULL;
    if (!received_packet) return CAN_INVALID_PARAM;

    // Check if the instance's buffer is empty
    if (is_rx_buffer_empty(instance)) {
        return CAN_BUFFER_EMPTY;
    }

    // Copy data from the instance's buffer at the tail position
    *received_packet = instance->rx_buffer[instance->rx_buffer_tail];

    // Advance the instance's tail index (with wrap-around)
    instance->rx_buffer_tail = (instance->rx_buffer_tail + 1) % CAN_RX_BUFFER_SIZE;


    return CAN_OK;
}

/**
 * @brief Polls the hardware FIFOs for received messages and moves them to the driver's buffer.
 */
CANDriverStatus CAN_PollReceive(NightCANInstance *instance) {
    if (!instance || !instance->initialized) return CAN_INSTANCE_NULL;
    if (!instance->hcan) return CAN_ERROR;

    // --- Platform specific polling ---
#if defined(STM32H733xx)
    uint32_t fill_level0 = HAL_FDCAN_GetRxFifoFillLevel(instance->hcan, FDCAN_RX_FIFO0);
    uint32_t fill_level1 = HAL_FDCAN_GetRxFifoFillLevel(instance->hcan, FDCAN_RX_FIFO1);
    FDCAN_RxHeaderTypeDef rx_header;
    uint8_t rx_data[8];

    // Poll FIFO 0
    while (fill_level0 > 0) {
        if (HAL_FDCAN_GetRxMessage(instance->hcan, FDCAN_RX_FIFO0, &rx_header, rx_data) == HAL_OK) {
            add_to_rx_buffer(instance, &rx_header, rx_data);
        } else {
            // Error getting message from FIFO0, maybe break or log?
            break; // Avoid infinite loop if GetRxMessage fails repeatedly
        }
        fill_level0--; // Decrement manually as we process one message
        // Re-check fill level if GetRxMessage could fail without consuming msg?
        // fill_level0 = HAL_FDCAN_GetRxFifoFillLevel(instance->hcan, FDCAN_RX_FIFO0);
    }

    // Poll FIFO 1
    while (fill_level1 > 0) {
        if (HAL_FDCAN_GetRxMessage(instance->hcan, FDCAN_RX_FIFO1, &rx_header, rx_data) == HAL_OK) {
            add_to_rx_buffer(instance, &rx_header, rx_data);
        } else {
            // Error getting message from FIFO1
            break;
        }
        fill_level1--;
        // fill_level1 = HAL_FDCAN_GetRxFifoFillLevel(instance->hcan, FDCAN_RX_FIFO1);
    }

#elif defined(STM32L4xx)
    uint32_t fill_level0 = HAL_CAN_GetRxFifoFillLevel(instance->hcan, CAN_RX_FIFO0);
        uint32_t fill_level1 = HAL_CAN_GetRxFifoFillLevel(instance->hcan, CAN_RX_FIFO1);
        CAN_RxHeaderTypeDef rx_header;
        uint8_t rx_data[8];

        // Poll FIFO 0
        while (fill_level0 > 0) {
             if (HAL_CAN_GetRxMessage(instance->hcan, CAN_RX_FIFO0, &rx_header, rx_data) == HAL_OK) {
                 add_to_rx_buffer(instance, &rx_header, rx_data);
             } else {
                 break; // Error
             }
             fill_level0--;
             // fill_level0 = HAL_CAN_GetRxFifoFillLevel(instance->hcan, CAN_RX_FIFO0);
        }

        // Poll FIFO 1
        while (fill_level1 > 0) {
             if (HAL_CAN_GetRxMessage(instance->hcan, CAN_RX_FIFO1, &rx_header, rx_data) == HAL_OK) {
                 add_to_rx_buffer(instance, &rx_header, rx_data);
             } else {
                 break; // Error
             }
             fill_level1--;
             // fill_level1 = HAL_CAN_GetRxFifoFillLevel(instance->hcan, CAN_RX_FIFO1);
        }
    #else
        #error "Unsupported STM32 series for CAN polling"
        return CAN_ERROR;
#endif

    return CAN_OK;
}

/**
 * @brief Services the CAN driver for a specific instance (handles periodic transmissions).
 */
void CAN_Service(NightCANInstance *instance) {
    // make sure that the we are actually ready to send (filtering stupid mistakes)
    if (!instance || !instance->initialized || !instance->hcan) return;

    uint32_t current_time_ms = lib_timer_elapsed_ms();

   // go through every scheduled packet and send it
    for (uint32_t i = 0; i < instance->tx_schedule_count; i++) {
        NightCANPacket *packet = instance->tx_schedule[i];

        // make sure packet exists and that it is shceudled to be sent (should be), also make sure that this one
        // is nto a one time send packet (if ms interval is 0, it is meant ot only be sent once (crazy right?)) ⛺︎
        if (packet != NULL && packet->_is_scheduled && packet->tx_interval_ms > 0) {
            // make sure that we have waiited long enough
            if ((current_time_ms - packet->_last_tx_time_ms) >= packet->tx_interval_ms) {
                if (send_immediate(instance, packet) == CAN_OK) {
                    // Update last transmission time on successful send
                    packet->_last_tx_time_ms = current_time_ms;
                }
            }
        }
    }
}

/**
 * Creates a packet to be used by the driver. Use this to set up the packet, like a constructor! You NEED to use
 * the packet that is returned because it is the pointer that is stored in the CAN instance's struct. Lock in.
 * @param id
 * @param interval_ms
 * @param dlc
 * @return
 */
NightCANPacket CAN_create_packet(uint32_t id, uint32_t interval_ms, uint8_t dlc) {
    NightCANPacket packet;
    packet.tx_interval_ms = interval_ms;
    packet.id = id;
    packet.dlc = dlc;


    return packet;
};

/**
 * @brief Configures an additional CAN filter for a specific instance.
 */
CANDriverStatus CAN_ConfigFilter(NightCANInstance *instance, uint32_t filter_bank, uint32_t filter_id, uint32_t filter_mask) {
    // Check for valid instance
    if (!instance || !instance->initialized || !instance->hcan) return CAN_INSTANCE_NULL;

    NIGHTCAN_FILTERTYPEDEF sFilterConfig; // Use base HAL type

#if defined(STM32H733xx)
    // FDCAN Filter Configuration
    sFilterConfig.IdType = FDCAN_STANDARD_ID; // Assuming standard ID, adjust if needed (e.g., FDCAN_EXTENDED_ID)
    sFilterConfig.FilterIndex = filter_bank; // Index of the filter to configure
    sFilterConfig.FilterType = FDCAN_FILTER_MASK; // Or FDCAN_FILTER_RANGE etc.
    sFilterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0; // Or FDCAN_FILTER_TO_RX_FIFO1, FDCAN_FILTER_REJECT
    sFilterConfig.FilterID1 = filter_id;      // ID or start of range
    sFilterConfig.FilterID2 = filter_mask;    // Mask or end of range
#elif defined(STM32L4xx)
        sFilterConfig.FilterBank = filter_bank;           // Filter bank number (0..13 or 0..27 depending on device)
        sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK; // Or CAN_FILTERMODE_IDLIST
        sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;// Or CAN_FILTERSCALE_16BIT
        // Setup for 32-bit mask mode, Standard ID example:
        sFilterConfig.FilterIdHigh = (filter_id << 5) & 0xFFFF;
        sFilterConfig.FilterIdLow = 0x0000; // Set IDE=0, RTR=0 bits if needed
        sFilterConfig.FilterMaskIdHigh = (filter_mask << 5) & 0xFFFF;
        sFilterConfig.FilterMaskIdLow = 0x0000 | CAN_ID_EXT | CAN_RTR_REMOTE; // Mask IDE/RTR bits (0 = don't care)
        sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0; // Or CAN_RX_FIFO1
        sFilterConfig.FilterActivation = ENABLE;
        sFilterConfig.SlaveStartFilterBank = 14; // Adjust if using dual CAN on L4

        if (HAL_CAN_ConfigFilter(instance->hcan, &sFilterConfig) != HAL_OK) {
            return CAN_ERROR;
        }
    #else
         #error "Unsupported STM32 series for CAN filter configuration in can_driver.c"
         return CAN_ERROR;
#endif

    return CAN_OK;
}


// --- HAL Callback Implementations ---
// These functions are called by the HAL library globally for any CAN instance.
// We need to find which specific driver instance triggered the callback.
// ALL of this is for interrupt mode (which we probably won't ever use -- this part is ai generated tbh)

/**
 * @brief Common RX message processing logic called by FIFO callbacks.
 * @param instance Pointer to the specific driver instance.
 * @param RxFifo The FIFO identifier (e.g., CAN_RX_FIFO0 or FDCAN_RX_FIFO0).
 */
static void ProcessRxMessage(NightCANInstance *instance, uint32_t RxFifo) {
    // Basic check
    if (!instance || !instance->initialized || !instance->hcan) return;

    NIGHTCAN_RX_HANDLETYPEDEF rx_header; // Use base HAL type
    uint8_t rx_data[8];            // Max data length is 8 for classic CAN

    // Get the message from the appropriate FIFO using the instance's handle
#if defined(STM32H733xx)
    HAL_StatusTypeDef status = HAL_FDCAN_GetRxMessage(instance->hcan, RxFifo, &rx_header, rx_data);
#elif defined(STM32L4xx)
    HAL_StatusTypeDef status = HAL_CAN_GetRxMessage(instance->hcan, RxFifo, &rx_header, rx_data);
    #else
        #error "Unsupported STM32 series for GetRxMessage in can_driver.c"
        return;
#endif

    if (status == HAL_OK) {
        // Add the received message to the specific instance's buffer
        add_to_rx_buffer(instance, &rx_header, rx_data);
    } else {
        // Handle error getting message (optional: log error, increment instance error counter)
        // instance->rx_error_count++;
    }
}


/**
 * @brief HAL CAN RX FIFO 0 Message Pending Callback.
 */
void HAL_CAN_RxFifo0MsgPendingCallback(NIGHTCAN_HANDLE_TYPEDEF *hcan) {
    // Find which driver instance this callback belongs to
    NightCANInstance* instance = find_instance(hcan);
    if (instance) {
        // Call the processing function for the found instance
#if defined(STM32H733xx)
        ProcessRxMessage(instance, FDCAN_RX_FIFO0);
#elif defined(STM32L4xx)
        ProcessRxMessage(instance, CAN_RX_FIFO0);
#endif
    }
    // Else: Callback received for an unknown/uninitialized CAN handle
}

/**
 * @brief HAL CAN RX FIFO 1 Message Pending Callback.
 */
void HAL_CAN_RxFifo1MsgPendingCallback(NIGHTCAN_HANDLE_TYPEDEF *hcan) {
    // Find which driver instance this callback belongs to
    NightCANInstance* instance = find_instance(hcan);
    if (instance) {
        // Call the processing function for the found instance
#if defined(STM32H733xx)
        ProcessRxMessage(instance, FDCAN_RX_FIFO1);
#elif defined(STM32L4xx)
        ProcessRxMessage(instance, CAN_RX_FIFO1);
#endif
    }
    // Else: Callback received for an unknown/uninitialized CAN handle
}
