#include "app_ibeacon.h"
#include <stdio.h>
#include <string.h>

#include "nrf_sdm.h"
#include "nrf_soc.h"
#include "nrf51.h"
#include "nrf51_bitfields.h"

static nrf_radio_request_t             m_timeslot_request;
static bool volatile                   m_keep_running;
static uint32_t                        m_slot_distance;
static uint32_t                        m_slot_length;

void app_ibeacon_sd_evt_signal_handler(uint32_t event)
{
    switch (event)
    {
        case NRF_EVT_RADIO_SESSION_IDLE:
            break;
        case NRF_EVT_RADIO_SESSION_CLOSED:
            break;
        case NRF_EVT_RADIO_BLOCKED:
        case NRF_EVT_RADIO_CANCELED:
        default:
            break;
    }
}

static uint32_t m_get_adv_packet(void)
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

    return (uint32_t) &adv_pdu[0];
}

static void m_configure_radio(void)
{
  NRF_RADIO->POWER        = 1;
  NRF_RADIO->PCNF0        =   (((1UL) << RADIO_PCNF0_S0LEN_Pos) & RADIO_PCNF0_S0LEN_Msk)
                            | (((2UL) << RADIO_PCNF0_S1LEN_Pos) & RADIO_PCNF0_S1LEN_Msk)
                            | (((6UL) << RADIO_PCNF0_LFLEN_Pos) & RADIO_PCNF0_LFLEN_Msk);
  NRF_RADIO->PCNF1        =   (((RADIO_PCNF1_ENDIAN_Little) << RADIO_PCNF1_ENDIAN_Pos)  & RADIO_PCNF1_ENDIAN_Msk)
                            | (((3UL)                       << RADIO_PCNF1_BALEN_Pos)   & RADIO_PCNF1_BALEN_Msk)
                            | (((0UL)                       << RADIO_PCNF1_STATLEN_Pos) & RADIO_PCNF1_STATLEN_Msk)
                            | ((((uint32_t) 37)  << RADIO_PCNF1_MAXLEN_Pos)      & RADIO_PCNF1_MAXLEN_Msk)
                            | ((RADIO_PCNF1_WHITEEN_Enabled << RADIO_PCNF1_WHITEEN_Pos) & RADIO_PCNF1_WHITEEN_Msk);
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
  NRF_RADIO->FREQUENCY    = 2;
  NRF_RADIO->DATAWHITEIV  = 0x65; // why?
  NRF_RADIO->PACKETPTR    = m_get_adv_packet();

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

static nrf_radio_signal_callback_return_param_t * m_timeslot_callback(uint8_t signal_type)
{
  static nrf_radio_signal_callback_return_param_t signal_callback_return_param;
  static uint8_t mode = 0;

  signal_callback_return_param.p_next_request  = NULL;
  signal_callback_return_param.callback_action = NRF_RADIO_SIGNAL_CALLBACK_ACTION_NONE;

  if (signal_type == NRF_RADIO_CALLBACK_SIGNAL_TYPE_START)
  {
    // Set state machine
    mode = 0;

    // setup radio
    m_configure_radio();

    // Start RADIO on timer
    NRF_PPI->CH[8].TEP    = (uint32_t)(&NRF_RADIO->TASKS_TXEN);
    NRF_PPI->CH[8].EEP    = (uint32_t)(&NRF_RADIO_MULTITIMER->EVENTS_COMPARE[0]);
    NRF_PPI->CHENSET      = (1 << 8);

    NRF_RADIO_MULTITIMER->CC[0]             = 200;
    NRF_RADIO_MULTITIMER->INTENSET          = TIMER_INTENSET_COMPARE0_Msk;
    NVIC_EnableIRQ(NRF_RADIO_MULTITIMER_IRQn);
  }
  else if (signal_type == NRF_RADIO_CALLBACK_SIGNAL_TYPE_MULTITIMER)
  {
    if ( NRF_RADIO_MULTITIMER->EVENTS_COMPARE[0] == 1 )
    { // Radio start, do nothing
      // TODO: Why two calls here?
      NRF_RADIO_MULTITIMER->EVENTS_COMPARE[0] = 0;
      NRF_RADIO->SHORTS       = ((1 << RADIO_SHORTS_READY_START_Pos)
                               | (1 << RADIO_SHORTS_END_DISABLE_Pos)
                               | (1 << RADIO_SHORTS_DISABLED_RXEN_Pos));
    }
    else if ( NRF_RADIO_MULTITIMER->EVENTS_COMPARE[1] == 1 )
    { // hcto, end event
      NRF_RADIO_MULTITIMER->EVENTS_COMPARE[1] = 0;
      if (mode == 1 || mode == 4) // rx hcto on ch 37, 38
      { 
        mode++;
        NRF_RADIO->SHORTS        = ((1 << RADIO_SHORTS_READY_START_Pos) | (1 << RADIO_SHORTS_END_DISABLE_Pos));
        NRF_RADIO->TASKS_DISABLE = 1;
        while (NRF_RADIO->EVENTS_DISABLED == 0) {}
        NRF_RADIO->SHORTS       = ((1 << RADIO_SHORTS_READY_START_Pos)
                                 | (1 << RADIO_SHORTS_END_DISABLE_Pos)
                                 | (1 << RADIO_SHORTS_DISABLED_RXEN_Pos));
      }
      else if (mode == 7) // rx hcto on ch 39
      { 
        mode++;
        NRF_RADIO->SHORTS        = ((1 << RADIO_SHORTS_READY_START_Pos) | (1 << RADIO_SHORTS_END_DISABLE_Pos));
        NRF_RADIO->TASKS_DISABLE = 1;
      }
      else
      {
        signal_callback_return_param.callback_action = NRF_RADIO_SIGNAL_CALLBACK_ACTION_END;
      }
    }
    else
    {
      while (true) {}
    }
  }
  else if (signal_type == NRF_RADIO_CALLBACK_SIGNAL_TYPE_RADIO)
  {
    if (NRF_RADIO->EVENTS_DISABLED == 1)
    {
      NRF_RADIO->EVENTS_DISABLED = 0;
      if (mode == 0 || mode == 3 || mode == 6) // rx on ch 37, 38, 39
      { 
        mode++;
        // Set up HCTO
        NRF_RADIO_MULTITIMER->INTENSET          = TIMER_INTENSET_COMPARE1_Msk;
        NRF_RADIO_MULTITIMER->CC[1]             = 800;
        NRF_RADIO_MULTITIMER->TASKS_CLEAR       = 1;
      }
      else if (mode == 2 || mode == 5) // tx on ch 38, 38, 39
      { 
        NRF_RADIO_MULTITIMER->CC[0]             = NRF_RADIO_MULTITIMER->CC[1] + 600;
        mode++;
      }
      else if (mode == 8) // done
      { 
        mode++;
        if (! m_keep_running)
        {
          signal_callback_return_param.callback_action = NRF_RADIO_SIGNAL_CALLBACK_ACTION_END;
        }
        else
        {
          m_timeslot_request.request_type              = NRF_RADIO_REQ_TYPE_NORMAL;
          m_timeslot_request.params.normal.hfclk       = NRF_RADIO_HFCLK_CFG_DEFAULT;
          m_timeslot_request.params.normal.priority    = NRF_RADIO_PRIORITY_NORMAL;
          m_timeslot_request.params.normal.distance_us = m_slot_distance;
          m_timeslot_request.params.normal.length_us   = m_slot_length;

          signal_callback_return_param.p_next_request  = &m_timeslot_request;
          signal_callback_return_param.callback_action = NRF_RADIO_SIGNAL_CALLBACK_ACTION_REQUEST_AND_END;
        }
      }
      else
      {
        while (true) {}
        NRF_RADIO_MULTITIMER->INTENCLR          = TIMER_INTENSET_COMPARE1_Msk;
        signal_callback_return_param.callback_action = NRF_RADIO_SIGNAL_CALLBACK_ACTION_END;
      }
    }
  }
  else
  {
    while (true) {}
  }

  return ( &signal_callback_return_param );
}

void app_ibeacon_init(void)
{
    m_slot_distance = 36500;
    m_slot_length   =  5500;
}

void app_ibeacon_start(void)
{
    m_keep_running = true;

    sd_radio_session_open(m_timeslot_callback);
    
    m_timeslot_request.request_type                = NRF_RADIO_REQ_TYPE_EARLIEST;
    m_timeslot_request.params.earliest.hfclk       = NRF_RADIO_HFCLK_CFG_DEFAULT;
    m_timeslot_request.params.earliest.priority    = NRF_RADIO_PRIORITY_NORMAL;
    m_timeslot_request.params.earliest.length_us   = m_slot_length;
    m_timeslot_request.params.earliest.timeout_us  = 1000000;
    sd_radio_request(&m_timeslot_request);
}
