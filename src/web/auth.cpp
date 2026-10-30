#include "auth.h"

#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <mbedtls/sha256.h>

#include "config.h"
#include "storage/logger.h"

namespace {
constexpr const char* NVS_NAMESPACE = "auth";
constexpr const char* DEFAULT_PASSWORD = "admin";
constexpr const char* USERNAME = "admin";
constexpr size_t SALT_LEN = 16;
constexpr size_t HASH_LEN = 32;

struct Session {
  char token[auth::TOKEN_LEN + 1];
  uint32_t expiresAt;
};

Session sessions[AUTH_MAX_SESSIONS];
uint8_t salt[SALT_LEN];
uint8_t storedHash[HASH_LEN];
bool mustChange = true;
uint8_t failures = 0;
uint32_t lockedUntil = 0;

uint32_t nowSec() { return esp_timer_get_time() / 1000000; }

// SHA-256 iterado com sal: o sal impede tabelas prontas e as iterações encarecem
// a força bruta caso alguém leia a NVS. Usa o acelerador de hash do ESP32.
void hashPassword(const char* password, const uint8_t* saltBytes, uint8_t* out) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, saltBytes, SALT_LEN);
  mbedtls_sha256_update(&ctx, reinterpret_cast<const uint8_t*>(password), strlen(password));
  mbedtls_sha256_finish(&ctx, out);
  for (int i = 1; i < AUTH_HASH_ITERATIONS; i++) {
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, out, HASH_LEN);
    mbedtls_sha256_finish(&ctx, out);
  }
  mbedtls_sha256_free(&ctx);
}

// Tempo constante: uma comparação que para no primeiro byte diferente vaza o prefixo.
bool sameBytes(const uint8_t* a, const uint8_t* b, size_t len) {
  uint8_t diff = 0;
  for (size_t i = 0; i < len; i++) diff |= a[i] ^ b[i];
  return diff == 0;
}

void persist(bool requireChange) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putBytes("salt", salt, SALT_LEN);
  prefs.putBytes("hash", storedHash, HASH_LEN);
  prefs.putBool("mustChange", requireChange);
  prefs.end();
  mustChange = requireChange;
}

void setPassword(const char* password, bool requireChange) {
  esp_fill_random(salt, SALT_LEN);
  hashPassword(password, salt, storedHash);
  persist(requireChange);
}

bool passwordMatches(const char* password) {
  uint8_t candidate[HASH_LEN];
  hashPassword(password, salt, candidate);
  return sameBytes(candidate, storedHash, HASH_LEN);
}

const char* tokenFromCookie(AsyncWebServerRequest* req, char* buffer, size_t len) {
  const AsyncWebHeader* header = req->getHeader("Cookie");
  if (!header) return nullptr;
  const char* found = strstr(header->value().c_str(), "session=");
  if (!found) return nullptr;
  found += strlen("session=");
  size_t i = 0;
  while (found[i] && found[i] != ';' && i + 1 < len) {
    buffer[i] = found[i];
    i++;
  }
  buffer[i] = '\0';
  return buffer;
}

Session* findSession(AsyncWebServerRequest* req) {
  char token[auth::TOKEN_LEN + 1];
  if (!tokenFromCookie(req, token, sizeof(token))) return nullptr;
  if (strlen(token) != auth::TOKEN_LEN) return nullptr;
  const uint32_t now = nowSec();
  for (Session& s : sessions) {
    if (s.token[0] == '\0' || s.expiresAt < now) continue;
    if (sameBytes(reinterpret_cast<const uint8_t*>(s.token), reinterpret_cast<const uint8_t*>(token), auth::TOKEN_LEN)) return &s;
  }
  return nullptr;
}
}  // namespace

namespace auth {

void begin() {
  Preferences prefs;
  bool loaded = false;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    loaded = prefs.getBytes("salt", salt, SALT_LEN) == SALT_LEN && prefs.getBytes("hash", storedHash, HASH_LEN) == HASH_LEN;
    mustChange = prefs.getBool("mustChange", true);
    prefs.end();
  }
  if (!loaded) setPassword(DEFAULT_PASSWORD, true);
}

LoginResult login(const char* user, const char* password, char* tokenOut) {
  const uint32_t now = nowSec();
  if (now < lockedUntil) return LoginResult::LockedOut;

  if (strcmp(user, USERNAME) != 0 || !passwordMatches(password)) {
    if (++failures >= AUTH_MAX_FAILURES) {
      failures = 0;
      lockedUntil = now + AUTH_LOCKOUT_S;
      LOG_WARN("auth", "Login bloqueado por %d s", AUTH_LOCKOUT_S);
    }
    return LoginResult::BadCredentials;
  }
  failures = 0;

  Session* slot = &sessions[0];
  for (Session& s : sessions) {
    if (s.expiresAt < slot->expiresAt) slot = &s;  // Reaproveita a mais antiga.
  }
  uint8_t random[TOKEN_LEN / 2];
  esp_fill_random(random, sizeof(random));
  for (size_t i = 0; i < sizeof(random); i++) snprintf(&slot->token[i * 2], 3, "%02x", random[i]);
  slot->expiresAt = now + AUTH_SESSION_TTL_S;
  strlcpy(tokenOut, slot->token, TOKEN_LEN + 1);
  LOG_INFO("auth", "Login do administrador");
  return LoginResult::Ok;
}

void logout(AsyncWebServerRequest* req) {
  if (Session* s = findSession(req)) memset(s, 0, sizeof(*s));
}

bool authenticated(AsyncWebServerRequest* req) {
  Session* s = findSession(req);
  if (!s) return false;
  s->expiresAt = nowSec() + AUTH_SESSION_TTL_S;  // Expiração por inatividade.
  return true;
}

bool mustChangePassword() { return mustChange; }

bool changePassword(const char* current, const char* next) {
  if (strlen(next) < 8 || strcmp(next, DEFAULT_PASSWORD) == 0 || !passwordMatches(current)) return false;
  setPassword(next, false);
  LOG_INFO("auth", "Senha do administrador alterada");
  return true;
}

}  // namespace auth
