#include "ui_manager.h"
#include "ssd1306.h"
#include "globals.h"
#include "esp_log.h"
#include <string.h>
#include "encoder_manager.h"

static const char *TAG = "UI_MANAGER";
// Define the function here
void display_splash_screen(SSD1306_t *dev) {
    ssd1306_clear_screen(dev, false);
    ssd1306_display_text_x3(dev, 2, "PROJECT", 7, false);
    vTaskDelay(pdMS_TO_TICKS(2000));
}


void ui_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting UI Task...");
    
    // 1. Initialize the local display structure
    SSD1306_t dev;
    dev._address = 0x3C; // Replace with your I2C address
    dev._flip = false;
    ssd1306_init(&dev, 128, 64);

    sensor_xyz_t local_accel = {0};
    
    while (1) {
        uint8_t state = encoder_get_screen_state();
        // 2. Safely copy data from globals using the mutex
        if (g_data_mutex != NULL) {
            if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                local_accel = g_accel;
                xSemaphoreGive(g_data_mutex);
            }
        }

        // 3. Update Display
        ssd1306_clear_screen(&dev, false);
        ssd1306_contrast(&dev, 0xFF); // Max brightness
        switch (state) {
            case 0:

                char buf[16];
                // 1. Convert the float to a string
                snprintf(buf, sizeof(buf), "X%.3f", local_accel.x);
                // 2. Pass that string to the display
                ssd1306_display_text_x3(&dev, 1, buf, strlen(buf), false);
                break;
            case 1:

                snprintf(buf, sizeof(buf), "Y%.3f", local_accel.y);
                ssd1306_display_text_x3(&dev, 1, buf, strlen(buf), false);
                break;

            case 2:

                snprintf(buf, sizeof(buf), "Z%.3f", local_accel.z);
                ssd1306_display_text_x3(&dev, 1, buf, strlen(buf), false);
                break;
}

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}