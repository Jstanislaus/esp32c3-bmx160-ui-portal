#ifndef HELPER_H
#define HELPER_H

#include "driver/i2c_master.h"

/**
 * Initializes the I2C Master Bus using the New Generation (NG) driver.
 * @return handle to the initialized bus for adding devices (RTC, BMX, SSD1306).
 */
i2c_master_bus_handle_t app_i2c_init(void);

#endif