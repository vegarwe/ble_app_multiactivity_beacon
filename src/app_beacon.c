#include "app_beacon.h"
#include <stdio.h>
#include <string.h>

#include "app_error.h"
#include "nrf_sdm.h"
#include "nrf_soc.h"
#include "nrf51.h"
#include "nrf51_bitfields.h"

static nrf_radio_request_t             m_timeslot_request;
static bool volatile                   m_keep_running;
static bool volatile                   m_is_running;
static uint32_t                        m_slot_distance;
static uint32_t                        m_slot_length;
enum mode_t
{
  ADV_INIT,
  ADV_RX_CH37,
  ADV_RX_CH38,
  ADV_RX_CH39,
  ADV_DONE
};

void app_beacon_sd_evt_signal_handler(uint32_t event)
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
            if (m_keep_running)
            {
                // TODO: A proper solution should try again in <block_count> * m_slot_distance
                m_timeslot_request.request_type                = NRF_RADIO_REQ_TYPE_EARLIEST;
                m_timeslot_request.params.earliest.hfclk       = NRF_RADIO_HFCLK_CFG_DEFAULT;
                m_timeslot_request.params.earliest.priority    = NRF_RADIO_PRIORITY_NORMAL;
                m_timeslot_request.params.earliest.length_us   = m_slot_length;
                m_timeslot_request.params.earliest.timeout_us  = 1000000;
                sd_radio_request(&m_timeslot_request);
            }
            break;
        default:
            break;
    }
}

static uint8_t * m_get_adv_packet(void)
{
    static uint8_t adv_pdu[40] = {
        /*  0 */  0x02, // ADV_NONCONN_IND + rfu
        /*  1 */    21, // length (from adv address out...) + rf
        /*  2 */  0x00, // Extra byte used to map into the radio register. See ul_pdu_fields.h
        /*  3 */  0x0a, // adv addr
        /*  4 */  0xa9, // adv addr
        /*  5 */  0x71, // adv addr
        /*  6 */  0xca, // adv addr
        /*  7 */  0xce, // adv addr
        /*  8 */  0x1f, // adv addr
        /*  9 */    14, // adv param length
        /* 10 */  0x09, // adv param type (complete device name)
        /* 11 */   'F', // adv param data
        /* 12 */   'i', // adv param data
        /* 13 */   's', // adv param data
        /* 14 */   'k', // adv param data
        /* 15 */   'e', // adv param data
        /* 16 */   'n', // adv param data
        /* 17 */   '_', // adv param data
        /* 18 */   'f', // adv param data
        /* 19 */   'j', // adv param data
        /* 20 */   'a', // adv param data
        /* 21 */   's', // adv param data
        /* 22 */   'e', // adv param data
        /* 23 */   'r', // adv param data
        /* 24 */   '_',
        /* 25 */   'b',
        /* 26 */   'a',
        /* 27 */   'r',
        /* 28 */   'e',
        /* 29 */   '.',
        /* 30 */  0x00,
        /* 31 */  0x00,
        /* 32 */  0x00,
        /* 33 */  0x00,
        /* 34 */  0x00,
        /* 35 */  0x00,
        /* 36 */  0x00,
        /* 37 */  0x00,
        /* 38 */  0x00,
        /* 39 */  0x00
        };
    (void) adv_pdu;

    return &adv_pdu[0];
}

static void m_set_adv_ch(uint32_t channel)
{
    uint8_t * p_adv_pdu = m_get_adv_packet();
    p_adv_pdu[23]       = (uint8_t) channel & 0xff; // TODO: Only for debugging

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
    NRF_RADIO->PCNF0        =   (((1UL) << RADIO_PCNF0_S0LEN_Pos) & RADIO_PCNF0_S0LEN_Msk)
                              | (((2UL) << RADIO_PCNF0_S1LEN_Pos) & RADIO_PCNF0_S1LEN_Msk)
                              | (((6UL) << RADIO_PCNF0_LFLEN_Pos) & RADIO_PCNF0_LFLEN_Msk);
    NRF_RADIO->PCNF1        =   (((RADIO_PCNF1_ENDIAN_Little)   << RADIO_PCNF1_ENDIAN_Pos)     & RADIO_PCNF1_ENDIAN_Msk)
                              | (((3UL)                         << RADIO_PCNF1_BALEN_Pos)      & RADIO_PCNF1_BALEN_Msk)
                              | (((0UL)                         << RADIO_PCNF1_STATLEN_Pos)    & RADIO_PCNF1_STATLEN_Msk)
                              | ((((uint32_t) 37)               << RADIO_PCNF1_MAXLEN_Pos)     & RADIO_PCNF1_MAXLEN_Msk)
                              | ((RADIO_PCNF1_WHITEEN_Enabled   << RADIO_PCNF1_WHITEEN_Pos)    & RADIO_PCNF1_WHITEEN_Msk);
    NRF_RADIO->CRCCNF       =   (((RADIO_CRCCNF_SKIP_ADDR_Skip) << RADIO_CRCCNF_SKIP_ADDR_Pos) & RADIO_CRCCNF_SKIP_ADDR_Msk)
                              | (((RADIO_CRCCNF_LEN_Three)      << RADIO_CRCCNF_LEN_Pos)       & RADIO_CRCCNF_LEN_Msk);
    NRF_RADIO->CRCPOLY      = 0x0000065b;
    NRF_RADIO->RXADDRESSES  = ( (RADIO_RXADDRESSES_ADDR0_Enabled) << RADIO_RXADDRESSES_ADDR0_Pos);
    NRF_RADIO->SHORTS       = ((1 << RADIO_SHORTS_READY_START_Pos) | (1 << RADIO_SHORTS_END_DISABLE_Pos));
    NRF_RADIO->MODE         = ((RADIO_MODE_MODE_Ble_1Mbit) << RADIO_MODE_MODE_Pos) & RADIO_MODE_MODE_Msk;
    NRF_RADIO->TIFS         = 150;
    NRF_RADIO->INTENSET     = (1 << RADIO_INTENSET_DISABLED_Pos);
    NRF_RADIO->PREFIX0      = 0x0000008e; //access_addr[3]
    NRF_RADIO->BASE0        = 0x89bed600; //access_addr[0:3]
    NRF_RADIO->CRCINIT      = 0x00555555;
    NRF_RADIO->PACKETPTR    = (uint32_t) p_adv_pdu;
    
    NVIC_EnableIRQ(RADIO_IRQn);
}

void m_configure_next_event(void)
{
    m_timeslot_request.request_type              = NRF_RADIO_REQ_TYPE_NORMAL;
    m_timeslot_request.params.normal.hfclk       = NRF_RADIO_HFCLK_CFG_DEFAULT;
    m_timeslot_request.params.normal.priority    = NRF_RADIO_PRIORITY_NORMAL;
    m_timeslot_request.params.normal.distance_us = m_slot_distance;
    m_timeslot_request.params.normal.length_us   = m_slot_length;
}

void m_handle_start(void)
{
    // Configure TX_EN on TIMER EVENT_0
    NRF_PPI->CH[8].TEP    = (uint32_t)(&NRF_RADIO->TASKS_TXEN);
    NRF_PPI->CH[8].EEP    = (uint32_t)(&NRF_RADIO_MULTITIMER->EVENTS_COMPARE[0]);
    NRF_PPI->CHENSET      = (1 << 8);
    
    // Configure and initiate radio
    m_configure_radio();
    NRF_RADIO->TASKS_DISABLE = 1;
}

void m_handle_radio(enum mode_t mode)
{
    switch (mode)
    {
        case ADV_RX_CH37:
            m_set_adv_ch(37);
            NRF_RADIO->TASKS_TXEN = 1;
            break;
        case ADV_RX_CH38:
            m_set_adv_ch(38);
            NRF_RADIO_MULTITIMER->TASKS_CLEAR = 1;
            NRF_RADIO_MULTITIMER->CC[0]       = 400;
            break;
        case ADV_RX_CH39:
            m_set_adv_ch(39);
            NRF_RADIO_MULTITIMER->TASKS_CLEAR = 1;
            NRF_RADIO_MULTITIMER->CC[0]       = 400;
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
    
      // Set state machine
      mode = ADV_INIT;
      mode++;
      break;
    case NRF_RADIO_CALLBACK_SIGNAL_TYPE_RADIO:
      if (NRF_RADIO->EVENTS_DISABLED == 1)
      {
        NRF_RADIO->EVENTS_DISABLED = 0;

        m_handle_radio(mode);

        if (mode == ADV_DONE)
        {
            if (m_keep_running)
            {
                m_configure_next_event();

                signal_callback_return_param.params.request.p_next = &m_timeslot_request;
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

void app_beacon_init(void)
{
    m_slot_distance = 365000;
    m_slot_length   =   5500;
}

void app_beacon_start(void)
{
    m_keep_running = true;
    m_is_running   = true;

    sd_radio_session_open(m_timeslot_callback);
    
    m_timeslot_request.request_type                = NRF_RADIO_REQ_TYPE_EARLIEST;
    m_timeslot_request.params.earliest.hfclk       = NRF_RADIO_HFCLK_CFG_DEFAULT;
    m_timeslot_request.params.earliest.priority    = NRF_RADIO_PRIORITY_NORMAL;
    m_timeslot_request.params.earliest.length_us   = m_slot_length;
    m_timeslot_request.params.earliest.timeout_us  = 1000000;
    sd_radio_request(&m_timeslot_request);
}

void app_beacon_stop(void)
{
    m_keep_running = false;
    while (m_is_running) {} // Need some proper handling of timeout and power saving here.
}

