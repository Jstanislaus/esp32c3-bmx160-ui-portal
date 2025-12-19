#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "driver/i2c_master.h" // FIX: Include the handle definition

// Only list the functions you want to call from main.c
void ui_task(void *pvParameters);

// Updated to accept the New Master Handle instead of the old library struct
void display_splash_screen(i2c_master_dev_handle_t dev);

#endif /* UI_MANAGER_H */