#pragma once
#include <stdint.h>

namespace nfc {

constexpr uint8_t MAX_UID = 7;

// Bloqueia por alguns ms (reset do PN532), por isso roda dentro da taskNFC.
bool begin();
bool ready();
uint32_t firmwareVersion();

// Faz uma varredura limitada por NFC_READ_TIMEOUT_MS. Retorna true com cartão.
bool poll(uint8_t* uid, uint8_t* uidLen);

}  // namespace nfc
