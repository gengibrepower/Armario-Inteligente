#pragma once
// Profiler RAII leve: mede com esp_timer_get_time() (resolução de 1 µs) e custa
// duas leituras de contador mais uma seção crítica curta por chamada.
#include <esp_timer.h>
#include <stdint.h>

enum class ProfId : uint8_t {
  NfcRead,
  CardValidate,
  LockActuate,
  LogWrite,
  OledRender,
  JsonSerialize,
  HistoryWrite,
  Count,
};

struct ProfStats {
  uint32_t calls;
  uint32_t lastUs;
  uint32_t minUs;
  uint32_t maxUs;
  uint64_t totalUs;
};

namespace profiler {
void record(ProfId id, uint32_t us);
ProfStats get(ProfId id);
const char* name(ProfId id);
}  // namespace profiler

class ProfileScope {
 public:
  explicit ProfileScope(ProfId id) : id_(id), start_(esp_timer_get_time()) {}
  ~ProfileScope() { profiler::record(id_, static_cast<uint32_t>(esp_timer_get_time() - start_)); }

 private:
  ProfId id_;
  int64_t start_;
};

#define PROFILE_SCOPE(id) ProfileScope profileScope(id)
