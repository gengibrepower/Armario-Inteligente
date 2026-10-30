#include "usage_stats.h"

#include <string.h>

void UsageStats::reset() { *this = UsageStats(); }

uint32_t UsageStats::averageLoanSec() const { return timedLoans ? totalLoanSec / timedLoans : 0; }

void UsageStats::add(const EventRecord& ev, int hour, int weekday) {
  switch (static_cast<EventType>(ev.type)) {
    case EventType::Withdraw: {
      withdrawals++;
      if (hour >= 0 && hour < 24) perHour[hour]++;
      if (weekday >= 0 && weekday < 7) perWeekday[weekday]++;
      if (ev.userId == USER_NONE) break;

      int index = -1;
      for (uint8_t i = 0; i < rankedUsers; i++) {
        if (ranking[i].userId == ev.userId) index = i;
      }
      if (index < 0 && rankedUsers < USAGE_MAX_RANKED) {
        index = rankedUsers++;
        ranking[index].userId = ev.userId;
        ranking[index].count = 0;
        strncpy(ranking[index].name, ev.userName, sizeof(ranking[index].name) - 1);
        ranking[index].name[sizeof(ranking[index].name) - 1] = '\0';
      }
      if (index < 0) break;
      ranking[index].count++;
      // Uma passada de bolha basta: a contagem só sobe de 1 em 1.
      while (index > 0 && ranking[index].count > ranking[index - 1].count) {
        const UserCount tmp = ranking[index - 1];
        ranking[index - 1] = ranking[index];
        ranking[index] = tmp;
        index--;
      }
      break;
    }
    case EventType::Return:
      returns++;
      if (ev.durationSec > 0) {
        totalLoanSec += ev.durationSec;
        timedLoans++;
      }
      break;
    case EventType::Denied:
      denied++;
      break;
    case EventType::Violation:
    case EventType::Intrusion:
      violations++;
      break;
  }
}
