//
// Created by Dhairya Gupta on 4/5/25.
//

#ifndef VCU_FIRMWARE_2025_NIGHT_CAN_IDS_H
#define VCU_FIRMWARE_2025_NIGHT_CAN_IDS_H

#include <stdint.h>
#include <float.h>


/*
 * Defines the IDs that boards will use for sending and receiving data over CAN.
 * This removes the necessity for us to define a function for everything, but also enables
 * us to be able to manage data easily if any CAN packet/type ever changes!! 🥰
 *
 * This also means that if something changes on one board, it changes on all of them.
 * Think of TypeScript types but with CAN packets and indexes and C code. Wahoo 🤪
 */


/** APPS PACKET **/
#define APPS1_VOLTAGE_IDX 0
#define APPS2_VOLTAGE_IDX 2
#define APPS1_TRAVEL_IDX 4
#define APPS2_TRAVEL_IDX 6

#define APPS_VOLTAGE_TYPE float
#define APPS_TRAVEL_TYPE float
#define APPS_PACKET_ID 0xD0
#define APPS_PACKET_FREQ 333 // this is in Hz

/** APPS FAULT PACKET **/
#define ACCEL_PEDAL_TRAVEL_IDX 0
#define APPS_FAULT_VECTOR_IDX 2

#define ACCEL_PEDAL_TRAVEL_TYPE float
#define APPS_FAULT_VECTOR_TYPE uint8_t  // see how to decode on the Data Flow sheet.


#endif //VCU_FIRMWARE_2025_NIGHT_CAN_IDS_H
