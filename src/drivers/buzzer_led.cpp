#include "buzzer_led.h"

#include <Arduino.h>
#include <driver/rtc_io.h>

#include "config.h"
#include "pins.h"

namespace {
// O buzzer é ativo: tem oscilador próprio, então o PWM só recorta o som. É o
// único "volume" possível neste hardware, e o efeito precisa ser ajustado na bancada.
constexpr uint32_t BUZZER_PWM_HZ = 2000;
constexpr uint8_t BUZZER_PWM_BITS = 8;
bool auxOn = false;
}  // namespace

namespace outputs {

void begin() {
  // GPIO 14 emite pulsos durante o boot da ROM: um clique curto ao ligar é esperado.
  rtc_gpio_deinit(GPIO_NUM_14);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  digitalWrite(PIN_LED_GREEN, LOW);
  digitalWrite(PIN_LED_RED, LOW);
  ledcAttach(PIN_BUZZER, BUZZER_PWM_HZ, BUZZER_PWM_BITS);
  ledcWrite(PIN_BUZZER, 0);
#if AUX_ACTUATOR_ENABLED
  pinMode(PIN_AUX_ACTUATOR, OUTPUT);
  digitalWrite(PIN_AUX_ACTUATOR, !AUX_ACTIVE_LEVEL);
#endif
}

void setGreen(bool on) { digitalWrite(PIN_LED_GREEN, on); }

void setRed(bool on) { digitalWrite(PIN_LED_RED, on); }

void setBuzzer(bool on, uint8_t volumePct) {
  const uint32_t maxDuty = (1u << BUZZER_PWM_BITS) - 1;
  ledcWrite(PIN_BUZZER, on ? maxDuty * volumePct / 100 : 0);
}

void setAux(bool on) {
  auxOn = on;
#if AUX_ACTUATOR_ENABLED
  digitalWrite(PIN_AUX_ACTUATOR, on ? AUX_ACTIVE_LEVEL : !AUX_ACTIVE_LEVEL);
#endif
}

bool auxState() { return auxOn; }

void prepareForDeepSleep() {
  ledcWrite(PIN_BUZZER, 0);
  ledcDetach(PIN_BUZZER);
  rtc_gpio_init(GPIO_NUM_14);
  rtc_gpio_set_direction(GPIO_NUM_14, RTC_GPIO_MODE_DISABLED);
  rtc_gpio_pulldown_en(GPIO_NUM_14);
  rtc_gpio_pullup_dis(GPIO_NUM_14);
}

}  // namespace outputs
