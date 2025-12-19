#include "encoder_manager.h"
#include "driver/gpio.h"
#include "globals.h" // We'll move the counter here if needed
#define STEPS_PER_STATE 10  // Change this to adjust sensitivity
#define DT_GPIO 4
#define CLK_GPIO 5
#include "esp_log.h"

static volatile int encoder_counter = 0;
static int last_a = 0;
static volatile TickType_t last_isr_tick = 0; // in ticks (for debounce)
static volatile int prev_a = 0;
static volatile int prev_b = 0;

static void IRAM_ATTR encoder_isr_handler(void* arg) {
    /* Debounce: ignore changes within DEBOUNCE_MS of previous change */
    TickType_t now = xTaskGetTickCountFromISR();
    const TickType_t debounce_ticks = pdMS_TO_TICKS(20);
    if ((now - last_isr_tick) < debounce_ticks) {
        return; // ignore bouncing
    }
    last_isr_tick = now;

    int a = gpio_get_level(CLK_GPIO); 
    int b = gpio_get_level(DT_GPIO); 
    prev_a = a;
    prev_b = b;

    if (a != last_a) { 
        if (b != a) {
            encoder_counter++; 
        } else {
             encoder_counter--; 
         }
    }
    last_a = a;
}

void encoder_init(void) {
    // 1. Configure DT Pin
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << DT_GPIO),
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);

    // 2. Configure CLK Pin (Reuse io_conf)
    io_conf.pin_bit_mask = (1ULL << CLK_GPIO);
    gpio_config(&io_conf);

    // 3. Install ISR Service and Link the Handler
    
    gpio_isr_handler_add(CLK_GPIO, encoder_isr_handler, NULL);

}

ui_screen_t encoder_get_screen_state(void) {
    // This logic ensures the counter stays within 0 to (MAX-1)
    int state = (encoder_counter/STEPS_PER_STATE) % UI_STATE_MAX;
    ESP_LOGI("SENSOR", "Encoder Count: %d", encoder_counter);
    if (state < 0) state += UI_STATE_MAX; 
    return (ui_screen_t)state;
}