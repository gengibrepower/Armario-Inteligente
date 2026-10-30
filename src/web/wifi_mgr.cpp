#include "wifi_mgr.h"

#include <DNSServer.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <freertos/event_groups.h>

#include <atomic>

#include "config.h"
#include "core/shared_state.h"
#include "storage/logger.h"
#include "storage/settings.h"

namespace {
constexpr EventBits_t BIT_PAUSE_REQ = BIT0;
constexpr EventBits_t BIT_PAUSED = BIT1;
constexpr EventBits_t BIT_RESUME_REQ = BIT2;

wifimgr::Mode mode = wifimgr::Mode::Off;
DNSServer dns;
EventGroupHandle_t flags;
Settings cfg;
uint32_t seenGen = 0;
uint32_t stateSince = 0;
uint32_t lastApRetry = 0;
bool apRunning = false;
bool paused = false;
bool scanRequested = false;

std::atomic<uint32_t> reconnects{0};
std::atomic<uint32_t> connectedAt{0};
bool everConnected = false;

// Protege a cópia lida pela web, que roda em outra task.
SemaphoreHandle_t statusMutex;
wifimgr::Status published;

void onTimeSync(struct timeval*) { LOG_INFO("time", "Relogio sincronizado por NTP"); }

void applyTime() {
  setenv("TZ", cfg.timezone, 1);
  tzset();
  // O SNTP precisa da pilha de rede: sem conexão, aplica-se só o fuso.
  if (mode != wifimgr::Mode::Station) return;
  sntp_set_time_sync_notification_cb(onTimeSync);
  configTzTime(cfg.timezone, cfg.ntpServer);
}

void setMode(wifimgr::Mode next) {
  mode = next;
  stateSince = millis();
}

void connectStation() {
  if (cfg.staticIp) {
    IPAddress ip, gateway, subnet, dnsIp;
    ip.fromString(cfg.ip);
    gateway.fromString(cfg.gateway);
    subnet.fromString(cfg.subnet);
    dnsIp.fromString(cfg.dns);
    WiFi.config(ip, gateway, subnet, dnsIp);
  } else {
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  }
  WiFi.begin(cfg.wifiSsid, cfg.wifiPass);
  setMode(wifimgr::Mode::Connecting);
  LOG_INFO("wifi", "Conectando a %s", cfg.wifiSsid);
}

void startAccessPoint() {
  // AP_STA: o portal fica no ar enquanto o rádio ainda tenta a rede configurada.
  WiFi.mode(cfg.wifiSsid[0] ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  dns.start(53, "*", WiFi.softAPIP());  // Todo domínio resolve aqui: portal cativo.
  apRunning = true;
  lastApRetry = millis();
  setMode(wifimgr::Mode::AccessPoint);
  LOG_WARN("wifi", "Modo AP %s em %s", AP_SSID, WiFi.softAPIP().toString().c_str());
}

void stopAccessPoint() {
  dns.stop();
  WiFi.softAPdisconnect(true);
  apRunning = false;
}

void restartNetwork() {
  if (apRunning) stopAccessPoint();
  WiFi.disconnect(true);
  if (cfg.wifiSsid[0] == '\0') {
    startAccessPoint();
    return;
  }
  WiFi.mode(WIFI_STA);
  connectStation();
}

void onConnected() {
  setMode(wifimgr::Mode::Station);
  if (apRunning) stopAccessPoint();
  WiFi.mode(WIFI_STA);
  // Modem sleep é incompatível com AP, então só vale neste estado.
  WiFi.setSleep(cfg.modemSleep);
  connectedAt = millis();
  if (everConnected) reconnects++;
  everConnected = true;
  applyTime();
  LOG_INFO("wifi", "Conectado, IP %s", WiFi.localIP().toString().c_str());
}

void reloadSettings() {
  if (settings::generation() == seenGen) return;
  seenGen = settings::generation();
  const Settings next = settings::get();
  const bool networkChanged = strcmp(next.wifiSsid, cfg.wifiSsid) != 0 || strcmp(next.wifiPass, cfg.wifiPass) != 0 ||
                              next.staticIp != cfg.staticIp || strcmp(next.ip, cfg.ip) != 0 ||
                              strcmp(next.gateway, cfg.gateway) != 0 || strcmp(next.subnet, cfg.subnet) != 0 ||
                              strcmp(next.dns, cfg.dns) != 0;
  const bool timeChanged = strcmp(next.timezone, cfg.timezone) != 0 || strcmp(next.ntpServer, cfg.ntpServer) != 0;
  cfg = next;
  if (networkChanged) {
    restartNetwork();
  } else {
    if (timeChanged) applyTime();
    if (mode == wifimgr::Mode::Station) WiFi.setSleep(cfg.modemSleep);
  }
}

void publishStatus() {
  wifimgr::Status s{};
  s.mode = mode;
  s.apActive = apRunning;
  strlcpy(s.mac, WiFi.macAddress().c_str(), sizeof(s.mac));
  s.reconnects = reconnects.load();
  if (mode == wifimgr::Mode::Station) {
    strlcpy(s.ssid, WiFi.SSID().c_str(), sizeof(s.ssid));
    strlcpy(s.ip, WiFi.localIP().toString().c_str(), sizeof(s.ip));
    s.rssi = WiFi.RSSI();
    s.channel = WiFi.channel();
    s.connectedSec = (millis() - connectedAt.load()) / 1000;
  } else if (mode == wifimgr::Mode::AccessPoint) {
    strlcpy(s.ssid, AP_SSID, sizeof(s.ssid));
    strlcpy(s.ip, WiFi.softAPIP().toString().c_str(), sizeof(s.ip));
    s.channel = WiFi.channel();
    s.apClients = WiFi.softAPgetStationNum();
  }
  shared::wifiRssi = s.rssi;

  xSemaphoreTake(statusMutex, portMAX_DELAY);
  const bool ipChanged = strcmp(published.ip, s.ip) != 0 || published.mode != s.mode;
  published = s;
  xSemaphoreGive(statusMutex);

  if (ipChanged) {
    shared::update([&s](LockerSnapshot& snap) {
      if (s.mode == wifimgr::Mode::AccessPoint) {
        strlcpy(snap.networkLine, "AP " AP_SSID, sizeof(snap.networkLine));
      } else {
        strlcpy(snap.networkLine, s.ip[0] ? s.ip : "sem rede", sizeof(snap.networkLine));
      }
    });
  }
}

void handlePause() {
  const EventBits_t bits = xEventGroupGetBits(flags);
  if ((bits & BIT_PAUSE_REQ) && !paused) {
    xEventGroupClearBits(flags, BIT_PAUSE_REQ);
    if (apRunning) stopAccessPoint();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    setMode(wifimgr::Mode::Off);
    paused = true;
    xEventGroupSetBits(flags, BIT_PAUSED);
  }
  if ((bits & BIT_RESUME_REQ) && paused) {
    xEventGroupClearBits(flags, BIT_RESUME_REQ);
    paused = false;
    restartNetwork();
  }
}
}  // namespace

namespace wifimgr {

void begin() {
  statusMutex = xSemaphoreCreateMutex();
  flags = xEventGroupCreate();
  memset(&published, 0, sizeof(published));
  cfg = settings::get();
  seenGen = settings::generation();
  WiFi.persistent(false);  // A credencial mora na nossa NVS; evita gravação dupla.
  WiFi.setAutoReconnect(true);
  WiFi.setHostname(HOSTNAME);
  restartNetwork();
}

void loop() {
  handlePause();
  if (paused) return;
  reloadSettings();

  const bool connected = WiFi.status() == WL_CONNECTED;
  const uint32_t now = millis();

  switch (mode) {
    case Mode::Connecting:
      if (connected) {
        onConnected();
      } else if (now - stateSince >= STA_CONNECT_TIMEOUT_MS) {
        startAccessPoint();
      }
      break;
    case Mode::Station:
      if (!connected) {
        LOG_WARN("wifi", "Conexao perdida");
        setMode(Mode::Connecting);  // setAutoReconnect tenta; o timeout leva ao AP.
      }
      break;
    case Mode::AccessPoint:
      if (connected) {
        onConnected();
      } else if (cfg.wifiSsid[0] && WiFi.softAPgetStationNum() == 0 && now - lastApRetry >= STA_RETRY_IN_AP_MS) {
        // A tentativa troca o canal do rádio: com alguém no portal, a conexão dele cairia.
        lastApRetry = now;
        WiFi.begin(cfg.wifiSsid, cfg.wifiPass);
      }
      break;
    case Mode::Off:
      break;
  }

  if (scanRequested) {
    scanRequested = false;
    WiFi.scanNetworks(true);  // Assíncrono: o resultado é consultado por scanResults.
  }

  static uint32_t lastPublish = 0;
  if (now - lastPublish >= 1000) {
    lastPublish = now;
    publishStatus();
  }
}

void status(Status* out) {
  xSemaphoreTake(statusMutex, portMAX_DELAY);
  *out = published;
  xSemaphoreGive(statusMutex);
}

const char* modeName(Mode m) {
  switch (m) {
    case Mode::Off:
      return "OFF";
    case Mode::Connecting:
      return "CONECTANDO";
    case Mode::Station:
      return "STA";
    case Mode::AccessPoint:
      return "AP";
  }
  return "";
}

void startScan() { scanRequested = true; }

bool scanResults(JsonArray out) {
  const int16_t n = WiFi.scanComplete();
  if (n < 0) return false;
  for (int16_t i = 0; i < n; i++) {
    JsonObject o = out.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  return true;
}

bool pause(uint32_t timeoutMs) {
  xEventGroupClearBits(flags, BIT_PAUSED);
  xEventGroupSetBits(flags, BIT_PAUSE_REQ);
  return xEventGroupWaitBits(flags, BIT_PAUSED, pdTRUE, pdTRUE, pdMS_TO_TICKS(timeoutMs)) & BIT_PAUSED;
}

void resume() { xEventGroupSetBits(flags, BIT_RESUME_REQ); }

}  // namespace wifimgr
