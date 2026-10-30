#pragma once
// Ponto único de entrada para a persistência. Todas as escritas viram mensagens
// para a taskStorage: há um só escritor, e nenhuma task física toca na flash.
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "core/shared_state.h"
#include "core/types.h"
#include "ring_file.h"

// Empréstimo em aberto, gravado em NVS: precisa sobreviver a reset com o notebook fora.
struct ActiveLoanMsg {
  uint8_t slot;
  ActiveLoan value;
};

struct StorageMsg {
  enum Kind : uint8_t { Log, Event, Minute, Hour, Loan, ClearLogs } kind;
  union {
    LogRecord log;
    EventRecord event;
    MinuteSample minute;
    HourSample hour;
    ActiveLoanMsg loan;
  };
};

namespace storage {

bool begin();  // Monta o LittleFS e cria a fila; roda antes de qualquer task.
bool mounted();

bool post(const StorageMsg& msg, TickType_t wait);
void postEvent(const EventRecord& rec);  // Sempre pede flush: evento é crítico.
void requestFlush();

// Solicita flush e espera a conclusão. Usado antes de dormir ou reiniciar.
bool flushAndWait(TickType_t timeout);

RingFile& logs();
RingFile& events();
RingFile& minutes();
RingFile& hours();

void postLoan(uint8_t slot, const ActiveLoan& loan);
bool loadLoan(uint8_t slot, ActiveLoan* loan);

void runWriter();  // Corpo da taskStorage.

}  // namespace storage
