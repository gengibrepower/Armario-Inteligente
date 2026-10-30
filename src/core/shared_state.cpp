#include "shared_state.h"

#include <string.h>

namespace {
LockerSnapshot snapshot;
SemaphoreHandle_t mutex;
}  // namespace

namespace shared {

QueueHandle_t uidQueue;
QueueHandle_t sensorQueue;
QueueHandle_t commandQueue;
QueueHandle_t actuatorQueue;
QueueSetHandle_t accessSet;
std::atomic<uint32_t> nfcReads{0};
std::atomic<uint32_t> nfcFailures{0};
std::atomic<uint32_t> stateVersion{0};
std::atomic<uint8_t> cpuPct0{0};
std::atomic<uint8_t> cpuPct1{0};
std::atomic<int8_t> wifiRssi{0};

bool begin() {
  mutex = xSemaphoreCreateMutex();
  uidQueue = xQueueCreate(Q_UID_DEPTH, sizeof(UidMsg));
  sensorQueue = xQueueCreate(Q_SENSOR_DEPTH, sizeof(SensorMsg));
  commandQueue = xQueueCreate(Q_COMMAND_DEPTH, sizeof(AccessCommand));
  actuatorQueue = xQueueCreate(Q_ACTUATOR_DEPTH, sizeof(ActuatorCmd));
  // O conjunto deixa a taskAccess dormir até qualquer fila ter dado, com uma só
  // espera, em vez de varrer as três em polling.
  accessSet = xQueueCreateSet(Q_UID_DEPTH + Q_SENSOR_DEPTH + Q_COMMAND_DEPTH);
  if (!mutex || !uidQueue || !sensorQueue || !commandQueue || !actuatorQueue || !accessSet) return false;
  xQueueAddToSet(uidQueue, accessSet);
  xQueueAddToSet(sensorQueue, accessSet);
  xQueueAddToSet(commandQueue, accessSet);
  memset(&snapshot, 0, sizeof(snapshot));
  snapshot.locked = true;
  return true;
}

LockerSnapshot read() {
  lock();
  LockerSnapshot copy = snapshot;
  unlock();
  return copy;
}

void lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
void unlock() { xSemaphoreGive(mutex); }
LockerSnapshot& raw() { return snapshot; }

bool sendActuator(ActuatorCmd::Kind kind, uint8_t arg) {
  ActuatorCmd cmd{kind, arg};
  return xQueueSend(actuatorQueue, &cmd, 0) == pdTRUE;
}

bool sendCommand(AccessCommand::Kind kind, uint16_t userId) {
  AccessCommand cmd{kind, userId};
  return xQueueSend(commandQueue, &cmd, 0) == pdTRUE;
}

}  // namespace shared
