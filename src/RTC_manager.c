#include "RTC_manager.h"
#include "esp_log.h"
#include <string.h>
#include <sys/time.h>

static uint8_t bcd2dec(uint8_t val) { return ((val >> 4) * 10) + (val & 0x0f); }
static uint8_t dec2bcd(uint8_t val) { return ((val / 10) << 4) + (val % 10); }

void sync_logic(i2c_master_dev_handle_t rtc_handle) {
    uint8_t reg = 0x00;
    uint8_t d[7];
    // NEW DRIVER CALL: No I2C_PORT used here
    if (i2c_master_transmit_receive(rtc_handle, &reg, 1, d, 7, -1) == ESP_OK) {
        struct tm tm = {
            .tm_sec = bcd2dec(d[0]),
            .tm_min = bcd2dec(d[1]),
            .tm_hour = bcd2dec(d[2] & 0x3F),
            .tm_mday = bcd2dec(d[4]),
            .tm_mon = bcd2dec(d[5] & 0x7F) - 1,
            .tm_year = bcd2dec(d[6]) + 100,
            .tm_isdst = -1
        };
        time_t t = mktime(&tm);
        struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        ESP_LOGI("RTC", "System Clock synced to RTC");
    }
}

struct tm get_and_return_time(char *buffer, size_t max_len) {
    time_t now;
    struct tm timeinfo;
    
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // Still fills the buffer if one is provided
    if (buffer != NULL && max_len > 0) {
        strftime(buffer, max_len, "%c", &timeinfo);
    }
    
    return timeinfo; // Returns the full time structure
}