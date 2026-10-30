#include <Arduino.h>
#include <esp_ota_ops.h>

#include "storage/logger.h"
#include "tasks.h"
#include "web/server.h"
#include "web/wifi_mgr.h"

namespace {
constexpr uint32_t HEALTHY_AFTER_MS = 60000;

// Sobreviver 60 s com rede e tasks de pé confirma a imagem. Se ela travar antes,
// o watchdog reinicia e o bootloader volta à versão anterior.
void markFirmwareHealthy() {
  static bool done = false;
  if (done || millis() < HEALTHY_AFTER_MS) return;
  done = true;
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
    LOG_INFO("ota", "Nova versao confirmada");
  }
}
}  // namespace

void taskWeb(void*) {
  wifimgr::begin();
  web::begin();

  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    wifimgr::loop();
    web::loop();
    markFirmwareHealthy();
    xTaskDelayUntil(&wake, pdMS_TO_TICKS(100));
  }
}
