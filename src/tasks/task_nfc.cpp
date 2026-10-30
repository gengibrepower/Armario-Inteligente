#include <Arduino.h>
#include <esp_task_wdt.h>

#include "core/shared_state.h"
#include "drivers/nfc.h"
#include "storage/logger.h"
#include "storage/settings.h"
#include "tasks.h"

namespace {
constexpr uint32_t RETRY_INIT_MS = 5000;
}

void taskNFC(void*) {
  tasks::watchThisTask();

  if (nfc::begin()) {
    LOG_INFO("nfc", "PN532 pronto, firmware %08lX", (unsigned long)nfc::firmwareVersion());
  } else {
    LOG_ERROR("nfc", "PN532 nao responde");
  }

  uint32_t seenGen = 0;
  TickType_t period = pdMS_TO_TICKS(200);
  UidMsg last{};
  uint32_t lastSeenMs = 0;
  uint32_t lastInitMs = millis();
  TickType_t wake = xTaskGetTickCount();

  for (;;) {
    esp_task_wdt_reset();

    if (settings::generation() != seenGen) {
      seenGen = settings::generation();
      period = pdMS_TO_TICKS(settings::get().nfcPollMs);
    }

    const uint32_t now = millis();
    if (!nfc::ready()) {
      if (now - lastInitMs >= RETRY_INIT_MS) {
        lastInitMs = now;
        if (nfc::begin()) LOG_INFO("nfc", "PN532 recuperado");
      }
    } else {
      UidMsg msg{};
      if (nfc::poll(msg.uid, &msg.len)) {
        shared::nfcReads++;
        const bool same = msg.len == last.len && memcmp(msg.uid, last.uid, msg.len) == 0;
        // Só renova a janela enquanto o cartão fica no campo: tirar e reaproximar
        // depois da janela conta como nova leitura.
        if (!same || now - lastSeenMs >= NFC_REPEAT_SUPPRESS_MS) xQueueSend(shared::uidQueue, &msg, 0);
        last = msg;
        lastSeenMs = now;
      }
    }
    // Até o fim do ciclo a task não ocupa CPU; o período vem da configuração.
    xTaskDelayUntil(&wake, period);
  }
}
