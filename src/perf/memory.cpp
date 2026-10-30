#include "memory.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>

#include "storage/storage.h"

namespace {
constexpr uint32_t FS_REFRESH_EVERY = 15;  // usedBytes percorre o FS: não a cada segundo.

perfmem::Snapshot snapshot;
SemaphoreHandle_t mutex;
uint32_t samples = 0;
}  // namespace

namespace perfmem {

void begin() {
  mutex = xSemaphoreCreateMutex();
  memset(&snapshot, 0, sizeof(snapshot));
  snapshot.flashSize = ESP.getFlashChipSize();
  snapshot.appUsed = ESP.getSketchSize();
  snapshot.appMax = esp_ota_get_running_partition()->size;
}

void sample() {
  const uint32_t free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  const uint32_t total = heap_caps_get_total_size(MALLOC_CAP_8BIT);
  const uint32_t minimum = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);

  uint32_t fsTotal = snapshot.fsTotal;
  uint32_t fsUsed = snapshot.fsUsed;
  if (storage::mounted() && samples++ % FS_REFRESH_EVERY == 0) {
    fsTotal = LittleFS.totalBytes();
    fsUsed = LittleFS.usedBytes();
  }

  xSemaphoreTake(mutex, portMAX_DELAY);
  snapshot.heapFree = free;
  snapshot.heapTotal = total;
  snapshot.heapMin = minimum;
  snapshot.heapLargest = largest;
  // Fragmentação: quanto do livre não está no maior bloco contínuo.
  snapshot.fragmentationPct = free ? 100 - uint64_t(largest) * 100 / free : 0;
  snapshot.fsTotal = fsTotal;
  snapshot.fsUsed = fsUsed;
  xSemaphoreGive(mutex);
}

Snapshot read() {
  xSemaphoreTake(mutex, portMAX_DELAY);
  const Snapshot copy = snapshot;
  xSemaphoreGive(mutex);
  return copy;
}

}  // namespace perfmem
