#pragma once
// Configurações vivas: cópia em RAM protegida por mutex, persistida em NVS.
#include <stdint.h>

#include "settings_model.h"

namespace settings {

void begin();

// Devolve cópia: quem lê nunca segura o mutex durante o próprio trabalho.
Settings get();

// Valida, persiste e publica. Falha sem efeito colateral, com `err` preenchido.
bool update(JsonObjectConst json, char* err, size_t errLen);

// Cresce a cada alteração. As tasks comparam com o valor visto na última volta
// e recarregam, o que aplica a mudança sem reiniciar e sem acoplar as tasks.
uint32_t generation();

}  // namespace settings
