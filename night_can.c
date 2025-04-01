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
static NightCANDriverInstance* g_can_instances[MAX_CAN_INSTANCES] = {NULL};
// Counter for the number of active/initialized instances
static uint32_t g_active_instances = 0;


// --- Private Helper Functions ---

/**
 * @brief Finds the driver instance associated with a given HAL handle.
 * @param hcan Pointer to the HAL CAN handle.
 * @retval Pointer to the found NightCANDriverInstance, or NULL if not found.
 */
static NightCANDriverInstance* find_instance(NIGHTCAN_HANDLE_TYPEDEF *hcan) {
    if (!hcan) return NULL;
    for (uint32_t i = 0; i < g_active_instances; ++i) {
        // Check if the pointer is valid and the HAL handle matches
        if (g_can_instances[i] != NULL && g_can_instances[i]->hcan == hcan) {
            return g_can_instances[i];
        }
    }
    return NULL; // Instance not found for this HAL handle
}

/**
 * @brief Checks if the receive buffer for a specific instance is full.
 * @param instance Pointer to the driver instance.
 * @retval true if full, false otherwise.
 */
static bool is_rx_buffer_full(NightCANDriverInstance *instance) {
    if (!instance) return true; // Treat NULL instance as full
    // Check if head is one position behind tail (considering wrap-around)
    return ((instance->rx_buffer_head + 1) % CAN_RX_BUFFER_SIZE) == instance->rx_buffer_tail;
}

/**
 * @brief Checks if the receive buffer for a specific instance is empty.
 * @param instance Pointer to the driver instance.
 * @retval true if empty, false otherwise.
 */
static bool is_rx_buffer_empty(NightCANDriverInstance *instance) {
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
static void add_to_rx_buffer(NightCANDriverInstance *instance, NIGHTCAN_RX_HANDLETYPEDEF *rx_header, uint8_t *rx_data) {
    // Basic check for valid instance and header
    if (!instance || !rx_header || !rx_data) return;

    // Check if the instance's buffer is full
    if (is_rx_buffer_full(instance)) {
        // Handle buffer overflow - discard newest message and increment counter
        instance->rx_overflow_count++;
        // Optional: Advance tail to overwrite oldest:
        // instance->rx_buffer_tail = (instance->rx_buffer_tail + 1) % CAN_RX_BUFFER_SIZE;
        return;
    }

    // Get pointer to the next available slot in the instance's buffer
    NightCANReceivePacket *packet = &instance->rx_buffer[instance->rx_buffer_head];

    // Copy data from HAL structures to our receive packet structure
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
static CANDriverStatus send_immediate(NightCANDriverInstance *instance, NightCANPacket *packet) {
    // Check for valid instance and packet pointers
    if (!instance || !instance->initialized || !packet) {
        return CAN_INSTANCE_NULL; // Or CAN_INVALID_PARAM
    }
    if (!instance->hcan) {
        return CAN_ERROR; // HAL handle not set
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
CANDriverStatus CAN_Init(NightCANDriverInstance *instance, NIGHTCAN_HANDLE_TYPEDEF *hcan, uint32_t default_filter_id_1, uint32_t default_filter_id_2) {
    // Check for valid pointers and available instance slots
    if (!instance) return CAN_INVALID_PARAM;
    if (!hcan) return CAN_INVALID_PARAM;
    if (g_active_instances >= MAX_CAN_INSTANCES) {
        return CAN_MAX_INSTANCES_REACHED;
    }

    // Check if this HAL handle is already registered
    if (find_instance(hcan) != NULL) {
        // Allow re-initialization? Or return error? Currently allowing.
        // If returning error: return CAN_ERROR;
    }

    // --- Initialize Instance Structure ---
    memset(instance, 0, sizeof(NightCANDriverInstance)); // Clear the structure
    instance->hcan = hcan;
    instance->initialized = false; // Mark as not initialized until setup succeeds
    // Buffers/counts are already zeroed by memset

    // --- Register Instance ---
    // Find the first NULL spot or append if no re-initialization check above
    // Simple append for now:
    g_can_instances[g_active_instances] = instance;
    g_active_instances++;


    // --- Configure Default Filter (Filter Bank 0) ---
    // Note: Filter configuration differs significantly between FDCAN (H7) and bxCAN (L4)
    NIGHTCAN_FILTERTYPEDEF sFilterConfig; // Use the base HAL type

#if defined(STM32H733xx) // Use the specific define from can_driver.h
    // FDCAN Filter Configuration (Example for standard ID, classic CAN)
    sFilterConfig.IdType = FDCAN_STANDARD_ID;         // STD ID
    sFilterConfig.FilterIndex = 0;                    // Filter bank index
    sFilterConfig.FilterType = FDCAN_FILTER_RANGE;     // Mask mode
    sFilterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0; // Store in FIFO0
    sFilterConfig.FilterID1 = default_filter_id_1;      // The ID to match
    sFilterConfig.FilterID2 = default_filter_id_2;    // The mask applied to FilterID1

//    if (HAL_FDCAN_ConfigFilter(instance->hcan, &sFilterConfig) != HAL_OK) {
//        // Consider unregistering the instance on failure
//        g_active_instances--;
//        g_can_instances[g_active_instances] = NULL;
//        return CAN_ERROR; // Error configuring filter
//    }
//    // Configure global filter settings (accept non-matching frames, reject remote)
//    if (HAL_FDCAN_ConfigGlobalFilter(instance->hcan, FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE) != HAL_OK) {
//        g_active_instances--;
//        g_can_instances[g_active_instances] = NULL;
//        return CAN_ERROR;
//    }

#elif defined(STM32L4xx) // Use the specific define from can_driver.h
    // bxCAN Filter Configuration (Example for 32-bit mask mode)
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
    // FDCAN Notifications
    if (HAL_FDCAN_ActivateNotification(instance->hcan, FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO1_NEW_MESSAGE, 0) != HAL_OK) {
        g_active_instances--;
        g_can_instances[g_active_instances] = NULL;
        return CAN_ERROR;
    }

    // Activate error notifications (optional but recommended)
//    HAL_FDCAN_ActivateNotification(instance->hcan, FDCAN_IT_BUS_OFF | FDCAN_IT_ERROR_WARNING | FDCAN_IT_ERROR_PASSIVE, FDCAN_TX_BUFFER_ALL); // Monitor all Tx buffers for errors too

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
        g_active_instances--;
        g_can_instances[g_active_instances] = NULL;
        return CAN_ERROR;
    }
#elif defined(STM32L4xx)
    if (HAL_CAN_Start(instance->hcan) != HAL_OK) {
             g_active_instances--;
             g_can_instances[g_active_instances] = NULL;
            return CAN_ERROR;
        }
#endif

    // Mark instance as successfully initialized
    instance->initialized = true;
    return CAN_OK;
}

/**
 * @brief Adds a CAN packet to the transmission schedule or sends it immediately for a specific instance.
 */
CANDriverStatus CAN_AddTxPacket(NightCANDriverInstance *instance, NightCANPacket *packet) {
    // Check for valid instance and packet
    if (!instance || !instance->initialized) return CAN_INSTANCE_NULL;
    if (!packet) return CAN_INVALID_PARAM;

    // Validate DLC
    if (packet->dlc > 8) {
        return CAN_INVALID_PARAM; // Invalid data length for classic CAN
    }

    if (packet->tx_interval_ms == 0) {
        // Send immediately (one-shot)
        return send_immediate(instance, packet);
    } else {
        // --- Add to periodic schedule for this instance ---

        // Check if the instance's schedule is full
        if (instance->tx_schedule_count >= CAN_TX_SCHEDULE_SIZE) {
            return CAN_BUFFER_FULL; // Schedule is full for this instance
        }

        // Check if this exact packet instance is already scheduled *for this instance*
        for (uint32_t i = 0; i < instance->tx_schedule_count; ++i) {
            if (instance->tx_schedule[i] == packet) {
                // Already scheduled, update interval and return OK
                instance->tx_schedule[i]->tx_interval_ms = packet->tx_interval_ms;
                instance->tx_schedule[i]->_is_scheduled = true; // Ensure it's marked active
                return CAN_OK;
            }
        }

        // Add to the instance's schedule
        packet->_last_tx_time_ms = lib_timer_elapsed_ms(); // Initialize last tx time
        packet->_is_scheduled = true;             // Mark as actively scheduled
        instance->tx_schedule[instance->tx_schedule_count++] = packet; // Store pointer
        return CAN_OK;
    }
}

/**
 * @brief Removes a previously scheduled periodic packet from the transmission schedule for a specific instance.
 */
CANDriverStatus CAN_RemoveScheduledTxPacket(NightCANDriverInstance *instance, NightCANPacket *packet) {
    // Check for valid instance and packet
    if (!instance || !instance->initialized) return CAN_INSTANCE_NULL;
    if (!packet) return CAN_INVALID_PARAM;

    // Find the packet in the instance's schedule
    for (uint32_t i = 0; i < instance->tx_schedule_count; ++i) {
        if (instance->tx_schedule[i] == packet) {
            // Found the packet
            instance->tx_schedule[i]->_is_scheduled = false; // Mark as unscheduled (important for CAN_Service)

            // Shift remaining elements down to remove the gap
            for (uint32_t j = i; j < instance->tx_schedule_count - 1; ++j) {
                instance->tx_schedule[j] = instance->tx_schedule[j + 1];
            }
            // Clear the last element and decrement count
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
CANDriverStatus CAN_GetReceivedPacket(NightCANDriverInstance *instance, NightCANReceivePacket *received_packet) {
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
 * @brief Services the CAN driver for a specific instance (handles periodic transmissions).
 */
void CAN_Service(NightCANDriverInstance *instance) {
    // Check for valid instance
    if (!instance || !instance->initialized || !instance->hcan) return;

    uint32_t current_time_ms = lib_timer_elapsed_ms();

    // Check scheduled packets for this instance
    for (uint32_t i = 0; i < instance->tx_schedule_count; ++i) {
        NightCANPacket *packet = instance->tx_schedule[i];

        // Check if the packet pointer is valid and if it's still actively scheduled
        if (packet != NULL && packet->_is_scheduled && packet->tx_interval_ms > 0) {
            // Check if interval has elapsed (handle timer wrap-around using subtraction)
            // Ensure subtraction handles wrap-around correctly (unsigned arithmetic)
            if ((current_time_ms - packet->_last_tx_time_ms) >= packet->tx_interval_ms) {
                // Time to send this packet for this instance
                if (send_immediate(instance, packet) == CAN_OK) {
                    // Update last transmission time only on successful send
                    packet->_last_tx_time_ms = current_time_ms;
                } else {
                    // Optional: Handle Tx failure (e.g., log error, retry later?)
                    // Avoid blocking CAN_Service. Maybe set a flag in the packet struct.
                }
            }
        }
    }

    // Add any other periodic tasks needed by the driver for this instance here.
    // E.g., check error states, perform recovery actions.
#if defined(STM32H733xx) || defined(STM32L4xx) // Check if error handling is needed
    // Example: Check for Bus Off state
//    if (HAL_CAN_GetError(instance->hcan) & HAL_CAN_ERROR_BOF) {
//        // Handle Bus Off - maybe try re-initialization after a delay
//        // Be careful: Re-init might require stopping tasks using this CAN instance.
//        // Consider setting an error flag in the instance struct for the main app to handle.
//        // instance->error_flags |= CAN_ERROR_BUS_OFF_FLAG;
//    }
#endif
}

/**
 * @brief Configures an additional CAN filter for a specific instance.
 */
CANDriverStatus CAN_ConfigFilter(NightCANDriverInstance *instance, uint32_t filter_bank, uint32_t filter_id, uint32_t filter_mask) {
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

//    if (HAL_FDCAN_ConfigFilter(instance->hcan, &sFilterConfig) != HAL_OK) {
//        return CAN_ERROR;
//    }
#elif defined(STM32L4xx)
    // bxCAN Filter Configuration
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

/**
 * @brief Common RX message processing logic called by FIFO callbacks.
 * @param instance Pointer to the specific driver instance.
 * @param RxFifo The FIFO identifier (e.g., CAN_RX_FIFO0 or FDCAN_RX_FIFO0).
 */
static void ProcessRxMessage(NightCANDriverInstance *instance, uint32_t RxFifo) {
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
    NightCANDriverInstance* instance = find_instance(hcan);
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
    NightCANDriverInstance* instance = find_instance(hcan);
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

/**
 * @brief HAL CAN Error Callback.
 */
//void HAL_CAN_ErrorCallback(NIGHTCAN_HANDLE_TYPEDEF *hcan) {
//    // Find which driver instance this callback belongs to
//    NightCANDriverInstance* instance = find_instance(hcan);
//    if (instance) {
//        // Handle CAN errors for this specific instance
//        uint32_t error_code = HAL_CAN_GetError(instance->hcan);
//
//        // Example: Log error code or set flags in the instance structure
//        // instance->last_error_code = error_code;
//        // instance->error_flags |= CAN_ERROR_HAL_FLAG; // Define appropriate flags
//
//        if (error_code & HAL_CAN_ERROR_BOF) {
//            // Bus Off error occurred on this instance
//            // instance->error_flags |= CAN_ERROR_BUS_OFF_FLAG;
//            // The main application should check this flag in CAN_Service or elsewhere
//            // and decide on recovery action (e.g., re-init after delay).
//            // Avoid complex recovery directly in ISR context.
//        }
//        // Add handling for other errors as needed (Warning, Passive, ACK, Stuff, Form, CRC etc.)
//
//        // Optional: Clear specific error flags if required by HAL/hardware
//        // Check HAL documentation for necessary clearing actions for specific errors.
//        // e.g., __HAL_FDCAN_CLEAR_FLAG(instance->hcan, FDCAN_FLAG_BUS_OFF); (Check exact flag names)
//    }
//    // Else: Error callback received for an unknown/uninitialized CAN handle
//}

// Add implementations for other HAL callbacks if needed (e.g., Tx Complete)
// Remember to use find_instance() inside them to route to the correct instance state.
