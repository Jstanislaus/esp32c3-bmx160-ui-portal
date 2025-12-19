#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "ssd1306.h"
// Only list the functions you want to call from main.c
void ui_task(void *pvParameters);
void display_splash_screen(SSD1306_t *dev);

#endif /* UI_MANAGER_H */