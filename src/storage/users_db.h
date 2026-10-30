#pragma once
// Cadastro de usuários. A consulta é só em RAM: validar um cartão no core físico
// não pode depender da flash, cujas escritas pausam os dois cores.
#include <ArduinoJson.h>
#include <stdint.h>

#include "config.h"

struct User {
  uint16_t id;
  char name[32];
  char ra[16];
  char email[40];
  char uid[15];
  bool active;
  bool admin;
};

namespace users {

bool begin();
bool findByUid(const char* uidHex, User* out);
bool getById(uint16_t id, User* out);
size_t count();

// Preenche/atualiza a partir de JSON. id ausente cria um usuário novo.
bool upsert(JsonObjectConst json, uint16_t* outId, char* err, size_t errLen);
bool remove(uint16_t id);

void toJson(JsonArray out);
bool restore(JsonArrayConst json, char* err, size_t errLen);

// Persistência adiada: chamada pela taskStorage, único escritor da flash.
void saveIfDirty();

}  // namespace users
