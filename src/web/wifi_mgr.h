#pragma once
// Wi-Fi em STA com queda para AP. Toda chamada à pilha Wi-Fi sai da taskWeb,
// o que evita corrida entre reconexão, scan e entrada em sono.
#include <ArduinoJson.h>
#include <stdint.h>

namespace wifimgr {

enum class Mode : uint8_t { Off, Connecting, Station, AccessPoint };

struct Status {
  Mode mode;
  char ssid[33];
  char ip[16];
  char mac[18];
  int8_t rssi;
  uint8_t channel;
  uint32_t connectedSec;
  uint32_t reconnects;
  bool apActive;
  uint8_t apClients;
};

void begin();
void loop();
void status(Status* out);
const char* modeName(Mode mode);

void startScan();
bool scanResults(JsonArray out);  // false enquanto o scan não terminou.

// Chamadas pela taskPower: desligam/religam o rádio pela taskWeb e esperam.
bool pause(uint32_t timeoutMs);
void resume();

}  // namespace wifimgr
