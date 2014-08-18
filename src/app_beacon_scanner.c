#include "app_beacon_scanner.h"
#include <stdio.h>
#include <string.h>

#include "nrf_soc.h"
#include "app_error.h"

#define SCAN_INTERVAL_US 10000
#define TIMESLOT_LEN_US  SCAN_INTERVAL_US * 3 + 1000

static struct
{
    ble_uuid128_t       uuid;
    bool                keep_running;
    bool                is_running;
    nrf_radio_request_t timeslot_request;
    uint8_t             scn_pdu[40];
} m_beacon_scanner;


enum mode_t
{
  SCN_INIT,
  SCN_1,
  SCN_2,
  SCN_3,
  SCN_DONE
};

static bool field_has_beacon_data(uint8_t * field, uint8_t field_len)
{
    if (field_len != 0x1a)                     return false; // The beacon data field is 0x1a long
    if (field[0]  != 0xff)                     return false; // No manufactor specific data
    if (field[1]  != 0x4c || field[2] != 0x00) return false; // Not Apple Inc. 
    if (field[3]  == 0x02)                                   // Device type beacon
    {
        return true;
    }
    return false;
}

static bool has_beacon_data(uint8_t * pdu)
{
    uint8_t i         = 9; // Adv data start at this index in the pdu
    uint8_t field_len = 0;
    while (i < 40)
    {
        field_len = pdu[i];
        if (field_len > 39) // Field data corrupt or invalid
        {
            return false;
        }
        if (field_has_beacon_data(&pdu[i+1], field_len))
        {
            return true;
        }
        i += field_len + 1;

    }
    return false;
}

static nrf_radio_request_t * m_reqeust_earliest(enum NRF_RADIO_PRIORITY priority)
{
    m_beacon_scanner.timeslot_request.request_type                = NRF_RADIO_REQ_TYPE_EARLIEST;
    m_beacon_scanner.timeslot_request.params.earliest.hfclk       = NRF_RADIO_HFCLK_CFG_DEFAULT;
    m_beacon_scanner.timeslot_request.params.earliest.priority    = priority;
    m_beacon_scanner.timeslot_request.params.earliest.length_us   = TIMESLOT_LEN_US;
    m_beacon_scanner.timeslot_request.params.earliest.timeout_us  = 1000000;
    return &m_beacon_scanner.timeslot_request;
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
    NRF_RADIO->INTENSET     = (1 << RADIO_INTENSET_DISABLED_Pos) | (1 << RADIO_INTENSET_ADDRESS_Pos);
    NRF_RADIO->PREFIX0      = 0x0000008e; //access_addr[3]
    NRF_RADIO->BASE0        = 0x89bed600; //access_addr[0:3]
    NRF_RADIO->CRCINIT      = 0x00555555;
    NRF_RADIO->PACKETPTR    = (uint32_t) &m_beacon_scanner.scn_pdu[0];
    
    NVIC_EnableIRQ(RADIO_IRQn);
}

static void m_handle_start(void)
{
    // Configure TX_EN on TIMER EVENT_0
    NRF_PPI->CH[8].TEP    = (uint32_t)(&NRF_RADIO->TASKS_DISABLE);
    NRF_PPI->CH[8].EEP    = (uint32_t)(&NRF_TIMER0->EVENTS_COMPARE[0]);
    NRF_PPI->CHENSET      = (1 << 8);

    
    // Configure and initiate radio
    m_configure_radio();
    NRF_RADIO->TASKS_DISABLE = 1;
    NRF_TIMER0->CC[0] = 0; // TODO: Necessary?

    // Set up rescheduling
    NRF_TIMER0->INTENSET = (1UL << TIMER_INTENSET_COMPARE1_Pos);
    NRF_TIMER0->CC[1]    = TIMESLOT_LEN_US - 800;
    NVIC_EnableIRQ(TIMER0_IRQn);
}

static void m_handle_radio_disabled(enum mode_t mode)
{
    NRF_TIMER0->CC[0]       += SCAN_INTERVAL_US;
    
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

            if (has_beacon_data(&m_beacon_scanner.scn_pdu[0]))
            {
                NRF_GPIO->OUT ^= (1UL << 18);
            }
        }

        if (NRF_RADIO->EVENTS_DISABLED == 1)
        {
            NRF_RADIO->EVENTS_DISABLED = 0;

            if (mode == SCN_DONE)
            {
                mode = SCN_INIT;
                mode++;
            }

            m_handle_radio_disabled(mode);

            mode++;
        }
        break;
    case NRF_RADIO_CALLBACK_SIGNAL_TYPE_TIMER0:
        if (NRF_TIMER0->EVENTS_COMPARE[1] == 1)
        {
            NRF_TIMER0->EVENTS_COMPARE[1] = 0;

            signal_callback_return_param.params.extend.length_us = TIMESLOT_LEN_US;
            signal_callback_return_param.callback_action         = NRF_RADIO_SIGNAL_CALLBACK_ACTION_EXTEND;
        }

        break;
    case NRF_RADIO_CALLBACK_SIGNAL_TYPE_EXTEND_FAILED:
        NRF_GPIO->OUT ^= (1UL << 19);
        if (m_beacon_scanner.keep_running)
        {
            signal_callback_return_param.params.request.p_next   = m_reqeust_earliest(NRF_RADIO_PRIORITY_NORMAL);
            signal_callback_return_param.callback_action         = NRF_RADIO_SIGNAL_CALLBACK_ACTION_REQUEST_AND_END;
        }
        else
        {
            signal_callback_return_param.callback_action         = NRF_RADIO_SIGNAL_CALLBACK_ACTION_END;
        }
        break;
    case NRF_RADIO_CALLBACK_SIGNAL_TYPE_EXTEND_SUCCEEDED:
        NRF_TIMER0->CC[1]    += TIMESLOT_LEN_US;
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
            if (m_beacon_scanner.is_running)
            {
                m_beacon_scanner.is_running = false;
                sd_radio_session_close();
            }
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

