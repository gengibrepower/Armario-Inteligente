#include "access_control.h"

#include <stdio.h>
#include <time.h>

#include "perf/profiler.h"
#include "timebase.h"

namespace accessctl {

bool insideWindow(const Settings& cfg) {
  // Sem hora confiável não se bloqueia ninguém: o armário não pode prender os
  // notebooks só porque o NTP está fora do ar.
  if (!timebase::synced()) return true;
  const time_t now = time(nullptr);
  struct tm local;
  localtime_r(&now, &local);
  return local.tm_hour >= cfg.openHour && local.tm_hour < cfg.closeHour;
}

bool withinHours(const Settings& cfg) { return !cfg.hoursEnforced || insideWindow(cfg); }

AccessDecision decide(const char* uidHex, const Settings& cfg, User* user) {
  PROFILE_SCOPE(ProfId::CardValidate);
  if (!users::findByUid(uidHex, user)) return AccessDecision::UnknownCard;
  if (!user->active) return AccessDecision::Blocked;
  if (!user->admin && !withinHours(cfg)) return AccessDecision::OutOfHours;
  return AccessDecision::Granted;
}

const char* describe(AccessDecision decision) {
  switch (decision) {
    case AccessDecision::Granted:
      return "autorizado";
    case AccessDecision::UnknownCard:
      return "cartao desconhecido";
    case AccessDecision::Blocked:
      return "usuario bloqueado";
    case AccessDecision::OutOfHours:
      return "fora do horario";
  }
  return "";
}

void uidToHex(const uint8_t* uid, uint8_t len, char* out, size_t outLen) {
  size_t pos = 0;
  for (uint8_t i = 0; i < len && pos + 3 <= outLen; i++) pos += snprintf(out + pos, outLen - pos, "%02X", uid[i]);
  out[pos] = '\0';
}

}  // namespace accessctl
