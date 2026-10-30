#pragma once
// Logger assíncrono: quem registra só formata e enfileira; a flash fica com a
// taskStorage, então logar no core físico nunca espera uma escrita.
#include "core/types.h"

namespace logger {

void write(LogLevel level, const char* module, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
uint32_t dropped();

}  // namespace logger

#define LOG_ERROR(module, ...) logger::write(LogLevel::Error, module, __VA_ARGS__)
#define LOG_WARN(module, ...) logger::write(LogLevel::Warn, module, __VA_ARGS__)
#define LOG_INFO(module, ...) logger::write(LogLevel::Info, module, __VA_ARGS__)
