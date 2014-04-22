#ifndef APP_IBEACON_H__
#define APP_IBEACON_H__

#include <stdint.h>
#include <stdbool.h>
#include "ble_types.h"

typedef struct
{
    ble_uuid128_t uuid;
    uint32_t      adv_interval;
    uint16_t      major;
    uint16_t      minor;
} ble_beacon_init_t;

void app_beacon_sd_evt_signal_handler(uint32_t event);
void app_beacon_init(ble_beacon_init_t * app_beacon_init);
void app_beacon_start(void);

#endif // APP_IBEACON_H__
