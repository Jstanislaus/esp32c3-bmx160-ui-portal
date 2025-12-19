#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "globals.h"
#include "RTC_manager.h"
#include "encoder_manager.h"
#include "ui_manager.h"
#include "driver/gpio.h"
#include "ssd1306.h"
// Extern variable definitions
uint32_t g_data_take_fail = 0;
uint32_t g_gyro_take_fail = 0;
uint32_t g_data_take_success = 0;
uint32_t g_gyro_take_success = 0;

static const char *TAG = "APP_MAIN";

/* Global Sensor Storage */
sensor_xyz_t g_accel = {0};
sensor_xyz_t g_gyro = {0};
SemaphoreHandle_t g_data_mutex = NULL;
SemaphoreHandle_t g_gyro_mutex = NULL;
volatile bool g_ui_started = false;

/* --- I2C Helper for New Driver --- */
static esp_err_t bmx_read_regs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t len) {
    return i2c_master_transmit_receive(dev, &reg, 1, data, len, -1);
}

static esp_err_t bmx_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev, buf, sizeof(buf), -1);
}

/* --- BMX160 Sensor Initialization --- */
bool bmx160_init_new(i2c_master_dev_handle_t dev) {
    uint8_t id = 0;
    if (bmx_read_regs(dev, 0x00, &id, 1) != ESP_OK || id != 0xD8) {
        ESP_LOGE(TAG, "BMX160 not found! (ID: 0x%02X)", id);
        return false;
    }
    bmx_write_reg(dev, 0x7E, 0xB6); // Soft Reset
    vTaskDelay(pdMS_TO_TICKS(100));
    bmx_write_reg(dev, 0x7E, 0x11); // Accel Normal Mode
    bmx_write_reg(dev, 0x7E, 0x15); // Gyro Normal Mode
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "BMX160 Initialized (Accel + Gyro)");
    return true;
}

/* --- Task: Read Sensor Data --- */
void bmx_read_task(void *arg) {
    i2c_master_dev_handle_t bmx_dev = (i2c_master_dev_handle_t)arg;
    uint8_t buf[12]; // 6 bytes Gyro, 6 bytes Accel
    
    for (;;) {
        // Read 12 bytes starting from 0x0C (Gyro LSB)
        if (bmx_read_regs(bmx_dev, 0x0C, buf, 12) == ESP_OK) {
            
            // 1. Process Gyro (0x0C - 0x11)
            if (xSemaphoreTake(g_gyro_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                g_gyro.x = (int16_t)((buf[1] << 8) | buf[0]) / 16.4f;
                g_gyro.y = (int16_t)((buf[3] << 8) | buf[2]) / 16.4f;
                g_gyro.z = (int16_t)((buf[5] << 8) | buf[4]) / 16.4f;
                g_gyro_take_success++;
                xSemaphoreGive(g_gyro_mutex);
            } else { g_gyro_take_fail++; }

            // 2. Process Accel (0x12 - 0x17)
            if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                g_accel.x = (int16_t)((buf[7] << 8) | buf[6]) / 16384.0f;
                g_accel.y = (int16_t)((buf[9] << 8) | buf[8]) / 16384.0f;
                g_accel.z = (int16_t)((buf[11] << 10) | buf[10]) / 16384.0f;
                g_data_take_success++;
                xSemaphoreGive(g_data_mutex);
            } else { g_data_take_fail++; }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Initializing System...");

    // 1. Initialize Bus
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = SDA_PIN,
        .scl_io_num = SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));

    // 2. Add RTC (0x68)
    i2c_device_config_t rtc_cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = RTC_ADDR, .scl_speed_hz = 100000 };
    i2c_master_dev_handle_t rtc_handle;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &rtc_cfg, &rtc_handle));

    // 3. Add BMX160 (0x68)
    i2c_device_config_t bmx_cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = BMX160_ADDR, .scl_speed_hz = 100000 };
    i2c_master_dev_handle_t bmx_handle;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &bmx_cfg, &bmx_handle));

    // 4. Add SSD1306 (0x3C)
    i2c_device_config_t oled_cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = SSD1306_ADDR, .scl_speed_hz = 400000 };
    i2c_master_dev_handle_t oled_handle;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &oled_cfg, &oled_handle));

    // 5. System Infrastructure
    gpio_install_isr_service(0);
    encoder_init();
    
    g_data_mutex = xSemaphoreCreateMutex();
    g_gyro_mutex = xSemaphoreCreateMutex();

    // 6. Init Hardware Logic
    sync_logic(rtc_handle);
    bmx160_init_new(bmx_handle);

    // 7. Start Tasks (Passing handles as arguments)
    xTaskCreate(bmx_read_task, "bmx_read", 3072, (void*)bmx_handle, 5, NULL);
    xTaskCreate(ui_task, "ui", 4096, (void*)oled_handle, 4, NULL);

    
    while(1) {
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}