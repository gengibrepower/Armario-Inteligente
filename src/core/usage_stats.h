#pragma once
// Estatísticas do Dashboard, mantidas de forma incremental. Assim a página não
// precisa varrer a flash a cada acesso: reconstrói-se uma vez no boot e cada
// evento novo custa O(1).
#include <stdint.h>

#include "types.h"

constexpr uint8_t USAGE_MAX_RANKED = 16;

struct UserCount {
  uint16_t userId;
  uint16_t count;
  char name[24];
};

class UsageStats {
 public:
  void reset();
  // hour/weekday em hora local; negativos quando o evento não tem hora confiável.
  void add(const EventRecord& ev, int hour, int weekday);

  uint32_t withdrawals = 0;
  uint32_t returns = 0;
  uint32_t denied = 0;
  uint32_t violations = 0;
  uint32_t perHour[24] = {};
  uint32_t perWeekday[7] = {};
  uint64_t totalLoanSec = 0;
  uint32_t timedLoans = 0;
  UserCount ranking[USAGE_MAX_RANKED] = {};
  uint8_t rankedUsers = 0;

  uint32_t averageLoanSec() const;
};
