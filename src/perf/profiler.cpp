#include "profiler.h"

#include <freertos/FreeRTOS.h>

namespace {
ProfStats stats[static_cast<size_t>(ProfId::Count)];
// Spinlock e não mutex: a seção são poucas instruções e é chamada dos dois cores.
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
const char* const NAMES[] = {
    "nfc_read", "card_validate", "lock_actuate", "log_write", "oled_render", "json_serialize", "history_write",
};
}  // namespace

namespace profiler {

void record(ProfId id, uint32_t us) {
  ProfStats& s = stats[static_cast<size_t>(id)];
  portENTER_CRITICAL(&lock);
  if (s.calls == 0 || us < s.minUs) s.minUs = us;
  if (us > s.maxUs) s.maxUs = us;
  s.lastUs = us;
  s.totalUs += us;
  s.calls++;
  portEXIT_CRITICAL(&lock);
}

ProfStats get(ProfId id) {
  portENTER_CRITICAL(&lock);
  ProfStats copy = stats[static_cast<size_t>(id)];
  portEXIT_CRITICAL(&lock);
  return copy;
}

const char* name(ProfId id) { return NAMES[static_cast<size_t>(id)]; }

}  // namespace profiler
