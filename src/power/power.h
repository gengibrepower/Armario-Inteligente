#pragma once
#include <stddef.h>
#include <stdint.h>

namespace power {

enum class Request : uint8_t { LightSleep, DeepSleep };

// Valida e agenda. Falha, com o motivo em `reason`, durante sessão ou porta aberta.
bool request(Request kind, uint32_t seconds, char* reason, size_t reasonLen);

void begin();
void run();  // Corpo da taskPower.
bool ecoActive();
const char* lastWake();  // Texto só muda ao acordar, com as demais tasks paradas.
void setBootWake(const char* text);

}  // namespace power
