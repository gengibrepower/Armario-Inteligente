#include "ring_file.h"

#include <esp_rom_crc.h>
#include <unistd.h>

namespace {
constexpr const char* MOUNT = "/littlefs";

class Lock {
 public:
  explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(m_); }

 private:
  SemaphoreHandle_t m_;
};
}  // namespace

RingFile::RingFile(const char* dir, RingGeometry geometry, uint16_t payloadSize, uint16_t batchCapacity)
    : dir_(dir), geo_(geometry), payloadSize_(payloadSize), batchCapacity_(batchCapacity) {}

void RingFile::segmentPath(uint16_t segment, char* out, size_t len) const {
  snprintf(out, len, "/%s/%u.bin", dir_, segment);
}

uint16_t RingFile::crcOf(uint32_t seq, const void* payload) const {
  uint16_t crc = esp_rom_crc16_le(0, reinterpret_cast<const uint8_t*>(&seq), sizeof(seq));
  return esp_rom_crc16_le(crc, static_cast<const uint8_t*>(payload), payloadSize_);
}

bool RingFile::begin(bool mounted) {
  mutex_ = xSemaphoreCreateMutex();
  batch_ = static_cast<uint8_t*>(malloc(batchCapacity_ * recordSize()));
  if (!mutex_ || !batch_) return false;
  if (!mounted) return true;

  char path[24];
  snprintf(path, sizeof(path), "/%s", dir_);
  if (!LittleFS.exists(path)) LittleFS.mkdir(path);

  // Sem índice em disco: seq máximo + 1 é o head. Um índice separado exigiria
  // uma segunda escrita a cada lote e poderia divergir numa queda de energia.
  uint32_t maxSeq = 0;
  bool any = false;
  for (uint16_t s = 0; s < geo_.segments; s++) {
    uint32_t last;
    bool found;
    if (recoverSegment(s, &last, &found) && found && (!any || last > maxSeq)) {
      maxSeq = last;
      any = true;
    }
  }
  head_ = any ? maxSeq + 1 : 0;
  return true;
}

bool RingFile::recoverSegment(uint16_t segment, uint32_t* lastSeq, bool* found) {
  *found = false;
  char path[24];
  segmentPath(segment, path, sizeof(path));
  File f = LittleFS.open(path, FILE_READ);
  if (!f) return false;

  const size_t size = f.size();
  size_t validRecords = size / recordSize();
  uint8_t* tmp = static_cast<uint8_t*>(malloc(recordSize()));
  if (!tmp) {
    f.close();
    return false;
  }

  // Queda de energia pode deixar a cauda incompleta: recua até um CRC válido.
  while (validRecords > 0) {
    f.seek((validRecords - 1) * recordSize());
    if (f.read(tmp, recordSize()) == recordSize()) {
      Header h;
      memcpy(&h, tmp, sizeof(h));
      if (crcOf(h.seq, tmp + sizeof(h)) == h.crc) {
        *lastSeq = h.seq;
        *found = true;
        break;
      }
    }
    validRecords--;
  }
  free(tmp);
  f.close();

  const size_t goodSize = validRecords * recordSize();
  if (goodSize != size) {
    char full[40];
    snprintf(full, sizeof(full), "%s%s", MOUNT, path);
    truncate(full, goodSize);
  }
  return true;
}

void RingFile::append(const void* payload) {
  Lock lock(mutex_);
  if (pending_ == batchCapacity_) {
    // Quase nunca ocorre: a taskStorage esvazia pelo limiar e pelo período.
    xSemaphoreGive(mutex_);
    flush();
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (pending_ == batchCapacity_) {
      // Flash falhando: descarta o mais antigo do lote para não travar o sistema.
      memmove(batch_, batch_ + recordSize(), (pending_ - 1) * recordSize());
      pending_--;
    }
  }
  const uint32_t seq = head_;
  Header h{seq, crcOf(seq, payload)};
  uint8_t* slot = batch_ + pending_ * recordSize();
  memcpy(slot, &h, sizeof(h));
  memcpy(slot + sizeof(h), payload, payloadSize_);
  pending_++;
  head_++;
}

bool RingFile::appendRun(uint32_t firstSeq, uint16_t batchIndex, uint16_t count) {
  char path[24];
  segmentPath(geo_.segmentOf(firstSeq), path, sizeof(path));
  const size_t offset = size_t(geo_.offsetOf(firstSeq)) * recordSize();

  // Início de segmento: "w" trunca, descartando de uma vez o segmento mais antigo.
  File f = LittleFS.open(path, offset == 0 ? FILE_WRITE : FILE_APPEND, true);
  if (!f) return false;

  if (f.size() != offset) {
    // Escrita anterior falhou no meio: realinha para o registro cair na posição
    // que o seq determina, senão os seguintes ficariam ilegíveis.
    f.close();
    char full[40];
    snprintf(full, sizeof(full), "%s%s", MOUNT, path);
    if (truncate(full, offset) != 0) return false;
    f = LittleFS.open(path, FILE_APPEND, true);
    if (!f) return false;
  }

  const size_t bytes = size_t(count) * recordSize();
  const size_t written = f.write(batch_ + batchIndex * recordSize(), bytes);
  f.close();  // close() é o ponto de commit do LittleFS.
  return written == bytes;
}

bool RingFile::flush() {
  Lock lock(mutex_);
  uint16_t done = 0;
  bool ok = true;
  const uint32_t firstPending = head_ - pending_;

  while (done < pending_) {
    const uint32_t seq = firstPending + done;
    // Um lote pode cruzar a fronteira entre dois segmentos.
    const uint16_t room = geo_.perSegment - geo_.offsetOf(seq);
    const uint16_t run = min<uint16_t>(room, pending_ - done);
    if (!appendRun(seq, done, run)) {
      writeErrors_++;
      ok = false;
      break;
    }
    done += run;
  }

  if (done > 0) {
    memmove(batch_, batch_ + done * recordSize(), (pending_ - done) * recordSize());
    pending_ -= done;
  }
  return ok;
}

void RingFile::clear() {
  Lock lock(mutex_);
  char path[24];
  for (uint16_t s = 0; s < geo_.segments; s++) {
    segmentPath(s, path, sizeof(path));
    LittleFS.remove(path);
  }
  pending_ = 0;
  head_ = 0;
}

bool RingFile::Reader::read(uint32_t seq, void* payload) {
  Lock lock(ring_.mutex_);
  if (seq >= ring_.head_ || seq < ring_.oldest()) return false;

  const size_t recSize = ring_.recordSize();
  const uint32_t firstPending = ring_.head_ - ring_.pending_;
  if (seq >= firstPending) {
    // Ainda em RAM: quem consulta logs vê o evento antes do flush.
    memcpy(payload, ring_.batch_ + (seq - firstPending) * recSize + sizeof(Header), ring_.payloadSize_);
    return true;
  }

  const int segment = ring_.geo_.segmentOf(seq);
  if (segment != openSegment_ || !file_) {
    close();
    char path[24];
    ring_.segmentPath(segment, path, sizeof(path));
    file_ = LittleFS.open(path, FILE_READ);
    if (!file_) return false;
    openSegment_ = segment;
  }

  Header h;
  if (!file_.seek(size_t(ring_.geo_.offsetOf(seq)) * recSize)) return false;
  if (file_.read(reinterpret_cast<uint8_t*>(&h), sizeof(h)) != sizeof(h)) return false;
  if (file_.read(static_cast<uint8_t*>(payload), ring_.payloadSize_) != ring_.payloadSize_) return false;
  // seq confere: protege contra ler um segmento reescrito depois de aberto.
  return h.seq == seq && ring_.crcOf(seq, payload) == h.crc;
}

void RingFile::Reader::close() {
  if (file_) file_.close();
  openSegment_ = -1;
}
