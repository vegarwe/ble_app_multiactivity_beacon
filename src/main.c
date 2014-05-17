#include <stdint.h>
#include <string.h>
#include "nrf.h"
#include "ble.h"
#include "nrf_sdm.h"
#include "ble_gap.h"

#include "app_error.h"
#include "app_beacon.h"
#include "app_beacon_scanner.h"

static ble_beacon_init_t beacon_init = {
    .uuid = { 0xff, 0xfe, 0x2d, 0x12, 0x1e, 0x4b, 0x0f, 0xa4,
              0x99, 0x4e, 0xce, 0xb5, 0x31, 0xf4, 0x05, 0x45 },
    .adv_interval = 800,
    .major        = 0x1234,
    .minor        = 0x5678
};

static ble_beacon_scanner_init_t beacon_scanner_init = {
    .uuid = { 0xff, 0xfe, 0x2d, 0x12, 0x1e, 0x4b, 0x0f, 0xa4,
              0x99, 0x4e, 0xce, 0xb5, 0x31, 0xf4, 0x05, 0x45 }
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

static void advertiser_start(void)
{
    static ble_gap_adv_params_t adv_params = {
        .type        = BLE_GAP_ADV_TYPE_ADV_IND,
        .p_peer_addr = 0,
        .fp          = BLE_GAP_ADV_FP_ANY,
        .interval    = 100
        .timeout     = 0;
    };
    sd_ble_gap_adv_start(&adv_params);
}

static void on_ble_evt(ble_evt_t * p_ble_evt)
{
    uint32_t        err_code      = NRF_SUCCESS;
    static uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID;

    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GAP_EVT_CONNECTED:
            NRF_GPIO->DIRSET = (1UL << 18);
            NRF_GPIO->OUTSET = (1UL << 18);
            m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            NRF_GPIO->OUTCLR = (1UL << 18);
            advertiser_start();
            break;

        default:
            break;
    }

    APP_ERR_CHECK(err_code);
}

static void sys_evt_dispatch(uint32_t sys_evt)
{
    app_beacon_sd_evt_signal_handler(sys_evt);
    app_beacon_scanner_sd_evt_signal_handler(sys_evt);
}

static void ble_evt_dispatch(ble_evt_t * p_ble_evt)
{
    on_ble_evt(p_ble_evt);
}

void SD_EVT_IRQHandler(void)
{
    uint8_t    evt_buf[sizeof(ble_evt_t) + BLE_L2CAP_MTU_DEF];
    uint16_t   evt_len;
    ble_evt_t *p_ble_evt = (ble_evt_t *) evt_buf;

    uint32_t event;

    while ( sd_evt_get(&event) == NRF_SUCCESS )
    {
        sys_evt_dispatch(event);
    }

    evt_len = sizeof(evt_buf);
    while (sd_ble_evt_get(evt_buf, &evt_len) == NRF_SUCCESS)
    {
        ble_evt_dispatch(p_ble_evt);
        evt_len = sizeof(evt_buf);
    }
}


int main(void)
{
    uint32_t err_code;

    ble_stack_init();

    //(void)beacon_scanner_init;
    //app_beacon_init(&beacon_init);
    //app_beacon_start();

    (void)beacon_init;
    app_beacon_scanner_init(&beacon_scanner_init);
    app_beacon_scanner_start();

    advertiser_start();

    for (;;)
    {
        err_code = sd_app_evt_wait();
        APP_ERR_CHECK(err_code);
    }
}
