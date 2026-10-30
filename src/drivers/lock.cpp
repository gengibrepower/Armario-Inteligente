#include "lock.h"

#include <Arduino.h>

#include "perf/profiler.h"
#include "pins.h"

namespace {
constexpr uint32_t SERVO_HZ = 50;
constexpr uint8_t RESOLUTION_BITS = 14;  // ~1,2 µs por passo em 20 ms: sobra para o SG90.
constexpr uint32_t PERIOD_US = 1000000 / SERVO_HZ;
constexpr uint32_t PULSE_MIN_US = 500;
constexpr uint32_t PULSE_MAX_US = 2400;

uint32_t dutyFor(uint8_t degrees) {
  const uint32_t pulse = PULSE_MIN_US + (PULSE_MAX_US - PULSE_MIN_US) * degrees / 180;
  return (pulse << RESOLUTION_BITS) / PERIOD_US;
}
}  // namespace

namespace lock {

void begin(uint8_t lockedDeg) {
  ledcAttach(PIN_SERVO, SERVO_HZ, RESOLUTION_BITS);
  moveTo(lockedDeg);
}

void moveTo(uint8_t degrees) {
  PROFILE_SCOPE(ProfId::LockActuate);
  ledcWrite(PIN_SERVO, dutyFor(degrees));
}

// Parado, o SG90 mantém a posição por atrito. Cortar o sinal elimina o tremor
// do servo e reduz o consumo em repouso (usado também antes de dormir).
void releaseSignal() { ledcWrite(PIN_SERVO, 0); }

}  // namespace lock
