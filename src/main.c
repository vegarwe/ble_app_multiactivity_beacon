#include <stdint.h>
#include <string.h>
#include "ble.h"
#include "nrf.h"
#include "nrf51_bitfields.h"
#include "nrf_soc.h"
#include "nrf_sdm.h"

#include "app_ibeacon.h"

void assert_nrf_callback(uint32_t pc, uint16_t line_num, const uint8_t * p_file_name)
{
    (void)pc;
    while(true) {}
}


static void ble_stack_init(void)
{
    uint32_t err_code;

    err_code = sd_softdevice_enable(NRF_CLOCK_LFCLKSRC_XTAL_20_PPM, assert_nrf_callback);
    sd_nvic_EnableIRQ(SD_EVT_IRQn);
}

static void on_ble_evt(ble_evt_t * p_ble_evt)
{
    uint32_t        err_code;

    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GAP_EVT_CONNECTED:
            break;
        case BLE_GAP_EVT_DISCONNECTED:
            break;
        case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
            break;
        case BLE_GAP_EVT_TIMEOUT:
            break;
        default:
            break;
    }
}


static void ble_evt_dispatch(ble_evt_t * p_ble_evt)
{
    on_ble_evt(p_ble_evt);
}


static void sys_evt_dispatch(uint32_t sys_evt)
{
}


void SD_EVT_IRQHandler(void)
{
    uint32_t event;

    if ( sd_evt_get(&event) == NRF_SUCCESS )
    {
        sys_evt_dispatch(event);
    }

    (void) ble_evt_dispatch;
}


int main(void)
{
    uint32_t err_code;

    ble_stack_init();
    app_ibeacon_init();

    app_ibeacon_start();

    for (;;)
    {
        err_code = sd_app_evt_wait();
    }
}
