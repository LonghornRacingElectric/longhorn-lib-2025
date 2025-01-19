//
// Created by Alex Huang on 1/19/25.
//

#include "imu.h"
#include <stdint.h>
#include <stdbool.h>

static uint8_t data[6];
static SPI_HandleTypeDef *hspi;

/*private functions =======================================================*/
#define IMU_TIMEOUT 100
static void imu_writeregister1 (uint8_t addr, uint8_t value) {
    HAL_GPIO_WritePin(SPI_CS_IMU_GPIO_Port, SPI_CS_IMU_Pin, GPIO_PIN_RESET); //activate pin
    data[0] = addr; // address
    data[1] = value; // data
    HAL_StatusTypeDef status = HAL_SPI_Transmit(hspi, data, 2, IMU_TIMEOUT); //check status
    if(status != HAL_OK)
        Error_Handler();
    HAL_GPIO_WritePin(SPI_CS_IMU_GPIO_Port, SPI_CS_IMU_Pin, GPIO_PIN_SET); //deactivate pin
}

static void imu_readregister(uint8_t size, uint8_t addr) {
    HAL_GPIO_WritePin(SPI_CS_IMU_GPIO_Port, SPI_CS_IMU_Pin, GPIO_PIN_RESET); //active pin
    data[0] = addr | 0x80; //send read flag
    HAL_SPI_Transmit(hspi, data, 1, IMU_TIMEOUT ); //send data
    HAL_StatusTypeDef status = HAL_SPI_Receive(hspi, data, size, IMU_TIMEOUT); //check status
    if(status != HAL_OK)
        Error_Handler();
    HAL_GPIO_WritePin(SPI_CS_IMU_GPIO_Port, SPI_CS_IMU_Pin, GPIO_PIN_SET); //deactivate pin
}

static void imu_readregister1(uint8_t addr) { //one byte read
    data[0] = addr;
    imu_readregister(1, addr);
}

/*public functions =======================================================*/
//ACCELEROMETER 208Hz high performance, +/-16g
#define CTRL1_XL_REG 0x10
#define CTRL1_XL_VAL 0b01010100 // 208Hz, ±4g
//GYRO 208Hz high performance, 4000dps
#define CTRL2_G_REG 0x11
#define CTRL2_G_VAL 0b01010001 // 208Hz, 4000dps



void imu_init(SPI_HandleTypeDef *hspi_ptr) {
    hspi = hspi_ptr;
    imu_writeregister1(CTRL1_XL_REG, CTRL1_XL_VAL);
    imu_writeregister1(CTRL2_G_REG, CTRL2_G_VAL);
    imu_writeregister1(CTRL1_XL_REG, CTRL1_XL_VAL);
    imu_writeregister1(CTRL2_G_REG, CTRL2_G_VAL);
    imu_writeregister1(CTRL1_XL_REG, CTRL1_XL_VAL);
    imu_writeregister1(CTRL2_G_REG, CTRL2_G_VAL);
}

#define STATUS_REG 0x1e // correct address later
bool imu_isAccelReady() {
    imu_readregister1(STATUS_REG);
    bool ready = (data[0] & 0x01); // 0000 0001
    return ready;
}

bool imu_isGyroReady() {
    imu_readregister1(STATUS_REG);
    bool ready = (data[0] & 0x02); // 0000 0010
    return ready;
}

#define OUTX_H_A 0x29
#define ACCEL_LSB 0.00478728f
void imu_getAccel(xyz* vec) {
    imu_readregister(6, OUTX_H_A); //reads 6 bytes starts at OUTX_H_A

    int16_t accelX = data[0] | (data[1] << 8); //combines first and second byte, shifting over to get x bc higher value stored second
    vec->x = accelX * ACCEL_LSB;
    int16_t accelY = data[2] | (data[3] << 8);
    vec->y = accelY * ACCEL_LSB;
    int16_t accelZ = data[4] | (data[5] << 8);
    vec->z = accelZ * ACCEL_LSB;
}

#define OUTX_H_G 0x23
#define GYRO_LSB 0.0048869219f // rad/s
void imu_getGyro(xyz* vec) {
    imu_readregister(6, OUTX_H_G);
    int16_t buff_gyroX = data[0] + (data[1] << 8);
    vec->x = buff_gyroX * GYRO_LSB;
    int16_t buff_gyroY = data[2] + (data[3] << 8);
    vec->y = buff_gyroY * GYRO_LSB;
    int16_t buff_gyroZ = data[4] + (data[5] << 8);
    vec->z = buff_gyroZ * GYRO_LSB;
}

