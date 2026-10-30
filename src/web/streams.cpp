#include "streams.h"

#include <memory>

#include "core/types.h"

namespace {
constexpr size_t LINE_CAPACITY = 320;
constexpr size_t RECORD_CAPACITY = 128;
constexpr uint16_t MAX_SCAN_PER_CHUNK = 200;  // Mantém a task TCP responsiva em filtros raros.

class RingStream {
 public:
  RingStream(const web::StreamSpec& spec, web::LineFormatter format)
      : spec_(spec), format_(std::move(format)), reader_(*spec.ring) {
    const uint32_t oldest = spec.ring->oldest();
    const uint32_t head = spec.ring->head();
    remaining_ = head - oldest;
    next_ = spec.newestFirst ? head - 1 : oldest;
    skip_ = spec.offset;
    copyText(spec.header);
  }

  size_t fill(uint8_t* buffer, size_t capacity) {
    size_t written = 0;
    uint16_t scanned = 0;
    while (written < capacity) {
      if (linePos_ < lineLen_) {
        const size_t n = min(capacity - written, lineLen_ - linePos_);
        memcpy(buffer + written, line_ + linePos_, n);
        written += n;
        linePos_ += n;
        continue;
      }
      if (finished_) break;
      if (scanned >= MAX_SCAN_PER_CHUNK && written > 0) break;
      advance(&scanned);
    }
    return written;
  }

 private:
  void copyText(const char* text) {
    lineLen_ = strlcpy(line_, text, sizeof(line_));
    linePos_ = 0;
  }

  void finish() {
    finished_ = true;
    reader_.close();
    copyText(spec_.footer);
  }

  void advance(uint16_t* scanned) {
    if (remaining_ == 0 || (spec_.limit && emitted_ >= spec_.limit)) {
      finish();
      return;
    }

    const uint32_t seq = next_;
    next_ += spec_.newestFirst ? -1 : 1;
    remaining_--;
    (*scanned)++;

    if (!reader_.read(seq, record_)) return;
    size_t len = 0;
    const size_t sepLen = emitted_ ? strlen(spec_.separator) : 0;
    memcpy(line_, spec_.separator, sepLen);
    len = format_(record_, line_ + sepLen, sizeof(line_) - sepLen);
    if (len == 0) return;
    if (skip_ > 0) {
      skip_--;
      return;
    }
    if (spec_.step > 1 && (counter_++ % spec_.step) != 0) return;
    lineLen_ = sepLen + len;
    linePos_ = 0;
    emitted_++;
  }

  web::StreamSpec spec_;
  web::LineFormatter format_;
  RingFile::Reader reader_;
  uint8_t record_[RECORD_CAPACITY];
  char line_[LINE_CAPACITY];
  size_t lineLen_ = 0;
  size_t linePos_ = 0;
  uint32_t next_ = 0;
  uint32_t remaining_ = 0;
  uint32_t emitted_ = 0;
  uint32_t skip_ = 0;
  uint32_t counter_ = 0;
  bool finished_ = false;
};
}  // namespace

namespace web {

void sendRingStream(AsyncWebServerRequest* req, const StreamSpec& spec, LineFormatter format) {
  auto stream = std::make_shared<RingStream>(spec, std::move(format));
  AsyncWebServerResponse* response = req->beginChunkedResponse(
      spec.contentType, [stream](uint8_t* buffer, size_t maxLen, size_t) -> size_t { return stream->fill(buffer, maxLen); });
  if (spec.filename) {
    char disposition[64];
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", spec.filename);
    response->addHeader("Content-Disposition", disposition);
  }
  req->send(response);
}

size_t jsonEscape(const char* in, char* out, size_t cap) {
  size_t o = 0;
  for (; *in && o + 7 < cap; in++) {
    const unsigned char c = *in;
    if (c == '"' || c == '\\') {
      out[o++] = '\\';
      out[o++] = c;
    } else if (c < 0x20) {
      o += snprintf(out + o, cap - o, "\\u%04x", c);
    } else {
      out[o++] = c;
    }
  }
  out[o] = '\0';
  return o;
}

size_t csvEscape(const char* in, char* out, size_t cap) {
  size_t o = 0;
  if (cap < 3) return 0;
  out[o++] = '"';
  // Planilhas executam células iniciadas por estes sinais como fórmula.
  if (*in == '=' || *in == '+' || *in == '-' || *in == '@') out[o++] = '\'';
  for (; *in && o + 4 < cap; in++) {
    if (*in == '"') out[o++] = '"';
    out[o++] = *in;
  }
  out[o++] = '"';
  out[o] = '\0';
  return o;
}

size_t formatTimestamp(uint32_t ts, uint8_t flags, char* out, size_t cap) {
  if (!(flags & REC_TIME_SYNCED)) return snprintf(out, cap, "sem hora sincronizada (uptime %lu s)", (unsigned long)ts);
  const time_t t = ts;
  struct tm local;
  localtime_r(&t, &local);
  return strftime(out, cap, "%Y-%m-%d %H:%M:%S", &local);
}

}  // namespace web
