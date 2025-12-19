#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/i2c_master.h"

/* Pins */
#define SDA_PIN         8
#define SCL_PIN         9

/* Addresses */
#define BMX160_ADDR     0x69
#define SSD1306_ADDR    0x3C
#define RTC_ADDR        0x68

typedef struct {
    float x;
    float y;
    float z;
} sensor_xyz_t;

/* Externs for shared data used by UI and Main */
extern sensor_xyz_t g_accel;
extern sensor_xyz_t g_gyro;
extern SemaphoreHandle_t g_data_mutex;
extern SemaphoreHandle_t g_gyro_mutex;
extern volatile bool g_ui_started;
extern uint32_t g_data_take_fail;
extern uint32_t g_gyro_take_fail;
extern uint32_t g_data_take_success;
extern uint32_t g_gyro_take_success;
#endif