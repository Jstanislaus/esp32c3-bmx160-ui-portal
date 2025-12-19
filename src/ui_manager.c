#include "ui_manager.h"
#include "globals.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "encoder_manager.h"
#include "driver/i2c_master.h" // Essential for the handle
#include "RTC_manager.h"
#include "ssd1306.h"
static const char *TAG = "UI_MANAGER";

/* * NOTE: Since your ssd1306 library likely uses the old driver, 
 * we must ensure that functions like ssd1306_display_text 
 * are only used if you have updated the library. 
 * If the library is still old, these calls might still crash.
 * For now, we update the logic to use the handle.
 */

void display_splash_screen(i2c_master_dev_handle_t dev_handle) {
    // If your library requires the SSD1306_t struct, you'll need to 
    // wrap the handle inside it or use direct new-driver commands.
    ESP_LOGI(TAG, "Displaying Splash Screen...");
    // Direct command to show activity (All pixels ON)
    uint8_t cmd = 0xA5; 
    i2c_master_transmit(dev_handle, &cmd, 1, -1);
    vTaskDelay(pdMS_TO_TICKS(1000));
}

void ui_task(void *pvParameters) {
    // 1. Recover the handle passed from app_main
    i2c_master_dev_handle_t oled_handle = (i2c_master_dev_handle_t)pvParameters;
    
    // 2. Initialize the library structure manually
    // This bypasses legacy driver installation code inside library init functions
    SSD1306_t dev; 
    memset(&dev, 0, sizeof(SSD1306_t));
    
    dev._i2c_dev_handle = oled_handle; // Use the handle created in app_main
    dev._address = CONFIG_SSD1306_ADDR;
    dev._width = 128;
    dev._height = 64;
    dev._pages = 8;
    dev._flip = false;

    // 3. Manual Display Power-On via New Driver
    // We send raw initialization commands to wake up the screen without touching the old driver
    uint8_t init_cmds[] = {
        OLED_CONTROL_BYTE_CMD_STREAM,
        OLED_CMD_DISPLAY_OFF,        
        OLED_CMD_SET_CHARGE_PUMP, 0x14,
        OLED_CMD_DISPLAY_ON          
    };
    i2c_master_transmit(oled_handle, init_cmds, sizeof(init_cmds), -1);

    ESP_LOGI(TAG, "UI Task Started with Handle: %p", oled_handle);
    g_ui_started = true;

    sensor_xyz_t local_accel = {0};
    sensor_xyz_t local_gyro = {0}; 
    char buf[32];
    
    while (1) {
        // Fetch current screen state from the encoder
        ui_screen_t state = encoder_get_screen_state();

        // Sync Sensor Data via Mutexes
        if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            local_accel = g_accel;
            xSemaphoreGive(g_data_mutex);
        }
        if (xSemaphoreTake(g_gyro_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            local_gyro = g_gyro;
            xSemaphoreGive(g_gyro_mutex);
        }

        // Retrieve current time from the internal clock
        char time_str[64];
        struct tm now = get_and_return_time(time_str, sizeof(time_str));

        // 4. Drawing Logic
        // Clear screen before redrawing
        ssd1306_clear_screen(&dev, false);

        switch (state) {
            case UI_STATE_ACCEL:
                snprintf(buf, sizeof(buf), "X%.3f", local_accel.x);
                ssd1306_display_text(&dev, 0, "ACCEL", 5, false);
                ssd1306_display_text_x3(&dev, 2, buf, strlen(buf), false);
                break;

            case UI_STATE_GYRO:
                snprintf(buf, sizeof(buf), "X%.3f", local_gyro.x);
                ssd1306_display_text(&dev, 0, "GYRO", 4, false);
                ssd1306_display_text_x3(&dev, 2, buf, strlen(buf), false);
                break;

            case UI_TIME: // Ensure this matches your enum in encoder_manager.h
                snprintf(buf, sizeof(buf), "%02d:%02d:%02d", now.tm_hour, now.tm_min, now.tm_sec);
                ssd1306_display_text(&dev, 0, "REAL TIME", 9, false);
                ssd1306_display_text_x3(&dev, 2, buf, strlen(buf), false);
                break;
                
            default:
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}