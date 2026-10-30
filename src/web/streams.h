#pragma once
// Respostas em blocos a partir de um anel: exportar 1000 registros não pode
// montar a resposta inteira em RAM num sistema sem PSRAM.
#include <ESPAsyncWebServer.h>

#include <functional>

#include "storage/ring_file.h"

namespace web {

// Escreve a linha de um registro em `out`. Retorna 0 para pular (filtro).
using LineFormatter = std::function<size_t(const void* record, char* out, size_t cap)>;

struct StreamSpec {
  RingFile* ring;
  const char* contentType;
  const char* header;
  const char* footer;
  const char* separator;
  const char* filename;  // nullptr: exibir; preenchido: download.
  bool newestFirst;
  uint32_t limit;
  uint32_t offset;
  uint32_t step;  // Devolve 1 a cada `step` registros (gráfico de 24 h).
};

void sendRingStream(AsyncWebServerRequest* req, const StreamSpec& spec, LineFormatter format);

// Escapes mínimos para montar linhas manualmente, sem alocar um JsonDocument por registro.
size_t jsonEscape(const char* in, char* out, size_t cap);
size_t csvEscape(const char* in, char* out, size_t cap);
size_t formatTimestamp(uint32_t ts, uint8_t flags, char* out, size_t cap);

}  // namespace web
