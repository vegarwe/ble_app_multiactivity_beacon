#include "app_beacon_scanner.h"
#include <stdio.h>
#include <string.h>

#include "nrf_soc.h"
#include "app_error.h"

#define SCAN_INTERVAL_US 10000

static struct
{
    ble_uuid128_t       uuid;
    bool                keep_running;
    bool                is_running;
    nrf_radio_request_t timeslot_request;
} m_beacon_scanner;

enum mode_t
{
  SCN_INIT,
  SCN_1,
  SCN_2,
  SCN_3,
  SCN_DONE
};

static nrf_radio_request_t * m_reqeust_earliest(enum NRF_RADIO_PRIORITY priority)
{
    m_beacon_scanner.timeslot_request.request_type                = NRF_RADIO_REQ_TYPE_EARLIEST;
    m_beacon_scanner.timeslot_request.params.earliest.hfclk       = NRF_RADIO_HFCLK_CFG_DEFAULT;
    m_beacon_scanner.timeslot_request.params.earliest.priority    = priority;
    m_beacon_scanner.timeslot_request.params.earliest.length_us   = SCAN_INTERVAL_US * 3 + 1000;
    m_beacon_scanner.timeslot_request.params.earliest.timeout_us  = 1000000;
    return &m_beacon_scanner.timeslot_request;
}

static uint8_t * m_get_adv_packet(void)
{
    static uint8_t adv_pdu[40];

    return &adv_pdu[0];
}

static void m_set_adv_ch(uint32_t channel)
{
    if (channel == 37)
    {
        NRF_RADIO->FREQUENCY    =  2;
        NRF_RADIO->DATAWHITEIV  = 37;
    }
    if (channel == 38)
    {
        NRF_RADIO->FREQUENCY    = 26;
        NRF_RADIO->DATAWHITEIV  = 38;
    }
    if (channel == 39)
    {
        NRF_RADIO->FREQUENCY    = 80;
        NRF_RADIO->DATAWHITEIV  = 39;
    }
}

static void m_configure_radio()
{
    uint8_t * p_adv_pdu = m_get_adv_packet();

    NRF_RADIO->POWER        = 1;
    NRF_RADIO->PCNF0        =   (((1UL) << RADIO_PCNF0_S0LEN_Pos                               ) & RADIO_PCNF0_S0LEN_Msk)
                              | (((2UL) << RADIO_PCNF0_S1LEN_Pos                               ) & RADIO_PCNF0_S1LEN_Msk)
                              | (((6UL) << RADIO_PCNF0_LFLEN_Pos                               ) & RADIO_PCNF0_LFLEN_Msk);
    NRF_RADIO->PCNF1        =   (((RADIO_PCNF1_ENDIAN_Little)     << RADIO_PCNF1_ENDIAN_Pos    ) & RADIO_PCNF1_ENDIAN_Msk)
                              | (((3UL)                           << RADIO_PCNF1_BALEN_Pos     ) & RADIO_PCNF1_BALEN_Msk)
                              | (((0UL)                           << RADIO_PCNF1_STATLEN_Pos   ) & RADIO_PCNF1_STATLEN_Msk)
                              | ((((uint32_t) 37)                 << RADIO_PCNF1_MAXLEN_Pos    ) & RADIO_PCNF1_MAXLEN_Msk)
                              | ((RADIO_PCNF1_WHITEEN_Enabled     << RADIO_PCNF1_WHITEEN_Pos   ) & RADIO_PCNF1_WHITEEN_Msk);
    NRF_RADIO->CRCCNF       =   (((RADIO_CRCCNF_SKIP_ADDR_Skip)   << RADIO_CRCCNF_SKIP_ADDR_Pos) & RADIO_CRCCNF_SKIP_ADDR_Msk)
                              | (((RADIO_CRCCNF_LEN_Three)        << RADIO_CRCCNF_LEN_Pos      ) & RADIO_CRCCNF_LEN_Msk);
    NRF_RADIO->CRCPOLY      = 0x0000065b;
    NRF_RADIO->RXADDRESSES  = ((RADIO_RXADDRESSES_ADDR0_Enabled)  << RADIO_RXADDRESSES_ADDR0_Pos);
    NRF_RADIO->SHORTS       = ((1 << RADIO_SHORTS_READY_START_Pos) | (1 << RADIO_SHORTS_END_DISABLE_Pos));
    NRF_RADIO->MODE         = ((RADIO_MODE_MODE_Ble_1Mbit)        << RADIO_MODE_MODE_Pos       ) & RADIO_MODE_MODE_Msk;
    NRF_RADIO->TIFS         = 150;
    NRF_RADIO->INTENSET     = (1 << RADIO_INTENSET_DISABLED_Pos) | (1 << RADIO_INTENSET_ADDRESS_Pos);
    NRF_RADIO->PREFIX0      = 0x0000008e; //access_addr[3]
    NRF_RADIO->BASE0        = 0x89bed600; //access_addr[0:3]
    NRF_RADIO->CRCINIT      = 0x00555555;
    NRF_RADIO->PACKETPTR    = (uint32_t) p_adv_pdu;
    
    NVIC_EnableIRQ(RADIO_IRQn);
}

static void m_handle_start(void)
{
    // Configure TX_EN on TIMER EVENT_0
    NRF_PPI->CH[8].TEP    = (uint32_t)(&NRF_RADIO->TASKS_DISABLE);
    NRF_PPI->CH[8].EEP    = (uint32_t)(&NRF_RADIO_MULTITIMER->EVENTS_COMPARE[0]);
    NRF_PPI->CHENSET      = (1 << 8);

    
    // Configure and initiate radio
    m_configure_radio();
    NRF_RADIO->TASKS_DISABLE = 1;
    NRF_RADIO_MULTITIMER->CC[0] = 0; // TODO: Necessary?
}

static void m_handle_radio_disabled(enum mode_t mode)
{
    NRF_RADIO_MULTITIMER->CC[0]       += SCAN_INTERVAL_US;
    
    switch (mode)
    {
        case SCN_1:
            m_set_adv_ch(37);
            NRF_RADIO->TASKS_RXEN = 1;
            break;
        case SCN_2:
            m_set_adv_ch(38);
            NRF_RADIO->TASKS_RXEN = 1;
            break;
        case SCN_3:
            m_set_adv_ch(39);
            NRF_RADIO->TASKS_RXEN = 1;
            NRF_GPIO->OUT ^= (1UL << 19);
            break;
        default:
            break;
    }
}

static nrf_radio_signal_callback_return_param_t * m_timeslot_callback(uint8_t signal_type)
{
  static nrf_radio_signal_callback_return_param_t signal_callback_return_param;
  static enum mode_t mode;

  signal_callback_return_param.params.request.p_next  = NULL;
  signal_callback_return_param.callback_action        = NRF_RADIO_SIGNAL_CALLBACK_ACTION_NONE;

  switch (signal_type)
  {
    case NRF_RADIO_CALLBACK_SIGNAL_TYPE_START:

      m_handle_start();

      mode = SCN_INIT;
      mode++;
      break;
    case NRF_RADIO_CALLBACK_SIGNAL_TYPE_RADIO:
      if (NRF_RADIO->EVENTS_ADDRESS == 1)
      {
        NRF_RADIO->EVENTS_ADDRESS = 0;

        NRF_GPIO->OUT ^= (1UL << 18);
      }

      if (NRF_RADIO->EVENTS_DISABLED == 1)
      {
        NRF_RADIO->EVENTS_DISABLED = 0;

        m_handle_radio_disabled(mode);

        if (mode == SCN_DONE)
        {
            if (m_beacon_scanner.keep_running)
            {
                signal_callback_return_param.params.request.p_next = m_reqeust_earliest(NRF_RADIO_PRIORITY_NORMAL);
                signal_callback_return_param.callback_action       = NRF_RADIO_SIGNAL_CALLBACK_ACTION_REQUEST_AND_END;
            }
            else
            {
                signal_callback_return_param.callback_action       = NRF_RADIO_SIGNAL_CALLBACK_ACTION_END;
            }
            break;
        }
        mode++;
      }
      break;
    default:
      APP_ASSERT( false );
      break;
  }

  return ( &signal_callback_return_param );
}

void app_beacon_scanner_sd_evt_signal_handler(uint32_t event)
{
    switch (event)
    {
        case NRF_EVT_RADIO_SESSION_IDLE:
            sd_radio_session_close();
            break;
        case NRF_EVT_RADIO_SESSION_CLOSED:
            break;
        case NRF_EVT_RADIO_BLOCKED:
        case NRF_EVT_RADIO_CANCELED: // Fall through
            if (m_beacon_scanner.keep_running)
            {
                sd_radio_request(m_reqeust_earliest(NRF_RADIO_PRIORITY_NORMAL));
            }
            break;
        default:
            break;
    }
}

void app_beacon_scanner_init(ble_beacon_scanner_init_t * app_beacon_scanner_init)
{
    memcpy(&m_beacon_scanner.uuid, &app_beacon_scanner_init->uuid, sizeof(ble_beacon_scanner_init_t));
}

void app_beacon_scanner_start(void)
{
    NRF_GPIO->DIRSET = (1UL << 18) | (1UL << 19);

    m_beacon_scanner.keep_running = true;
    m_beacon_scanner.is_running   = true;

    sd_radio_session_open(m_timeslot_callback);
    
    sd_radio_request(m_reqeust_earliest(NRF_RADIO_PRIORITY_NORMAL));
}

void app_beacon_scanner_stop(void)
{
    m_beacon_scanner.keep_running = false;
    while (m_beacon_scanner.is_running) {} // Need some proper handling of timeout and power saving here.
}
