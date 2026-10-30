#pragma once
#include <ESPAsyncWebServer.h>

namespace web {

void begin();
void loop();  // Chamado pela taskWeb a cada volta.

// Reinício adiado: a resposta HTTP precisa sair antes do reset.
void scheduleReboot(uint32_t delayMs);

// Publica no WebSocket. Sem cliente conectado, não gasta CPU serializando.
bool hasClients();
void broadcast(const char* json, size_t len);

void registerApi(AsyncWebServer& server);
void registerOta(AsyncWebServer& server);
bool otaInProgress();

}  // namespace web
