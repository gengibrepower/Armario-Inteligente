#pragma once
#include <stdint.h>

// Servo da tranca via LEDC. Só a taskActuators chama estas funções.
namespace lock {

void begin(uint8_t lockedDeg);
void moveTo(uint8_t degrees);
void releaseSignal();

}  // namespace lock
