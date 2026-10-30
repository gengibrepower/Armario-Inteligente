#include "cpu_usage.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

#include "config.h"
#include "core/shared_state.h"

namespace {
struct PreviousRun {
  TaskHandle_t handle;
  uint32_t runtime;
};

TaskStatus_t raw[MAX_TRACKED_TASKS];
PreviousRun previous[MAX_TRACKED_TASKS];
size_t previousCount = 0;
uint32_t previousTotal = 0;

cpuusage::TaskInfo published[MAX_TRACKED_TASKS];
size_t publishedCount = 0;
SemaphoreHandle_t mutex;

uint32_t runtimeBefore(TaskHandle_t handle, uint32_t fallback) {
  for (size_t i = 0; i < previousCount; i++) {
    if (previous[i].handle == handle) return previous[i].runtime;
  }
  return fallback;
}

uint8_t percentOf(uint32_t part, uint32_t whole) {
  if (whole == 0) return 0;
  const uint64_t pct = uint64_t(part) * 100 / whole;
  return pct > 100 ? 100 : pct;
}
}  // namespace

namespace cpuusage {

void begin() { mutex = xSemaphoreCreateMutex(); }

void sample() {
  uint32_t total = 0;
  const size_t count = uxTaskGetSystemState(raw, MAX_TRACKED_TASKS, &total);
  if (count == 0) return;  // Só ocorre se houver mais tasks que MAX_TRACKED_TASKS.

  // Os contadores são de 32 bits e voltam a zero a cada ~71 min; a subtração sem
  // sinal continua correta por trabalharmos só com diferenças.
  const uint32_t elapsed = total - previousTotal;

  xSemaphoreTake(mutex, portMAX_DELAY);
  for (size_t i = 0; i < count; i++) {
    const TaskStatus_t& t = raw[i];
    const uint32_t delta = t.ulRunTimeCounter - runtimeBefore(t.xHandle, t.ulRunTimeCounter);
    const uint8_t pct = percentOf(delta, elapsed);

    TaskInfo& info = published[i];
    strlcpy(info.name, t.pcTaskName, sizeof(info.name));
    info.core = t.xCoreID == tskNO_AFFINITY ? -1 : t.xCoreID;
    info.priority = t.uxCurrentPriority;
    info.state = t.eCurrentState;
    info.stackFree = t.usStackHighWaterMark;  // No ESP-IDF a unidade é byte.
    info.cpuPct = pct;

    if (t.xHandle == xTaskGetIdleTaskHandleForCore(0)) shared::cpuPct0 = 100 - pct;
    if (t.xHandle == xTaskGetIdleTaskHandleForCore(1)) shared::cpuPct1 = 100 - pct;

    previous[i] = {t.xHandle, t.ulRunTimeCounter};
  }
  publishedCount = count;
  previousCount = count;
  previousTotal = total;
  xSemaphoreGive(mutex);
}

size_t tasks(TaskInfo* out, size_t capacity) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  const size_t n = publishedCount < capacity ? publishedCount : capacity;
  memcpy(out, published, n * sizeof(TaskInfo));
  xSemaphoreGive(mutex);
  return n;
}

}  // namespace cpuusage
