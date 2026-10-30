#pragma once
#include <stdint.h>

// Sequenciador não bloqueante: em vez de delay(), cada saída é função do tempo.
class Blinker {
 public:
  // count = 0 repete até stop().
  void start(uint32_t nowMs, uint16_t onMs, uint16_t offMs, uint8_t count) {
    onMs_ = onMs;
    offMs_ = offMs;
    count_ = count;
    startMs_ = nowMs;
    running_ = true;
  }

  void stop() { running_ = false; }

  bool update(uint32_t nowMs) {
    if (!running_) return false;
    const uint32_t period = uint32_t(onMs_) + offMs_;
    const uint32_t elapsed = nowMs - startMs_;
    if (count_ != 0 && elapsed >= period * count_) {
      running_ = false;
      return false;
    }
    return elapsed % period < onMs_;
  }

 private:
  uint16_t onMs_ = 0;
  uint16_t offMs_ = 1;
  uint8_t count_ = 0;
  uint32_t startMs_ = 0;
  bool running_ = false;
};

namespace outputs {

void begin();
void setGreen(bool on);
void setRed(bool on);
void setBuzzer(bool on, uint8_t volumePct);
void setAux(bool on);
bool auxState();

// Antes do deep sleep: o GPIO do buzzer flutuaria e poderia polarizar o BC547.
void prepareForDeepSleep();

}  // namespace outputs
