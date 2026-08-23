// log.hpp - minimal crash-safe staged logger.
// Writes to sdmc:/makro_terminal.log, flushing and closing on every call so
// the last line survives even a hard crash / force-close. Read it off the SD
// card to see exactly which stage was reached.
#pragma once
#include <cstdio>
#include <cstdarg>
#include <ctime>

namespace applog {

inline const char* path() { return "sdmc:/makro_terminal.log"; }

inline void reset() {
    FILE* f = std::fopen(path(), "w");
    if (f) std::fclose(f);
}

inline void line(const char* fmt, ...) {
    FILE* f = std::fopen(path(), "a");
    if (!f) return;
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    std::fprintf(f, "[%02d:%02d:%02d] ", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fputc('\n', f);
    std::fflush(f);
    std::fclose(f);
}

} // namespace applog
