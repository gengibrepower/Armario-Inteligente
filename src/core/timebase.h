#pragma once
#include <esp_timer.h>
#include <stdint.h>
#include <time.h>

namespace timebase {

// Antes disso o relógio do sistema ainda não recebeu NTP (2024-01-01 UTC).
constexpr time_t VALID_EPOCH = 1704067200;

inline bool synced() { return time(nullptr) >= VALID_EPOCH; }

// Sem NTP o registro carrega uptime em s e não leva a flag REC_TIME_SYNCED.
inline uint32_t now(uint8_t* flags) {
  const time_t t = time(nullptr);
  if (t >= VALID_EPOCH) {
    *flags |= 0x01;
    return static_cast<uint32_t>(t);
  }
  *flags &= ~0x01;
  return static_cast<uint32_t>(esp_timer_get_time() / 1000000);
}

}  // namespace timebase
