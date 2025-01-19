//
// Created by Alex Huang on 1/19/25.
//

#ifndef LONGHORN_LIBRARY_2025_IMU_H
#define LONGHORN_LIBRARY_2025_IMU_H
//#include "main.h"
#ifndef XYZ_STRUCT
#define XYZ_STRUCT
typedef struct xyz {
    float x;
    float y;
    float z;
} xyz;
#endif

void imu_init(SPI_HandleTypeDef *hspi); //initialize imu

void imu_calibrate(); //calibrate imu

bool imu_isAccelReady();
bool imu_isGyroReady();

void imu_getAccel(xyz *accel); //adjusted data (not raw)

void imu_getGyro(xyz *gyro);

void imu_periodic();


#endif //LONGHORN_LIBRARY_2025_IMU_H

