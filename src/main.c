#include <stdint.h>
#include <string.h>
#include "nrf.h"
#include "nrf_sdm.h"

#include "app_beacon.h"
#include "app_error.h"

static ble_beacon_init_t beacon_init = {
    .uuid = { 0xff, 0xfe, 0x2d, 0x12, 0x1e, 0x4b, 0x0f, 0xa4,
              0x99, 0x4e, 0xce, 0xb5, 0x31, 0xf4, 0x05, 0x45 },
    .adv_interval = 800,
    .major        = 0x1234,
    .minor        = 0x5678
};

void assert_nrf_callback(uint32_t pc, uint16_t line_num, const uint8_t * p_file_name)
{
    while(true) {}
}

static void ble_stack_init(void)
{
    uint32_t err_code;

    err_code = sd_softdevice_enable(NRF_CLOCK_LFCLKSRC_XTAL_20_PPM, assert_nrf_callback);
    APP_ERR_CHECK(err_code);

    err_code = sd_nvic_EnableIRQ(SD_EVT_IRQn);
    APP_ERR_CHECK(err_code);
}

static void sys_evt_dispatch(uint32_t sys_evt)
{
    app_beacon_sd_evt_signal_handler(sys_evt);
}

void SD_EVT_IRQHandler(void)
{
    uint32_t event;

    if ( sd_evt_get(&event) == NRF_SUCCESS )
    {
        sys_evt_dispatch(event);
    }
}


int main(void)
{
    uint32_t err_code;

    ble_stack_init();
    app_beacon_init(&beacon_init);

    app_beacon_start();

    for (;;)
    {
        err_code = sd_app_evt_wait();
        APP_ERR_CHECK(err_code);
    }
}
