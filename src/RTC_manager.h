#ifndef RTC_MANAGER_H
#define RTC_MANAGER_H

#include <time.h>           // Essential for struct tm
#include "driver/i2c_master.h"

void sync_logic(i2c_master_dev_handle_t rtc_handle);

// Change 'void' to 'struct tm' here
struct tm get_and_return_time(char *buffer, size_t max_len);

#endif