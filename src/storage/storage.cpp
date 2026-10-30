#include "storage.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <freertos/event_groups.h>

#include "logger.h"
#include "perf/profiler.h"
#include "users_db.h"

namespace {
RingFile logRing("log", {RING_LOG_SEGMENTS, RING_LOG_PER_SEGMENT}, sizeof(LogRecord), LOG_BATCH_RECORDS);
RingFile eventRing("evt", {RING_EVENT_SEGMENTS, RING_EVENT_PER_SEGMENT}, sizeof(EventRecord), 8);
RingFile minuteRing("min", {RING_MINUTE_SEGMENTS, RING_MINUTE_PER_SEGMENT}, sizeof(MinuteSample), 12);
RingFile hourRing("hour", {RING_HOUR_SEGMENTS, RING_HOUR_PER_SEGMENT}, sizeof(HourSample), 2);

QueueHandle_t queue;
EventGroupHandle_t flags;
bool isMounted = false;

constexpr EventBits_t BIT_FLUSH = BIT0;
constexpr EventBits_t BIT_FLUSHED = BIT1;

void loanKey(uint8_t slot, char* key) { snprintf(key, 8, "loan%u", slot); }

void saveLoan(const ActiveLoanMsg& msg) {
  char key[8];
  loanKey(msg.slot, key);
  Preferences prefs;
  if (!prefs.begin("armario", false)) return;
  prefs.putBytes(key, &msg.value, sizeof(msg.value));
  prefs.end();
}

void flushCritical() {
  PROFILE_SCOPE(ProfId::LogWrite);
  logRing.flush();
  eventRing.flush();
}

void flushHistory() {
  PROFILE_SCOPE(ProfId::HistoryWrite);
  minuteRing.flush();
  hourRing.flush();
}

void handle(const StorageMsg& msg) {
  switch (msg.kind) {
    case StorageMsg::Log:
      logRing.append(&msg.log);
      if (logRing.pending() >= LOG_BATCH_RECORDS) flushCritical();
      break;
    case StorageMsg::Event:
      eventRing.append(&msg.event);
      break;
    case StorageMsg::Minute:
      minuteRing.append(&msg.minute);
      break;
    case StorageMsg::Hour:
      hourRing.append(&msg.hour);
      break;
    case StorageMsg::Loan:
      saveLoan(msg.loan);
      break;
    case StorageMsg::ClearLogs:
      logRing.clear();
      break;
  }
}
}  // namespace

namespace storage {

bool begin() {
  queue = xQueueCreate(Q_STORAGE_DEPTH, sizeof(StorageMsg));
  flags = xEventGroupCreate();
  if (!queue || !flags) return false;

  // Formatar só se a montagem falhar: é o caminho de recuperação de uma imagem
  // corrompida, e sem sistema de arquivos o armário ainda precisa operar.
  isMounted = LittleFS.begin(true, "/littlefs", 12);
  const bool rings = logRing.begin(isMounted) && eventRing.begin(isMounted) && minuteRing.begin(isMounted) && hourRing.begin(isMounted);
  return isMounted && rings;
}

bool mounted() { return isMounted; }

bool post(const StorageMsg& msg, TickType_t wait) { return queue && xQueueSend(queue, &msg, wait) == pdTRUE; }

void postEvent(const EventRecord& rec) {
  StorageMsg msg;
  msg.kind = StorageMsg::Event;
  msg.event = rec;
  if (post(msg, pdMS_TO_TICKS(50))) requestFlush();
}

void postLoan(uint8_t slot, const ActiveLoan& loan) {
  StorageMsg msg;
  msg.kind = StorageMsg::Loan;
  msg.loan = {slot, loan};
  post(msg, pdMS_TO_TICKS(50));
}

bool loadLoan(uint8_t slot, ActiveLoan* loan) {
  char key[8];
  loanKey(slot, key);
  Preferences prefs;
  if (!prefs.begin("armario", true)) return false;
  const bool ok = prefs.getBytes(key, loan, sizeof(*loan)) == sizeof(*loan);
  prefs.end();
  return ok;
}

void requestFlush() {
  if (flags) xEventGroupSetBits(flags, BIT_FLUSH);
}

bool flushAndWait(TickType_t timeout) {
  xEventGroupClearBits(flags, BIT_FLUSHED);
  requestFlush();
  return xEventGroupWaitBits(flags, BIT_FLUSHED, pdTRUE, pdTRUE, timeout) & BIT_FLUSHED;
}

RingFile& logs() { return logRing; }
RingFile& events() { return eventRing; }
RingFile& minutes() { return minuteRing; }
RingFile& hours() { return hourRing; }

void runWriter() {
  esp_task_wdt_add(nullptr);
  TickType_t lastLogFlush = xTaskGetTickCount();
  TickType_t lastHistoryFlush = lastLogFlush;

  for (;;) {
    StorageMsg msg;
    // Acorda pelo menos 4×/s para atender pedidos de flush e o watchdog.
    while (xQueueReceive(queue, &msg, pdMS_TO_TICKS(250)) == pdTRUE) {
      handle(msg);
      if (uxQueueMessagesWaiting(queue) == 0) break;
    }

    const TickType_t now = xTaskGetTickCount();
    const bool requested = xEventGroupClearBits(flags, BIT_FLUSH) & BIT_FLUSH;

    if (requested) {
      // Esvazia a fila antes do flush: quem pediu quer ver o próprio registro.
      while (xQueueReceive(queue, &msg, 0) == pdTRUE) handle(msg);
    }
    if (requested || now - lastLogFlush >= pdMS_TO_TICKS(LOG_FLUSH_INTERVAL_MS)) {
      flushCritical();
      lastLogFlush = now;
    }
    if (requested || now - lastHistoryFlush >= pdMS_TO_TICKS(HISTORY_FLUSH_INTERVAL_MS)) {
      flushHistory();
      lastHistoryFlush = now;
    }
    if (requested) xEventGroupSetBits(flags, BIT_FLUSHED);

    users::saveIfDirty();
    esp_task_wdt_reset();
  }
}

}  // namespace storage
