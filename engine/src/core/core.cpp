#include "as3d/core.h"

#include <cstdio>
#include <cstring>

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace as3d {

static LogLevel g_minLevel = LogLevel::Info;

void setLogLevel(LogLevel minimum) { g_minLevel = minimum; }

void log(LogLevel level, const char* fmt, ...) {
    if (level < g_minLevel) return;
    va_list args;
    va_start(args, fmt);
#ifdef __ANDROID__
    static const int prio[] = {ANDROID_LOG_DEBUG, ANDROID_LOG_INFO, ANDROID_LOG_WARN, ANDROID_LOG_ERROR};
    __android_log_vprint(prio[static_cast<int>(level)], "AS3D", fmt, args);
#else
    static const char* names[] = {"debug", "info", "warn", "error"};
    std::fprintf(stderr, "[%s] ", names[static_cast<int>(level)]);
    std::vfprintf(stderr, fmt, args);
    std::fputc('\n', stderr);
#endif
    va_end(args);
}

void ByteReader::seek(size_t p) {
    if (p > size_) failed_ = true;
    pos_ = p;
}

void ByteReader::skip(size_t n) { seek(pos_ + n); }

bool ByteReader::readBytes(void* out, size_t n) {
    if (failed_ || n > remaining()) {
        failed_ = true;
        std::memset(out, 0, n);
        return false;
    }
    std::memcpy(out, data_ + pos_, n);
    pos_ += n;
    return true;
}

u8 ByteReader::readU8() { u8 v; readBytes(&v, 1); return v; }

u16 ByteReader::readU16() {
    u8 b[2];
    readBytes(b, 2);
    return static_cast<u16>(b[0] | (b[1] << 8));
}

u32 ByteReader::readU32() {
    u8 b[4];
    readBytes(b, 4);
    return static_cast<u32>(b[0]) | (static_cast<u32>(b[1]) << 8) |
           (static_cast<u32>(b[2]) << 16) | (static_cast<u32>(b[3]) << 24);
}

i32 ByteReader::readI32() { return static_cast<i32>(readU32()); }

float ByteReader::readF32() {
    u32 v = readU32();
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

std::string ByteReader::readFixedString(size_t fieldSize) {
    std::string s(fieldSize, '\0');
    readBytes(&s[0], fieldSize);
    s.resize(std::strlen(s.c_str()));
    return s;
}

std::string normalizePath(const std::string& path) {
    std::string out = path;
    for (char& c : out) {
        if (c == '/') c = '\\';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

u32 Rng::next() {
    // xorshift32
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return state_;
}

float Rng::uniform() { return static_cast<float>(next() >> 8) / 16777216.0f; }

float Rng::signedUniform() { return uniform() * 2.0f - 1.0f; }

} // namespace as3d
