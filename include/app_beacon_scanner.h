#ifndef APP_BEACON_SCANNER_H__
#define APP_BEACON_SCANNER_H__

#include <stdint.h>
#include <stdbool.h>
#include "ble_types.h"

typedef struct
{
    ble_uuid128_t uuid;
} ble_beacon_scanner_init_t;

void app_beacon_scanner_sd_evt_signal_handler(uint32_t event);
void app_beacon_scanner_init(ble_beacon_scanner_init_t * app_beacon_init);
void app_beacon_scanner_start(void);

#endif // APP_BEACON_SCANNER_H__
