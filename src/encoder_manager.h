#ifndef ENCODER_MANAGER_H
#define ENCODER_MANAGER_H

typedef enum {
    UI_STATE_ACCEL = 0,
    UI_STATE_GYRO,
    UI_STATE_STATS,
    UI_STATE_MAX // Helper to wrap back to 0
} ui_screen_t;

void encoder_init(void);
// We'll use this in ui_manager.c to see which screen to draw
ui_screen_t encoder_get_screen_state(void);

#endif