#ifndef APP_IBEACON_H__
#define APP_IBEACON_H__

#include <stdint.h>
#include "ble_types.h"

typedef struct
{
    ble_uuid128_t uuid; 
} ble_beacon_init_t;

void app_beacon_sd_evt_signal_handler(uint32_t event);
void app_beacon_init(void);
void app_beacon_start(void);

#endif // APP_IBEACON_H__
