#include <Arduino.h>
#include <esp_task_wdt.h>

#include "core/shared_state.h"
#include "drivers/buzzer_led.h"
#include "drivers/lock.h"
#include "storage/settings.h"
#include "tasks.h"

namespace {
constexpr TickType_t PERIOD = pdMS_TO_TICKS(10);  // Resolução dos padrões de bipe.
constexpr uint32_t SERVO_SETTLE_MS = 600;         // Curso completo do SG90 com folga.
constexpr uint32_t TEST_SERVO_HOLD_MS = 1500;

Blinker buzzer;
Blinker green;
Blinker red;
Settings cfg;
uint32_t seenGen = 0;
bool servoSignalOn = false;
uint32_t servoOffAt = 0;
uint32_t testServoReturnAt = 0;
bool alarmActive = false;

// Multiplicador do padrão de bipe configurado: 0 curto, 1 normal, 2 longo.
uint16_t scaled(uint16_t ms) {
  static const uint8_t PERCENT[] = {60, 100, 180};
  return ms * PERCENT[cfg.beepPattern] / 100;
}

void moveServo(uint8_t degrees, uint32_t now) {
  lock::moveTo(degrees);
  servoSignalOn = true;
  servoOffAt = now + SERVO_SETTLE_MS;
}

void applyFeedback(FeedbackKind kind, uint32_t now) {
  // Bipe de cartão não pode substituir o padrão contínuo do alarme.
  if (alarmActive && kind != FeedbackKind::AlarmOff) return;
  switch (kind) {
    case FeedbackKind::Granted:
      green.start(now, 1500, 1, 1);
      buzzer.start(now, scaled(150), 1, 1);
      break;
    case FeedbackKind::Denied:
      red.start(now, 1200, 1, 1);
      buzzer.start(now, scaled(90), scaled(110), 2);  // 2 bipes curtos
      break;
    case FeedbackKind::EnrollDone:
      green.start(now, 200, 100, 3);
      buzzer.start(now, scaled(80), scaled(80), 2);
      break;
    case FeedbackKind::AlarmOn:
      alarmActive = true;
      red.start(now, 250, 250, 0);
      buzzer.start(now, 300, 300, 0);
      break;
    case FeedbackKind::AlarmOff:
      alarmActive = false;
      red.stop();
      buzzer.stop();
      break;
  }
}

void handle(const ActuatorCmd& cmd, uint32_t now) {
  switch (cmd.kind) {
    case ActuatorCmd::Lock:
      moveServo(cfg.servoLockedDeg, now);
      break;
    case ActuatorCmd::Unlock:
      moveServo(cfg.servoUnlockedDeg, now);
      break;
    case ActuatorCmd::Feedback:
      applyFeedback(static_cast<FeedbackKind>(cmd.arg), now);
      break;
    case ActuatorCmd::Aux:
      outputs::setAux(cmd.arg);
      break;
    case ActuatorCmd::TestServo:
      moveServo(cfg.servoUnlockedDeg, now);
      testServoReturnAt = now + TEST_SERVO_HOLD_MS;
      break;
    case ActuatorCmd::TestBuzzer:
      if (!alarmActive) buzzer.start(now, 400, 1, 1);
      break;
    case ActuatorCmd::TestGreen:
      green.start(now, 1000, 1, 1);
      break;
    case ActuatorCmd::TestRed:
      if (!alarmActive) red.start(now, 1000, 1, 1);
      break;
    case ActuatorCmd::Sleep:
      lock::releaseSignal();
      servoSignalOn = false;
      outputs::setGreen(false);
      outputs::setRed(false);
      outputs::setAux(false);
      buzzer.stop();
      if (cmd.arg) outputs::prepareForDeepSleep();
      break;
  }
}
}  // namespace

void taskActuators(void*) {
  tasks::watchThisTask();
  cfg = settings::get();
  seenGen = settings::generation();
  outputs::begin();
  lock::begin(cfg.servoLockedDeg);
  servoSignalOn = true;
  servoOffAt = millis() + SERVO_SETTLE_MS;

  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    const uint32_t now = millis();

    if (settings::generation() != seenGen) {
      seenGen = settings::generation();
      cfg = settings::get();
    }

    ActuatorCmd cmd;
    while (xQueueReceive(shared::actuatorQueue, &cmd, 0) == pdTRUE) handle(cmd, now);

    if (testServoReturnAt != 0 && static_cast<int32_t>(now - testServoReturnAt) >= 0) {
      testServoReturnAt = 0;
      moveServo(cfg.servoLockedDeg, now);
    }
    if (servoSignalOn && static_cast<int32_t>(now - servoOffAt) >= 0) {
      lock::releaseSignal();
      servoSignalOn = false;
    }

    outputs::setGreen(green.update(now));
    outputs::setRed(red.update(now));
    outputs::setBuzzer(buzzer.update(now), cfg.beepVolume);

    esp_task_wdt_reset();
    xTaskDelayUntil(&wake, PERIOD);
  }
}
