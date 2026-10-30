#pragma once
// Aritmética do buffer circular, separada do acesso a arquivo para ser testada.
//
// Por que vários segmentos e não um arquivo único: o LittleFS é copy-on-write e
// encadeia os blocos de trás para frente; alterar o início de um arquivo obriga
// a regravar tudo até o fim dele. Num anel de arquivo único, cada escrita
// custaria dezenas de blocos. Com segmentos só de acréscimo, cada escrita toca
// apenas a cauda de um arquivo pequeno, e apagar o segmento mais antigo é O(1).
#include <stdint.h>

struct RingGeometry {
  uint16_t segments;
  uint16_t perSegment;

  uint32_t capacity() const { return uint32_t(segments) * perSegment; }

  // A posição deriva só do número de sequência, sem índice que possa divergir.
  uint16_t segmentOf(uint32_t seq) const { return (seq / perSegment) % segments; }
  uint16_t offsetOf(uint32_t seq) const { return seq % perSegment; }

  // Quantos registros ainda existem quando o próximo a escrever é `head`.
  uint32_t retained(uint32_t head) const {
    if (head <= capacity()) return head;
    return uint32_t(segments - 1) * perSegment + (head - 1) % perSegment + 1;
  }

  uint32_t oldest(uint32_t head) const { return head - retained(head); }
};
