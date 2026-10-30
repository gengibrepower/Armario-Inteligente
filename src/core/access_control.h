#pragma once
#include "storage/settings_model.h"
#include "storage/users_db.h"

enum class AccessDecision : uint8_t { Granted, UnknownCard, Blocked, OutOfHours };

namespace accessctl {

// Admin entra fora do horário: alguém precisa poder atender uma emergência.
AccessDecision decide(const char* uidHex, const Settings& cfg, User* user);
const char* describe(AccessDecision decision);
bool withinHours(const Settings& cfg);
bool insideWindow(const Settings& cfg);  // Janela pura, sem olhar se está sendo aplicada.
void uidToHex(const uint8_t* uid, uint8_t len, char* out, size_t outLen);

}  // namespace accessctl
