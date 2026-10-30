#include "usage_store.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>

#include "storage/settings.h"
#include "storage/storage.h"
#include "timebase.h"

namespace {
// Sem PSRAM e usada só em boot/consulta: fica estática para não fragmentar o heap.
UsageStats stats;
SemaphoreHandle_t mutex;

void addLocked(const EventRecord& ev) {
  int hour = -1;
  int weekday = -1;
  if (ev.flags & REC_TIME_SYNCED) {
    const time_t t = ev.ts;
    struct tm local;
    localtime_r(&t, &local);
    hour = local.tm_hour;
    weekday = local.tm_wday;
  }
  stats.add(ev, hour, weekday);
}
}  // namespace

namespace usage {

void begin() {
  mutex = xSemaphoreCreateMutex();
  RingFile& ring = storage::events();
  RingFile::Reader reader(ring);
  EventRecord ev;
  const uint32_t cutoff = usage::retentionCutoff();
  for (uint32_t seq = ring.oldest(); seq < ring.head(); seq++) {
    if (reader.read(seq, &ev) && usage::retained(ev, cutoff)) addLocked(ev);
  }
}

uint32_t retentionCutoff() {
  if (!timebase::synced()) return 0;
  uint8_t flags = 0;
  return timebase::now(&flags) - settings::get().eventRetentionDays * 86400UL;
}

bool retained(const EventRecord& ev, uint32_t cutoff) { return !(ev.flags & REC_TIME_SYNCED) || ev.ts >= cutoff; }

void record(const EventRecord& ev) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  addLocked(ev);
  xSemaphoreGive(mutex);
}

void copy(UsageStats* out) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  *out = stats;
  xSemaphoreGive(mutex);
}

}  // namespace usage
