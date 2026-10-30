// OTA por upload: grava na partição inativa e só troca ao final de uma imagem
// íntegra. Com rollback ligado no bootloader, a imagem nova entra em "pendente" e
// volta sozinha para a anterior se não confirmar saúde (ver markFirmwareHealthy).
#include <Update.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>

#include <atomic>

#include "auth.h"
#include "core/shared_state.h"
#include "server.h"
#include "storage/logger.h"

// Faz o core Arduino deixar a confirmação da imagem para o nosso código.
extern "C" bool verifyRollbackLater() { return true; }

namespace {
std::atomic<bool> active{false};
const char* failure = nullptr;

void abortUpdate(const char* reason) {
  failure = reason;
  if (active.exchange(false)) Update.abort();
  LOG_WARN("ota", "Atualizacao cancelada: %s", reason);
}

// Recusa antes de apagar flash: arquivo errado não deve custar a partição inativa.
const char* validateHeader(const uint8_t* data, size_t len) {
  if (len < sizeof(esp_image_header_t)) return "arquivo muito pequeno";
  const auto* header = reinterpret_cast<const esp_image_header_t*>(data);
  if (header->magic != ESP_IMAGE_HEADER_MAGIC) return "nao e uma imagem de firmware";
  if (header->chip_id != ESP_CHIP_ID_ESP32) return "imagem de outro chip";
  return nullptr;
}

void onChunk(AsyncWebServerRequest* req, const String&, size_t index, uint8_t* data, size_t len, bool final) {
  if (index == 0) {
    failure = nullptr;
    if (!auth::authenticated(req) || auth::mustChangePassword()) {
      failure = "login necessario";
      return;
    }
    if (shared::read().state != LockerState::Idle) {
      failure = "armario em uso";
      return;
    }
    if (const char* invalid = validateHeader(data, len)) {
      failure = invalid;
      return;
    }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      failure = "nao foi possivel iniciar a gravacao";
      return;
    }
    active = true;
    LOG_INFO("ota", "Atualizacao iniciada");
    // Cliente caiu no meio: sem isto a partição ficaria aberta e o OTA, travado.
    req->onDisconnect([] {
      if (active.load()) abortUpdate("conexao perdida");
    });
  }
  if (failure || !active.load()) return;

  if (Update.write(data, len) != len) return abortUpdate("falha ao gravar");
  if (final) {
    active = false;
    if (!Update.end(true)) {
      failure = "imagem invalida";
      LOG_ERROR("ota", "Imagem recusada na verificacao final");
    }
  }
}

void onFinished(AsyncWebServerRequest* req) {
  if (failure) {
    char body[80];
    snprintf(body, sizeof(body), "{\"error\":\"%s\"}", failure);
    req->send(400, "application/json", body);
    return;
  }
  LOG_INFO("ota", "Imagem gravada; reiniciando");
  req->send(200, "application/json", "{\"ok\":true}");
  web::scheduleReboot(1000);
}
}  // namespace

namespace web {

void registerOta(AsyncWebServer& server) { server.on("/api/ota", HTTP_POST, onFinished, onChunk); }

bool otaInProgress() { return active.load(); }

}  // namespace web
