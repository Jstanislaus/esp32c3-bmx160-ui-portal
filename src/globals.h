#ifndef GLOBALS_H
#define GLOBALS_H

/* Configuration ----------------------------------------------------------- */
#define I2C_PORT        I2C_NUM_0
#define SDA_PIN         8
#define SCL_PIN         9
#define I2C_FREQ_HZ     400000
#define BMX160_ADDR     0x68   /* default 7-bit address; can be 0x69 */
#define SSD1306_ADDR    0x3C

#define I2C_TIMEOUT_MS  100
#define MUTEX_TIMEOUT_MS 5     /* ms to wait for mutex in UI/reader */

/* BMX160 command and expected PMU values */
#define BMX160_CMD_REG        0x7E
#define BMX160_CMD_ACC_NORMAL 0x11
#define BMX160_CMD_GYR_NORMAL 0x15
#define BMX160_PMU_NORMAL     0x14

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Define the struct once here so everyone knows what it is
typedef struct {
    float x;
    float y;
    float z;
} sensor_xyz_t;

// Use 'extern' to tell ui_manager.c that these exist in main.c
extern sensor_xyz_t g_accel;
extern SemaphoreHandle_t g_data_mutex;
// Use 'extern' to tell other files these exist in main.c
extern sensor_xyz_t g_accel;
extern sensor_xyz_t g_gyro;           // Added: UI task needs this
extern SemaphoreHandle_t g_data_mutex;
extern SemaphoreHandle_t g_gyro_mutex; // Added: UI task needs this
extern volatile bool g_ui_started;

// Add these if you are tracking errors/stats in your UI
extern uint32_t g_data_take_fail;
extern uint32_t g_gyro_take_fail;

// Add this so the UI task knows how long to wait
#define MUTEX_TIMEOUT_TICKS pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)

#endif /* GLOBALS_H */