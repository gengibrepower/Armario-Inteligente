// Gerenciamento de energia. Fica numa task própria porque entrar em sono exige
// esperar o flush e o desligamento do rádio, o que não cabe num handler web.
#include "power.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include <atomic>

#include "core/access_control.h"
#include "core/shared_state.h"
#include "drivers/sensors.h"
#include "pins.h"
#include "storage/logger.h"
#include "storage/settings.h"
#include "storage/storage.h"
#include "web/server.h"
#include "web/wifi_mgr.h"

namespace {
constexpr uint32_t ECO_CHECK_MS = 60000;
constexpr uint32_t RESPONSE_GRACE_MS = 800;  // A resposta HTTP precisa sair antes do rádio cair.
constexpr uint32_t MIN_SLEEP_S = 5;
constexpr uint32_t MAX_SLEEP_S = 86400;
constexpr uint16_t ECO_MHZ = 80;  // Mínimo que mantém o Wi-Fi operando.

struct SleepRequest {
  power::Request kind;
  uint32_t seconds;
};

QueueHandle_t requests;
std::atomic<bool> eco{false};
char wakeText[28];

void setWake(const char* text) { strlcpy(wakeText, text, sizeof(wakeText)); }

void prepare(const SleepRequest& r, bool deep) {
  vTaskDelay(pdMS_TO_TICKS(RESPONSE_GRACE_MS));
  LOG_INFO("power", "Entrando em %s por %lu s", deep ? "deep sleep" : "light sleep", (unsigned long)r.seconds);
  shared::sendActuator(ActuatorCmd::Sleep, deep);
  // Registrar antes de dormir é requisito: o flush garante que o log sobreviva.
  storage::flushAndWait(pdMS_TO_TICKS(2000));
  if (!wifimgr::pause(3000)) LOG_WARN("power", "Wi-Fi nao desligou a tempo");
  Serial.flush();
}

void enterLightSleep(const SleepRequest& r) {
  prepare(r, false);

  sensors::suspendDoorInterrupt();
  esp_sleep_enable_timer_wakeup(uint64_t(r.seconds) * 1000000);
  gpio_wakeup_enable(static_cast<gpio_num_t>(PIN_REED_DOOR), GPIO_INTR_HIGH_LEVEL);
  esp_sleep_enable_gpio_wakeup();

  esp_light_sleep_start();  // RAM e tasks preservadas; retoma nesta linha.

  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  gpio_wakeup_disable(static_cast<gpio_num_t>(PIN_REED_DOOR));
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  sensors::resumeDoorInterrupt();
  sensors::resync();  // Porta aberta durante o sono vira intrusão pela FSM.

  const char* text = cause == ESP_SLEEP_WAKEUP_TIMER ? "timer" : cause == ESP_SLEEP_WAKEUP_GPIO ? "reed (porta)" : "outro";
  setWake(text);
  wifimgr::resume();
  LOG_INFO("power", "Acordou do light sleep por %s", text);
}

void enterDeepSleep(const SleepRequest& r) {
  prepare(r, true);

  // Em deep sleep só os periféricos RTC continuam: o pull-up digital some, e o
  // reed sem pull-up acordaria o sistema por ruído.
  sensors::suspendDoorInterrupt();
  rtc_gpio_init(GPIO_NUM_25);
  rtc_gpio_set_direction(GPIO_NUM_25, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en(GPIO_NUM_25);
  rtc_gpio_pulldown_dis(GPIO_NUM_25);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_25, 1);  // Porta aberta = nível alto.
  esp_sleep_enable_timer_wakeup(uint64_t(r.seconds) * 1000000);

  esp_deep_sleep_start();  // Não retorna: ao acordar o firmware reinicia.
}

void applyFrequency() {
  const Settings cfg = settings::get();
  const bool wantEco = cfg.autoEco && !accessctl::insideWindow(cfg) && shared::read().state == LockerState::Idle;
  const uint16_t target = wantEco ? ECO_MHZ : cfg.cpuMhz;
  if (wantEco != eco.load()) LOG_INFO("power", "Modo economico %s", wantEco ? "ligado" : "desligado");
  eco = wantEco;
  if (getCpuFrequencyMhz() != target) {
    setCpuFrequencyMhz(target);
    LOG_INFO("power", "CPU em %u MHz", target);
  }
}
}  // namespace

namespace power {

void begin() {
  requests = xQueueCreate(1, sizeof(SleepRequest));
}

bool request(Request kind, uint32_t seconds, char* reason, size_t reasonLen) {
  const LockerSnapshot s = shared::read();
  if (s.state != LockerState::Idle || s.doorOpen) {
    strlcpy(reason, "armario em uso ou porta aberta", reasonLen);
    return false;
  }
  if (web::otaInProgress()) {
    strlcpy(reason, "atualizacao em andamento", reasonLen);
    return false;
  }
  if (seconds < MIN_SLEEP_S || seconds > MAX_SLEEP_S) {
    strlcpy(reason, "tempo fora da faixa", reasonLen);
    return false;
  }
  SleepRequest r{kind, seconds};
  if (xQueueSend(requests, &r, 0) != pdTRUE) {
    strlcpy(reason, "ja existe pedido pendente", reasonLen);
    return false;
  }
  return true;
}

void run() {
  for (;;) {
    SleepRequest r;
    if (xQueueReceive(requests, &r, pdMS_TO_TICKS(ECO_CHECK_MS)) == pdTRUE) {
      if (r.kind == Request::LightSleep) {
        enterLightSleep(r);
      } else {
        enterDeepSleep(r);
      }
    }
    applyFrequency();
  }
}

bool ecoActive() { return eco.load(); }

const char* lastWake() { return wakeText; }

void setBootWake(const char* text) { setWake(text); }

}  // namespace power
