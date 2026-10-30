#include "server.h"

#include <LittleFS.h>
#include <WiFi.h>

#include "auth.h"
#include "config.h"
#include "core/shared_state.h"
#include "json_views.h"
#include "storage/logger.h"
#include "storage/settings.h"
#include "storage/storage.h"
#include "wifi_mgr.h"

namespace {
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
uint32_t rebootAt = 0;
uint32_t lastCleanup = 0;
uint32_t lastStateVersion = 0;
char stateBuffer[1024];

void onSocket(AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type, void*, uint8_t*, size_t) {
  if (type != WS_EVT_CONNECT) return;
  // Sem PSRAM cada cliente custa fila e buffers: o excedente é recusado.
  if (ws.count() > WS_MAX_CLIENTS) client->close();
}

void broadcastState() {
  JsonDocument doc;
  doc["t"] = "state";
  web::stateToJson(doc.as<JsonObject>());
  const size_t len = serializeJson(doc, stateBuffer, sizeof(stateBuffer));
  if (len > 0 && len < sizeof(stateBuffer)) ws.textAll(stateBuffer, len);
}
}  // namespace

namespace web {

void begin() {
  auth::begin();

  ws.onEvent(onSocket);
  ws.handleHandshake([](AsyncWebServerRequest* req) { return settings::get().publicPages || auth::authenticated(req); });
  server.addHandler(&ws);

  registerApi(server);
  registerOta(server);

  // Só /www é público. Dados privados (cadastro, logs) ficam fora desta árvore,
  // então não dependem de lista de bloqueio para não vazar.
  server.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html").setCacheControl("max-age=300");

  server.onNotFound([](AsyncWebServerRequest* req) {
    wifimgr::Status net;
    wifimgr::status(&net);
    const bool foreignHost = req->host() != net.ip;
    if (net.mode == wifimgr::Mode::AccessPoint && foreignHost) {
      // Celulares testam um domínio fixo ao entrar na rede: o redirect abre o portal.
      req->redirect(String("http://") + net.ip + "/admin.html");
      return;
    }
    req->send(404, "application/json", "{\"error\":\"nao encontrado\"}");
  });

  server.begin();
  LOG_INFO("web", "Servidor HTTP iniciado");
}

void loop() {
  const uint32_t now = millis();

  if (now - lastCleanup >= 1000) {
    lastCleanup = now;
    ws.cleanupClients(WS_MAX_CLIENTS);
  }

  const uint32_t version = shared::stateVersion.load();
  if (version != lastStateVersion) {
    lastStateVersion = version;
    if (ws.count() > 0) broadcastState();
  }

  if (rebootAt != 0 && static_cast<int32_t>(now - rebootAt) >= 0) {
    storage::flushAndWait(pdMS_TO_TICKS(2000));
    ESP.restart();
  }
}

void scheduleReboot(uint32_t delayMs) { rebootAt = millis() + delayMs; }

bool hasClients() { return ws.count() > 0; }

void broadcast(const char* json, size_t len) {
  if (ws.availableForWriteAll()) ws.textAll(json, len);
}

}  // namespace web
