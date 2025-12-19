#include "helper.h"      // Fix 1: Added quotes and fixed include syntax
#include "driver/i2c.h"
#include "esp_log.h"     // Fix 2: Added missing header for ESP_LOGI
#include "globals.h"
// Fix 3: Removed 'static' so app_main can see this function
void app_i2c_init(void) 
{
    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SDA_PIN,
        .scl_io_num = SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &cfg));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, cfg.mode, 0, 0, 0));
    
    // Ensure TAG is defined (usually at the top of the file)
    ESP_LOGI("I2C_HELPER", "I2C initialized (SDA=%d SCL=%d)", SDA_PIN, SCL_PIN);
}