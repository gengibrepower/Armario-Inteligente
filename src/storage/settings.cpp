#include "settings.h"

#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>

#include "logger.h"

namespace {
constexpr const char* NVS_NAMESPACE = "armario";
constexpr const char* NVS_KEY = "settings";

Settings current;
SemaphoreHandle_t mutex;
std::atomic<uint32_t> gen{1};

bool persist(const Settings& s) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) return false;
  const bool ok = prefs.putBytes(NVS_KEY, &s, sizeof(s)) == sizeof(s);
  prefs.end();
  return ok;
}
}  // namespace

namespace settings {

void begin() {
  mutex = xSemaphoreCreateMutex();
  settingsDefaults(current);

  Preferences prefs;
  bool loaded = false;
  // readOnly falha se o namespace ainda não existe, caso normal na primeira carga.
  if (prefs.begin(NVS_NAMESPACE, true)) {
    Settings stored;
    if (prefs.getBytesLength(NVS_KEY) == sizeof(Settings) && prefs.getBytes(NVS_KEY, &stored, sizeof(stored)) == sizeof(stored)) {
      const bool reset = settingsSanitize(stored);
      if (!reset) {
        current = stored;
        loaded = true;
      }
    }
    prefs.end();
  }
  if (!loaded) {
    LOG_INFO("config", "Usando configuracoes padrao");
    persist(current);
  }
}

Settings get() {
  xSemaphoreTake(mutex, portMAX_DELAY);
  Settings copy = current;
  xSemaphoreGive(mutex);
  return copy;
}

bool update(JsonObjectConst json, char* err, size_t errLen) {
  Settings next = get();
  if (!settingsApplyJson(json, next, err, errLen)) return false;
  if (!persist(next)) {
    snprintf(err, errLen, "falha ao gravar NVS");
    return false;
  }
  xSemaphoreTake(mutex, portMAX_DELAY);
  current = next;
  xSemaphoreGive(mutex);
  gen++;
  LOG_INFO("config", "Configuracoes atualizadas");
  return true;
}

uint32_t generation() { return gen.load(); }

}  // namespace settings
