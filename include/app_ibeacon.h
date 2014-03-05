#ifndef APP_IBEACON_H__
#define APP_IBEACON_H__

#include <stdint.h>

void app_ibeacon_sd_evt_signal_handler(uint32_t event);
void app_ibeacon_init(void);
void app_ibeacon_start(void);

#endif // APP_IBEACON_H__
