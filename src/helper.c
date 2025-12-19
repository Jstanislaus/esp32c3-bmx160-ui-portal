#include "helper.h"
#include "driver/i2c_master.h" // FIX: Changed from i2c.h to i2c_master.h
#include "esp_log.h"
#include "globals.h"

i2c_master_bus_handle_t app_i2c_init(void) 
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = SDA_PIN,
        .scl_io_num = SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t handle;
    
    // This is the new-style initialization
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &handle));
    
    ESP_LOGI("I2C_HELPER", "New I2C Driver initialized (SDA=%d SCL=%d)", SDA_PIN, SCL_PIN);
    
    return handle; 
}