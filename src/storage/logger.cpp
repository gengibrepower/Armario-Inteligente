#include "logger.h"

#include <Arduino.h>

#include <atomic>

#include "core/timebase.h"
#include "storage.h"

namespace {
std::atomic<uint32_t> droppedCount{0};
const char* const LEVEL_NAMES[] = {"ERROR", "WARN", "INFO"};
}  // namespace

namespace logger {

void write(LogLevel level, const char* module, const char* fmt, ...) {
  StorageMsg msg;
  msg.kind = StorageMsg::Log;
  LogRecord& rec = msg.log;
  memset(&rec, 0, sizeof(rec));
  rec.level = static_cast<uint8_t>(level);
  rec.ts = timebase::now(&rec.flags);
  strncpy(rec.module, module, sizeof(rec.module) - 1);

  va_list args;
  va_start(args, fmt);
  vsnprintf(rec.msg, sizeof(rec.msg), fmt, args);
  va_end(args);

  Serial.printf("[%s] %s: %s\n", LEVEL_NAMES[rec.level], rec.module, rec.msg);

  // Timeout zero: com a fila cheia é melhor perder um log (e contar) do que
  // atrasar a máquina de estados. ERROR espera um pouco por ser o que importa.
  const TickType_t wait = level == LogLevel::Error ? pdMS_TO_TICKS(20) : 0;
  if (!storage::post(msg, wait)) {
    droppedCount++;
    return;
  }
  if (level == LogLevel::Error) storage::requestFlush();
}

uint32_t dropped() { return droppedCount.load(); }

}  // namespace logger
