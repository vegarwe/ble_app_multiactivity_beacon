#include "app_beacon.h"
#include <stdio.h>
#include <string.h>

#include "nrf_soc.h"
#include "app_error.h"

static struct
{
    ble_uuid128_t       uuid;
    uint32_t            adv_interval;
    uint16_t            major;
    uint16_t            minor;
    bool                keep_running;
    bool                is_running;
    uint32_t            slot_length;
    nrf_radio_request_t timeslot_request;
} m_beacon;

enum mode_t
{
  ADV_INIT,
  ADV_RX_CH37,
  ADV_RX_CH38,
  ADV_RX_CH39,
  ADV_DONE
};

nrf_radio_request_t * m_configure_next_event(void)
{
    m_beacon.timeslot_request.request_type              = NRF_RADIO_REQ_TYPE_NORMAL;
    m_beacon.timeslot_request.params.normal.hfclk       = NRF_RADIO_HFCLK_CFG_DEFAULT;
    m_beacon.timeslot_request.params.normal.priority    = NRF_RADIO_PRIORITY_NORMAL;
    m_beacon.timeslot_request.params.normal.distance_us = m_beacon.adv_interval * 1000;
    m_beacon.timeslot_request.params.normal.length_us   = m_beacon.slot_length;
    return &m_beacon.timeslot_request;
}

void m_reqeust_earliest(enum NRF_RADIO_PRIORITY priority)
{
    m_beacon.timeslot_request.request_type                = NRF_RADIO_REQ_TYPE_EARLIEST;
    m_beacon.timeslot_request.params.earliest.hfclk       = NRF_RADIO_HFCLK_CFG_DEFAULT;
    m_beacon.timeslot_request.params.earliest.priority    = priority;
    m_beacon.timeslot_request.params.earliest.length_us   = m_beacon.slot_length;
    m_beacon.timeslot_request.params.earliest.timeout_us  = 1000000;
    sd_radio_request(&m_beacon.timeslot_request);
}

static uint8_t * m_get_adv_packet(void)
{
    static uint8_t adv_pdu[40] = {
        /*  0 */  0x02, // ADV_NONCONN_IND + rfu
        /*  1 */    36, // length (from adv address out...) + rf
        /*  2 */  0x00, // Extra byte used to map into the radio register. See ul_pdu_fields.h
        /*  3 */  0x0a, // adv addr
        /*  4 */  0xa9, // adv addr
        /*  5 */  0x71, // adv addr
        /*  6 */  0xca, // adv addr
        /*  7 */  0xce, // adv addr
        /*  8 */  0x1f, // adv addr

        /*  9 */  0x02, // adv param length
        /* 10 */  0x01, // adv flags
        /* 11 */  0x04, // BrEdrNotSupported

        /* 12 */  0x1a, // adv param length
        /* 13 */  0xff, // manufactor specific data
        /* 14 */  0x4c, // 0x004c == Apple Inc.
        /* 15 */  0x00, // 
        /* 16 */  0x02, // device type == beacon
        /* 17 */  0x15, // manufactor specific data length
        /* 18 */  0x00, // beacon uuid
        /* 19 */  0x00, // beacon uuid
        /* 20 */  0x00, // beacon uuid
        /* 21 */  0x00, // beacon uuid
        /* 22 */  0x00, // beacon uuid
        /* 23 */  0x00, // beacon uuid
        /* 24 */  0x00, // beacon uuid
        /* 25 */  0x00, // beacon uuid
        /* 26 */  0x00, // beacon uuid
        /* 27 */  0x00, // beacon uuid
        /* 28 */  0x00, // beacon uuid
        /* 29 */  0x00, // beacon uuid
        /* 30 */  0x00, // beacon uuid
        /* 31 */  0x00, // beacon uuid
        /* 32 */  0x00, // beacon uuid
        /* 33 */  0x00, // beacon uuid

        /* 34 */  0x00, // Major
        /* 35 */  0x00, //
        /* 36 */  0x00, // Minor
        /* 37 */  0x00, //

        /* 38 */  0x3c, // measured RSSI at 1 meter distance in dBm
        /* 39 */  0x00
        };
    memcpy(&adv_pdu[18], &m_beacon.uuid, sizeof(ble_uuid128_t));
    memcpy(&adv_pdu[34], (uint8_t*) &m_beacon.major, sizeof(uint16_t));
    memcpy(&adv_pdu[36], (uint8_t*) &m_beacon.minor, sizeof(uint16_t));

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
    NRF_RADIO->CRCCNF       =   (((RADIO_CRCCNF_SKIPADDR_Skip)    << RADIO_CRCCNF_SKIPADDR_Pos ) & RADIO_CRCCNF_SKIPADDR_Msk)
                              | (((RADIO_CRCCNF_LEN_Three)        << RADIO_CRCCNF_LEN_Pos      ) & RADIO_CRCCNF_LEN_Msk);
    NRF_RADIO->CRCPOLY      = 0x0000065b;
    NRF_RADIO->RXADDRESSES  = ((RADIO_RXADDRESSES_ADDR0_Enabled)  << RADIO_RXADDRESSES_ADDR0_Pos);
    NRF_RADIO->SHORTS       = ((1 << RADIO_SHORTS_READY_START_Pos) | (1 << RADIO_SHORTS_END_DISABLE_Pos));
    NRF_RADIO->MODE         = ((RADIO_MODE_MODE_Ble_1Mbit)        << RADIO_MODE_MODE_Pos       ) & RADIO_MODE_MODE_Msk;
    NRF_RADIO->TIFS         = 150;
    NRF_RADIO->INTENSET     = (1 << RADIO_INTENSET_DISABLED_Pos);
    NRF_RADIO->PREFIX0      = 0x0000008e; //access_addr[3]
    NRF_RADIO->BASE0        = 0x89bed600; //access_addr[0:3]
    NRF_RADIO->CRCINIT      = 0x00555555;
    NRF_RADIO->PACKETPTR    = (uint32_t) p_adv_pdu;
    
    NVIC_EnableIRQ(RADIO_IRQn);
}

void m_handle_start(void)
{
    // Configure TX_EN on TIMER EVENT_0
    NRF_PPI->CH[8].TEP    = (uint32_t)(&NRF_RADIO->TASKS_TXEN);
    NRF_PPI->CH[8].EEP    = (uint32_t)(&NRF_TIMER0->EVENTS_COMPARE[0]);
    NRF_PPI->CHENSET      = (1 << 8);
    
    // Configure and initiate radio
    m_configure_radio();
    NRF_RADIO->TASKS_DISABLE = 1;
}

void m_handle_radio_disabled(enum mode_t mode)
{
    switch (mode)
    {
        case ADV_RX_CH37:
            m_set_adv_ch(37);
            NRF_RADIO->TASKS_TXEN = 1;
            break;
        case ADV_RX_CH38:
            m_set_adv_ch(38);
            NRF_TIMER0->TASKS_CLEAR = 1;
            NRF_TIMER0->CC[0]       = 400;
            break;
        case ADV_RX_CH39:
            m_set_adv_ch(39);
            NRF_TIMER0->TASKS_CLEAR = 1;
            NRF_TIMER0->CC[0]       = 400;
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

      mode = ADV_INIT;
      mode++;
      break;
    case NRF_RADIO_CALLBACK_SIGNAL_TYPE_RADIO:
      if (NRF_RADIO->EVENTS_DISABLED == 1)
      {
        NRF_RADIO->EVENTS_DISABLED = 0;

        m_handle_radio_disabled(mode);

        if (mode == ADV_DONE)
        {
            if (m_beacon.keep_running)
            {
                signal_callback_return_param.params.request.p_next = m_configure_next_event();
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

void app_beacon_sd_evt_signal_handler(uint32_t event)
{
    switch (event)
    {
        case NRF_EVT_RADIO_SESSION_IDLE:
            if (m_beacon.is_running)
            {
                m_beacon.is_running = false;
                sd_radio_session_close();
            }
            break;
        case NRF_EVT_RADIO_SESSION_CLOSED:
            break;
        case NRF_EVT_RADIO_BLOCKED:
        case NRF_EVT_RADIO_CANCELED: // Fall through
            if (m_beacon.keep_running)
            {
                // TODO: A proper solution should try again in <block_count> * m_beacon.adv_interval
                m_reqeust_earliest(NRF_RADIO_PRIORITY_NORMAL);
            }
            break;
        default:
            break;
    }
}

void app_beacon_init(ble_beacon_init_t * app_beacon_init)
{
    memcpy(&m_beacon.uuid, &app_beacon_init->uuid, sizeof(ble_beacon_init_t));
    m_beacon.adv_interval = app_beacon_init->adv_interval;
    m_beacon.major        = app_beacon_init->major;
    m_beacon.minor        = app_beacon_init->minor;
    m_beacon.slot_length  = 5500;
}

void app_beacon_start(void)
{
    m_beacon.keep_running = true;
    m_beacon.is_running   = true;

    sd_radio_session_open(m_timeslot_callback);
    
    m_reqeust_earliest(NRF_RADIO_PRIORITY_NORMAL);
}

void app_beacon_stop(void)
{
    m_beacon.keep_running = false;
    while (m_beacon.is_running) {} // Need some proper handling of timeout and power saving here.
}

