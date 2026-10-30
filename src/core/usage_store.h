#pragma once
#include "usage_stats.h"

// Cópia viva das estatísticas, compartilhada entre taskAccess e o servidor web.
namespace usage {

void begin();    // Refaz as estatísticas a partir do anel de eventos.
void record(const EventRecord& ev);

// Retenção por idade: o anel limita o espaço, e estes dois filtram o que passou
// do prazo configurado. Sem NTP, nada é considerado vencido.
uint32_t retentionCutoff();
bool retained(const EventRecord& ev, uint32_t cutoff);

void copy(UsageStats* out);

}  // namespace usage
