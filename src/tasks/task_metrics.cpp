#include <Arduino.h>
#include <ArduinoJson.h>

#include "config.h"
#include "perf/cpu_usage.h"
#include "perf/memory.h"
#include "tasks.h"
#include "web/json_views.h"
#include "web/server.h"

namespace {
char buffer[6144];  // Comporta ~30 tasks; estático para não disputar o heap.
}

void taskMetrics(void*) {
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    xTaskDelayUntil(&wake, pdMS_TO_TICKS(METRICS_PERIOD_MS));
    cpuusage::sample();
    perfmem::sample();

    // Sem ninguém olhando não há por que serializar: medir não pode virar carga.
    if (!web::hasClients()) continue;
    JsonDocument doc;
    doc["t"] = "perf";
    web::perfToJson(doc.as<JsonObject>());
    const size_t len = serializeJson(doc, buffer, sizeof(buffer));
    if (len > 0 && len < sizeof(buffer)) web::broadcast(buffer, len);
  }
}
