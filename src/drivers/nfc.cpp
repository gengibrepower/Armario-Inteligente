#include "nfc.h"

#include <Adafruit_PN532.h>
#include <SPI.h>

#include "config.h"
#include "perf/profiler.h"
#include "pins.h"

namespace {
Adafruit_PN532 reader(PIN_NFC_SS, &SPI);
uint32_t firmware = 0;
bool isReady = false;
}  // namespace

namespace nfc {

bool begin() {
  SPI.begin(PIN_NFC_SCK, PIN_NFC_MISO, PIN_NFC_MOSI, PIN_NFC_SS);
  isReady = false;
  if (!reader.begin()) return false;
  firmware = reader.getFirmwareVersion();
  if (firmware == 0) return false;
  if (!reader.SAMConfig()) return false;
  // O padrão do chip é tentar para sempre. Cancelar essa busca pelo nosso
  // timeout perderia cartões, então limitamos as tentativas e ele sempre
  // responde, com ou sem cartão, antes do próximo comando.
  if (!reader.setPassiveActivationRetries(1)) return false;
  isReady = true;
  return true;
}

bool ready() { return isReady; }

uint32_t firmwareVersion() { return firmware; }

bool poll(uint8_t* uid, uint8_t* uidLen) {
  PROFILE_SCOPE(ProfId::NfcRead);
  return reader.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLen, NFC_READ_TIMEOUT_MS);
}

}  // namespace nfc
