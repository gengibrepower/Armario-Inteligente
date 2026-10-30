#pragma once
#include <stddef.h>
#include <stdint.h>

// Uso de CPU por core e dados de cada task.
//
// Técnica: o prompt assumia que o core Arduino não trazia run-time stats e
// sugeria idle hooks calibrados. O core 3.3 traz CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
// com base no esp_timer (1 µs), então usamos o tempo real de cada task. Vantagens
// sobre o idle hook: não exige calibração, não depende da frequência da CPU, dá o
// percentual por task e não mantém o idle em laço, preservando a instrução waiti
// que reduz o consumo. Uso do core = 100% - fatia do IDLE daquele core.
namespace cpuusage {

struct TaskInfo {
  char name[16];
  int8_t core;  // -1: sem afinidade.
  uint8_t priority;
  uint8_t state;
  uint8_t cpuPct;
  uint32_t stackFree;
};

void begin();
void sample();  // Chamado a cada período pela taskMetrics; publica os percentuais.
size_t tasks(TaskInfo* out, size_t capacity);

}  // namespace cpuusage
