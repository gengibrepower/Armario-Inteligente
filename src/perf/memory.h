#pragma once
#include <stdint.h>

namespace perfmem {

struct Snapshot {
  uint32_t heapFree;
  uint32_t heapTotal;
  uint32_t heapMin;      // Menor valor desde o boot: a margem real do sistema.
  uint32_t heapLargest;
  uint8_t fragmentationPct;
  uint32_t flashSize;
  uint32_t appUsed;
  uint32_t appMax;
  uint32_t fsTotal;
  uint32_t fsUsed;
};

void begin();
void sample();
Snapshot read();

}  // namespace perfmem
