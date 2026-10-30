#pragma once
// Buffer circular persistente de registros de tamanho fixo, com lote em RAM.
#include <Arduino.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "ring_math.h"

class RingFile {
 public:
  RingFile(const char* dir, RingGeometry geometry, uint16_t payloadSize, uint16_t batchCapacity);

  // Reconstrói o índice lendo o último registro de cada segmento. Sem sistema de
  // arquivos, o anel opera só na RAM: o armário não pode parar por falha da flash.
  bool begin(bool mounted);

  // Só a taskStorage chama append/flush/clear; a leitura vale para qualquer task.
  void append(const void* payload);
  bool flush();
  void clear();

  uint32_t head() const { return head_; }
  uint32_t oldest() const { return geo_.oldest(head_); }
  uint32_t count() const { return geo_.retained(head_); }
  uint16_t pending() const { return pending_; }
  uint16_t payloadSize() const { return payloadSize_; }
  uint32_t writeErrors() const { return writeErrors_; }

  // Leitura aleatória com cursor: mantém o segmento aberto para que varrer o
  // anel inteiro (exportação) não custe um open() por registro.
  class Reader {
   public:
    explicit Reader(RingFile& ring) : ring_(ring) {}
    ~Reader() { close(); }
    bool read(uint32_t seq, void* payload);
    void close();

   private:
    RingFile& ring_;
    File file_;
    int openSegment_ = -1;
  };

 private:
  struct __attribute__((packed)) Header {
    uint32_t seq;
    uint16_t crc;
  };

  uint32_t recordSize() const { return sizeof(Header) + payloadSize_; }
  void segmentPath(uint16_t segment, char* out, size_t len) const;
  uint16_t crcOf(uint32_t seq, const void* payload) const;
  bool recoverSegment(uint16_t segment, uint32_t* lastSeq, bool* found);
  bool appendRun(uint32_t firstSeq, uint16_t batchIndex, uint16_t count);

  const char* dir_;
  RingGeometry geo_;
  uint16_t payloadSize_;
  uint16_t batchCapacity_;
  uint8_t* batch_ = nullptr;
  uint16_t pending_ = 0;
  uint32_t head_ = 0;  // Próximo seq; inclui o que ainda está em RAM.
  uint32_t writeErrors_ = 0;
  SemaphoreHandle_t mutex_ = nullptr;
};
