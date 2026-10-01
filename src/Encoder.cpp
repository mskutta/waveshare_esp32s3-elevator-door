#include <Arduino.h>
#include "Encoder.h"
#include "Pins.h"
#include <driver/pcnt.h>
#include <soc/pcnt_struct.h>
#include <esp_intr_alloc.h>

namespace {
constexpr int kLimit=30000;
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
volatile int64_t accumulated=0;
int64_t origin=0;

// Own the raw ISR so a task snapshot can reconcile a pending rollover before
// reading the hardware register. No driver ISR can clear the evidence first.
void IRAM_ATTR reconcile() {
  if(PCNT.int_st.val & 1u) {
    uint32_t status=PCNT.status_unit[0].val;
    if(status & PCNT_EVT_H_LIM) accumulated+=kLimit;
    if(status & PCNT_EVT_L_LIM) accumulated-=kLimit;
    PCNT.int_clr.val=1u;
  }
}
void IRAM_ATTR counterISR(void *) { portENTER_CRITICAL_ISR(&mux); reconcile(); portEXIT_CRITICAL_ISR(&mux); }
int64_t snapshot() {
  int16_t raw;
  do {
    reconcile();
    raw=static_cast<int16_t>(PCNT.cnt_unit[0].pulse_cnt_un);
    // A rollover between reconcile() and the count read invalidates the read.
  } while(PCNT.int_st.val & 1u);
  return accumulated+raw;
}
}
bool encoderBegin() {
  pinMode(Pins::encoderA,INPUT); pinMode(Pins::encoderB,INPUT);
  pcnt_config_t c{};
  c.pulse_gpio_num=Pins::encoderA; c.ctrl_gpio_num=Pins::encoderB;
  c.lctrl_mode=PCNT_MODE_KEEP; c.hctrl_mode=PCNT_MODE_REVERSE;
  c.pos_mode=PCNT_COUNT_INC; c.neg_mode=PCNT_COUNT_DIS;
  c.counter_h_lim=kLimit; c.counter_l_lim=-kLimit;
  c.unit=PCNT_UNIT_0; c.channel=PCNT_CHANNEL_0;
  if(pcnt_unit_config(&c)!=ESP_OK || pcnt_counter_pause(PCNT_UNIT_0)!=ESP_OK ||
     pcnt_counter_clear(PCNT_UNIT_0)!=ESP_OK || pcnt_set_filter_value(PCNT_UNIT_0,80)!=ESP_OK ||
     pcnt_filter_enable(PCNT_UNIT_0)!=ESP_OK || pcnt_event_enable(PCNT_UNIT_0,PCNT_EVT_H_LIM)!=ESP_OK ||
     pcnt_event_enable(PCNT_UNIT_0,PCNT_EVT_L_LIM)!=ESP_OK ||
     pcnt_isr_register(counterISR,nullptr,ESP_INTR_FLAG_IRAM,nullptr)!=ESP_OK ||
     pcnt_intr_enable(PCNT_UNIT_0)!=ESP_OK) return false;
  return pcnt_counter_resume(PCNT_UNIT_0)==ESP_OK;
}
int64_t encoderCount() { portENTER_CRITICAL(&mux); int64_t n=snapshot()-origin; portEXIT_CRITICAL(&mux); return n; }
void encoderZero() {
  // Logical zero leaves hardware running: pulses are not discarded by pause/clear.
  portENTER_CRITICAL(&mux); origin=snapshot(); portEXIT_CRITICAL(&mux);
}
