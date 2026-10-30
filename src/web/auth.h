#pragma once
#include <stddef.h>

class AsyncWebServerRequest;

// Sessões e senha do admin. As funções são chamadas pela task do AsyncTCP, que
// serializa as requisições, por isso a tabela de sessões dispensa mutex.
namespace auth {

constexpr size_t TOKEN_LEN = 32;

void begin();

enum class LoginResult { Ok, BadCredentials, LockedOut };
LoginResult login(const char* user, const char* password, char* tokenOut);
void logout(AsyncWebServerRequest* req);

bool authenticated(AsyncWebServerRequest* req);
bool mustChangePassword();

// Senha nova com menos de 8 caracteres é recusada.
bool changePassword(const char* current, const char* next);

}  // namespace auth
