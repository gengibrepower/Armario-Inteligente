#pragma once
#include <ArduinoJson.h>

// Visões JSON compartilhadas entre REST e WebSocket.
namespace web {

void stateToJson(JsonObject out);
void usageToJson(JsonObject out);
void perfToJson(JsonObject out);

const char* stateName(uint8_t state);
const char* alarmName(uint8_t alarm);
const char* eventTypeName(uint8_t type);

}  // namespace web
