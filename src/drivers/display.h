#pragma once
#include <stdint.h>

// Texto já resolvido pela taskDisplay; o driver só desenha (ASCII: a fonte
// padrão do Adafruit GFX não tem acentos do português).
struct DisplayModel {
  char title[22];
  char line1[22];
  char line2[22];
  char footer[22];
};

namespace display {

bool begin();
bool ready();
void render(const DisplayModel& model);

}  // namespace display
