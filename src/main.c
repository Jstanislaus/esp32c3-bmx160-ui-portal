/*
 * src/main.c
 * BMX160 I2C reader (ESP-IDF)
 *
 * Tidied for readability:
 *  - grouped includes & constants
 *  - consolidated I2C/BMX helpers
 *  - extracted parse helpers for accel/gyro
 *  - reduced log spam and removed unused variables
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "helper.h"
#include "globals.h"
#include "ssd1306.h"
#include "font8x8_basic.h"
#include "ui_manager.h"
#include "encoder_manager.h"
#define PACK8 __attribute__((aligned( __alignof__( uint8_t ) ), packed ))
static const char *TAG = "BMX160";



/* SSD1306 Command */


static volatile uint8_t bmx_detected_addr = 0; /* 0=undetected; set to 0x68/0x69 (BMX160 only) */

// Forward declarations for tasks
void bmx_read_task(void *arg);
void ui_task(void *arg);
void stats_task(void *arg);
void mutex_hog_task(void *arg);




/* ---------------- I2C helpers ------------------------------------------- */
/* Basic address-aware read/write wrappers that honour a common timeout. */
static inline esp_err_t i2c_read_addr(uint8_t addr, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(
        I2C_PORT, addr, &reg, 1, data, len, pdMS_TO_TICKS(I2C_TIMEOUT_MS)
    );
}

static inline esp_err_t i2c_write_addr(uint8_t addr, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_write_to_device(
        I2C_PORT, addr, buf, sizeof(buf), pdMS_TO_TICKS(I2C_TIMEOUT_MS)
    );
}

/* Convenience helpers using the detected address (or default) */
static inline uint8_t bmx160_addr(void)
{
    return bmx_detected_addr ? bmx_detected_addr : BMX160_ADDR;
}

static inline uint8_t SSD1306_addr(void)
{
    /* SSD1306 uses fixed I2C address; do not reuse BMX160 detected address */
    return SSD1306_ADDR;
}

static inline esp_err_t bmx160_read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_read_addr(bmx160_addr(), reg, data, len);
}

static inline esp_err_t bmx160_write_reg(uint8_t reg, uint8_t val)
{
    return i2c_write_addr(bmx160_addr(), reg, val);
} 

static inline esp_err_t SSD1306_write_reg(uint8_t reg, uint8_t val)
{
    return i2c_write_addr(SSD1306_addr(), reg, val);
} 

/* Try to find BMX160 at 0x68 or 0x69 by reading CHIP_ID */
static bool bmx160_detect_address(void)
{
    uint8_t id = 0;
    const uint8_t candidates[] = {BMX160_ADDR, 0x69};
    for (size_t i = 0; i < sizeof(candidates); ++i) {
        uint8_t addr = candidates[i];
        if (i2c_read_addr(addr, 0x00, &id, 1) == ESP_OK && id == 0xD8) {
            bmx_detected_addr = addr;
            ESP_LOGI(TAG, "BMX160 detected at 0x%02X (CHIP_ID=0x%02X)", addr, id);
            return true;
        }
    }
    ESP_LOGE(TAG, "BMX160 not found on I2C (tried 0x%02X and 0x69)", BMX160_ADDR);
    return false;
}

static bool ssd1306_detect(void)
{
    uint8_t dummy = 0x00; // control byte
    if (i2c_master_write_to_device(
            I2C_PORT,
            SSD1306_ADDR,
            &dummy,
            1,
            pdMS_TO_TICKS(100)) == ESP_OK)
    {
        ESP_LOGI(TAG, "SSD1306 ACK at 0x%02X", SSD1306_ADDR);
        return true;
    }

    ESP_LOGE(TAG, "SSD1306 not found at 0x%02X", SSD1306_ADDR);
    return false;
}


/* Initialize sensor into normal mode with conservative settings */
static bool bmx160_init(void)
{
    if (!bmx160_detect_address()) {
        return false;
    }

    /* soft reset */
    if (bmx160_write_reg(0x7E, 0xB6) != ESP_OK) {
        ESP_LOGW(TAG, "soft reset write failed");
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    /* conservative config: ACC 100Hz ±2g, GYR 100Hz ±2000dps */
    bmx160_write_reg(0x40, 0x28);
    bmx160_write_reg(0x41, 0x03);
    bmx160_write_reg(0x42, 0x28);
    bmx160_write_reg(0x43, 0x00);

    vTaskDelay(pdMS_TO_TICKS(80));

    /* Issue CMDs required by BMX160 to exit suspend: ACC -> NORMAL, GYR -> NORMAL */
    if (bmx160_write_reg(BMX160_CMD_REG, BMX160_CMD_ACC_NORMAL) == ESP_OK) {
        ESP_LOGI(TAG, "CMD: ACC -> NORMAL");
        vTaskDelay(pdMS_TO_TICKS(20));
    } else {
        ESP_LOGW(TAG, "Failed to write ACC NORMAL CMD");
    }

    if (bmx160_write_reg(BMX160_CMD_REG, BMX160_CMD_GYR_NORMAL) == ESP_OK) {
        ESP_LOGI(TAG, "CMD: GYR -> NORMAL");
        /* give gyro more time and verify outputs are non-zero; retry if necessary */
        vTaskDelay(pdMS_TO_TICKS(100));
        uint8_t gbuf[6];
        bool gyr_ok = false;
        for (int attempt = 0; attempt < 4; ++attempt) {
            if (bmx160_read_regs(0x0C, gbuf, sizeof(gbuf)) == ESP_OK) {
                bool any_nonzero = false;
                for (size_t i = 0; i < sizeof(gbuf); ++i) {
                    if (gbuf[i] != 0) { any_nonzero = true; break; }
                }
                if (any_nonzero) { gyr_ok = true; break; }
            }
            ESP_LOGW(TAG, "gyro read all zero, re-issuing GYR NORMAL (attempt %d)", attempt + 1);
            /* re-issue command and wait longer */
            (void)bmx160_write_reg(BMX160_CMD_REG, BMX160_CMD_GYR_NORMAL);
            vTaskDelay(pdMS_TO_TICKS(150));
        }
        if (!gyr_ok) {
            ESP_LOGW(TAG, "Gyro still reporting zeros after retries; continuing but gyro output will be zero until fixed");
        } else {
            ESP_LOGI(TAG, "Gyro appears to be active (non-zero samples observed)");
        }
    } else {
        ESP_LOGW(TAG, "Failed to write GYR NORMAL CMD");
    }

    /* Poll PMU_STATUS for normal flags (give the chip some time) */
    uint8_t pmu = 0;
    bool pmu_ok = false;
    for (int i = 0; i < 20; ++i) {
        if (bmx160_read_regs(0x03, &pmu, 1) == ESP_OK) {
            if ((pmu & 0x3F) == BMX160_PMU_NORMAL) {
                pmu_ok = true;
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI(TAG, "PMU_STATUS=0x%02X (%s)", pmu, pmu_ok ? "normal" : "not normal");

    /* Read DATA block and log zero/non-zero detection */
    uint8_t raw[0x14];
    if (bmx160_read_regs(0x04, raw, sizeof(raw)) == ESP_OK) {
        bool any_nonzero = false;
        for (size_t i = 0; i < sizeof(raw); ++i) if (raw[i] != 0) { any_nonzero = true; break; }
        if (any_nonzero) {
            ESP_LOGI(TAG, "BMX160 DATA looks live (non-zero bytes present)");
        } else {
            ESP_LOGW(TAG, "BMX160 DATA all zeros after CMDs; sensor may still be suspended or misconfigured");
        }
    } else {
        ESP_LOGW(TAG, "Could not read DATA block after CMDs");
    }

    if (!pmu_ok) {
        ESP_LOGW(TAG, "BMX160 did not enter normal PMU mode");
    }

    ESP_LOGI(TAG, "BMX160 init complete (addr=0x%02X)" , bmx_detected_addr);
    return pmu_ok;
} 

/* Conversion constants (match sensor config) */
static const float ACC_LSB_PER_G = 16384.0f;  /* ±2g */
static const float ACC_TO_G = (1.0f / ACC_LSB_PER_G);

static const float GYR_LSB_PER_DPS = 16.4f;   /* ±2000dps */
static const float GYR_TO_DPS = (1.0f / GYR_LSB_PER_DPS);


/* parse helpers: sensor data is little-endian LSB first pairs (X_LSB,X_MSB...) */
static inline sensor_xyz_t parse_gyro(const uint8_t *buf)
{
    int16_t gx = (int16_t)((buf[1] << 8) | buf[0]);
    int16_t gy = (int16_t)((buf[3] << 8) | buf[2]);
    int16_t gz = (int16_t)((buf[5] << 8) | buf[4]);
    sensor_xyz_t out = {
        .x = gx * GYR_TO_DPS,
        .y = gy * GYR_TO_DPS,
        .z = gz * GYR_TO_DPS,
    };
    return out;
}

static inline sensor_xyz_t parse_accel(const uint8_t *buf)
{
    int16_t ax = (int16_t)((buf[1] << 8) | buf[0]);
    int16_t ay = (int16_t)((buf[3] << 8) | buf[2]);
    int16_t az = (int16_t)((buf[5] << 8) | buf[4]);
    sensor_xyz_t out = {
        .x = ax * ACC_TO_G,
        .y = ay * ACC_TO_G,
        .z = az * ACC_TO_G,
    };
    return out;
}

/* Global sensor storage and synchronization - REMOVED 'static' */
sensor_xyz_t g_accel = {0};
sensor_xyz_t g_gyro = {0};
SemaphoreHandle_t g_data_mutex = NULL;
SemaphoreHandle_t g_gyro_mutex = NULL;

/* We can keep this static because only main.c needs to see the handles */
static TaskHandle_t g_ui_handle = NULL;
static TaskHandle_t g_reader_handle = NULL;

/* Share these for the UI to display status */
uint32_t g_data_take_fail = 0;
uint32_t g_gyro_take_fail = 0;
uint32_t g_data_take_success = 0;
uint32_t g_gyro_take_success = 0;
volatile bool g_ui_started = false;
static const TickType_t mutex_timeout_ticks = pdMS_TO_TICKS(5);
void bmx_read_task(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    uint8_t buf[12];
    uint32_t loop = 0;

    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(50));

        if (bmx160_read_regs(0x0C, buf, sizeof(buf)) != ESP_OK) {
            ESP_LOGW(TAG, "bmx_read_task: I2C read failed");
            continue;
        }

        /* Debug: occasionally dump the raw 12 bytes starting at 0x0C so we can see where gyro/accel live */
        static uint32_t _dbg_dump = 0;
        if ((++_dbg_dump % 20) == 0) {
            ESP_LOGI(TAG, "RAW[0x0C..] %02X %02X %02X %02X %02X %02X | %02X %02X %02X %02X %02X %02X",
                buf[0], buf[1], buf[2], buf[3], buf[4], buf[5],
                buf[6], buf[7], buf[8], buf[9], buf[10], buf[11]);
        }

        sensor_xyz_t gyro_sample = parse_gyro(buf);           /* buf[0..5] GYRO */
        sensor_xyz_t accel_sample = parse_accel(buf + 6);    /* buf[6..11] ACC */

        if (xSemaphoreTake(g_gyro_mutex, mutex_timeout_ticks) == pdTRUE) {
            g_gyro = gyro_sample;
            g_gyro_take_success++;
            xSemaphoreGive(g_gyro_mutex);
        } else {
            g_gyro_take_fail++;
        }

        if (xSemaphoreTake(g_data_mutex, mutex_timeout_ticks) == pdTRUE) {
            g_accel = accel_sample;
            g_data_take_success++;
            xSemaphoreGive(g_data_mutex);
        } else {
            g_data_take_fail++;
        }

        if ((++loop % 100) == 0) { /* every 100*50ms = 5s */
            uint8_t raw[0x14]; /* read 0x04..0x17 = 20 bytes */
            if (bmx160_read_regs(0x04, raw, sizeof(raw)) == ESP_OK) {
                char buf[128];
                int p = snprintf(buf, sizeof(buf), "DATA:");
                for (size_t i = 0; i < sizeof(raw) && p < (int)sizeof(buf) - 4; ++i) {
                    p += snprintf(buf + p, sizeof(buf) - p, " %02X", raw[i]);
                }
                ESP_LOGI(TAG, "%s", buf);

                bool any_nonzero = false;
                for (size_t i = 0; i < sizeof(raw); ++i) if (raw[i] != 0) { any_nonzero = true; break; }
                if (any_nonzero) {
                    ESP_LOGI(TAG, "reader: observed non-zero DATA bytes (sensor producing data)");
                } else {
                    ESP_LOGW(TAG, "reader: DATA all zeros — sensor may be suspended or mis-configured");
                }
            } else {
                ESP_LOGW(TAG, "bmx_read_task: full DATA read failed");
            }

            uint8_t pmu = 0, status = 0;
            if (bmx160_read_regs(0x03, &pmu, 1) == ESP_OK) {}
            if (bmx160_read_regs(0x1B, &status, 1) == ESP_OK) {}
            ESP_LOGI(TAG, "PMU_STATUS=0x%02X STATUS=0x%02X | acc_ok=%u acc_fail=%u gyr_ok=%u gyr_fail=%u",
                pmu, status, g_data_take_success, g_data_take_fail, g_gyro_take_success, g_gyro_take_fail);
        }
    }
} 


// void blink_oled_test() {
//     SSD1306_write_reg(0x00, 0xAE); // 1. Display OFF
//     SSD1306_write_reg(0x00, 0x8D); // 2. Charge Pump Setting
//     SSD1306_write_reg(0x00, 0x14); // 3. Enable Charge Pump
//     SSD1306_write_reg(0x00, 0xA5); // 4. Entire Display ON (Force all pixels)
//     SSD1306_write_reg(0x00, 0xAF); // 5. Display ON
// }

void blink_oled_test()
{
    // Display OFF
    SSD1306_write_reg(0x00, 0xAE);

    // Set display clock divide
    SSD1306_write_reg(0x00, 0xD5);
    SSD1306_write_reg(0x00, 0x80);

    // Set multiplex ratio (CRITICAL for 128x32)
    SSD1306_write_reg(0x00, 0xA8);
    SSD1306_write_reg(0x00, 0x1F);

    // Display offset
    SSD1306_write_reg(0x00, 0xD3);
    SSD1306_write_reg(0x00, 0x00);

    // Set start line
    SSD1306_write_reg(0x00, 0x40);

    // Enable charge pump
    SSD1306_write_reg(0x00, 0x8D);
    SSD1306_write_reg(0x00, 0x14);

    // Segment remap
    SSD1306_write_reg(0x00, 0xA1);

    // COM scan direction
    SSD1306_write_reg(0x00, 0xC8);

    // COM pins configuration (128x32 specific!)
    SSD1306_write_reg(0x00, 0xDA);
    SSD1306_write_reg(0x00, 0x02);

    // Set contrast
    SSD1306_write_reg(0x00, 0x81);
    SSD1306_write_reg(0x00, 0x8F);

    // Pre-charge period
    SSD1306_write_reg(0x00, 0xD9);
    SSD1306_write_reg(0x00, 0xF1);

    // VCOM detect
    SSD1306_write_reg(0x00, 0xDB);
    SSD1306_write_reg(0x00, 0x40);

    // Resume RAM display
    SSD1306_write_reg(0x00, 0xA4);

    // Force all pixels ON (VISUAL TEST)
    SSD1306_write_reg(0x00, 0xA5);

    // Display ON
    SSD1306_write_reg(0x00, 0xAF);
}


static void i2c_scan(void)
{
    ESP_LOGI("I2C", "Scanning I2C bus...");
    for (uint8_t addr = 1; addr < 127; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);

        esp_err_t err = i2c_master_cmd_begin(
            I2C_NUM_0, cmd, pdMS_TO_TICKS(50)
        );
        i2c_cmd_link_delete(cmd);

        if (err == ESP_OK) {
            ESP_LOGI("I2C", "Found device at 0x%02X", addr);
        }
    }
}


/* ---------------- Main ---------------- */
void app_main(void)
{
    app_i2c_init();
    gpio_install_isr_service(0);
    encoder_init();
    vTaskDelay(pdMS_TO_TICKS(500));
    if (!bmx160_init()) {
        ESP_LOGW(TAG, "BMX160 init incomplete; continuing anyway");
    }

    g_data_mutex = xSemaphoreCreateMutex();
    g_gyro_mutex = xSemaphoreCreateMutex();

    if (g_data_mutex == NULL || g_gyro_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex(es): data=%p gyro=%p", g_data_mutex, g_gyro_mutex);
        /* proceed but tasks may fail when attempting to take null mutexes */
    }
    BaseType_t rc;


    rc = xTaskCreate(bmx_read_task, "bmx_read", 3*1024, NULL, tskIDLE_PRIORITY + 2, &g_reader_handle);
    ESP_LOGI(TAG, "bmx_read task create -> %s handle=%p free_heap=%u", rc == pdPASS ? "ok" : "FAILED", g_reader_handle, esp_get_free_heap_size());

    /* Give UI higher priority than stats so UI is responsive */
    rc = xTaskCreate(ui_task, "ui", 4*1024, NULL, tskIDLE_PRIORITY + 1, &g_ui_handle);
    ESP_LOGI(TAG, "ui task create -> %s handle=%p free_heap=%u", rc == pdPASS ? "ok" : "FAILED", g_ui_handle, esp_get_free_heap_size());

    rc = xTaskCreate(stats_task, "stats", 2*1024, NULL, tskIDLE_PRIORITY, NULL);
    ESP_LOGI(TAG, "stats task create -> %s free_heap=%u", rc == pdPASS ? "ok" : "FAILED", esp_get_free_heap_size());


    ESP_LOGI(TAG, "post-create heap free=%u", esp_get_free_heap_size());

    /* Wait up to 2s for ui task to report startup (allows for scheduling latencies) */
    for (int i = 0; i < 200; ++i) {
        if (g_ui_started) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (g_ui_started) {
        ESP_LOGI(TAG, "ui reported started = true (handle=%p)", g_ui_handle);
    } else {
        ESP_LOGE(TAG, "ui reported started = false; ui_handle=%p reader_handle=%p",
            g_ui_handle, g_reader_handle);
    }

    /* mutex_hog runs at idle priority (don't underflow tskIDLE_PRIORITY) */
    //rc = xTaskCreate(mutex_hog_task, "mutex_hog", 1024, NULL, tskIDLE_PRIORITY, NULL);
    //ESP_LOGI(TAG, "mutex_hog create -> %s", rc == pdPASS ? "ok" : "FAILED");

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
} 



// void ui_task(void *arg)
// {
//     sensor_xyz_t local_accel = {0};
//     sensor_xyz_t local_gyro = {0};
//     g_ui_started = true; /* mark visible to app_main */
//     TaskHandle_t h = xTaskGetCurrentTaskHandle();
//     ESP_LOGI(TAG, "ui_task: started (task=%p name=%s)", h, pcTaskGetName(h));

//     for (;;) {
//         vTaskDelay(pdMS_TO_TICKS(60));

//         if (g_data_mutex == NULL || g_gyro_mutex == NULL) {
//             ESP_LOGE(TAG, "ui_task: mutexes not initialized");
//             vTaskDelay(pdMS_TO_TICKS(1000));
//             continue;
//         }

//         if (xSemaphoreTake(g_data_mutex, mutex_timeout_ticks) == pdTRUE) {
//             local_accel = g_accel;
//             xSemaphoreGive(g_data_mutex);
//         } else {
//             g_data_take_fail++;
//             ESP_LOGW(TAG, "ui_task: accel data unavailable");
//         }

//         if (xSemaphoreTake(g_gyro_mutex, mutex_timeout_ticks) == pdTRUE) {
//             local_gyro = g_gyro;
//             xSemaphoreGive(g_gyro_mutex);
//         } else {
//             g_gyro_take_fail++;
//             ESP_LOGW(TAG, "ui_task: gyro data unavailable");
//         }

//         /* TODO: render local_accel / local_gyro to OLED */



//         if (local_accel.x != 0.0f || local_accel.y != 0.0f || local_accel.z != 0.0f ||
//             local_gyro.x != 0.0f || local_gyro.y != 0.0f || local_gyro.z != 0.0f) {
//             ESP_LOGI(TAG, "UI: accel=(%.3f,%.3f,%.3f) gyro=(%.2f,%.2f,%.2f)",
//                 local_accel.x, local_accel.y, local_accel.z,
//                 local_gyro.x, local_gyro.y, local_gyro.z);
//         } else {
//             ESP_LOGD(TAG, "UI: sample all zeros");
//         }
//     }
// } 

void stats_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "ACC: ok=%u fail=%u | GYR: ok=%u fail=%u",
            g_data_take_success, g_data_take_fail, g_gyro_take_success, g_gyro_take_fail);
    }
}
