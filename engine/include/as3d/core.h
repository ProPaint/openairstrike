// Core types shared by every module. Owned by the orchestrator.
#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace as3d {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using i16 = std::int16_t;
using i32 = std::int32_t;

// Owned contents of one file.
using Blob = std::vector<u8>;

enum class LogLevel { Debug, Info, Warn, Error };

// printf-style logging. Goes to stderr on desktop and logcat on Android.
void log(LogLevel level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void setLogLevel(LogLevel minimum);

#define AS3D_INFO(...) ::as3d::log(::as3d::LogLevel::Info, __VA_ARGS__)
#define AS3D_WARN(...) ::as3d::log(::as3d::LogLevel::Warn, __VA_ARGS__)
#define AS3D_ERROR(...) ::as3d::log(::as3d::LogLevel::Error, __VA_ARGS__)

// Bounds-checked little-endian reader over a byte range. Reads past the end
// return zero and set failed(), so loaders check once at the end.
class ByteReader {
public:
    ByteReader(const u8* data, size_t size) : data_(data), size_(size) {}
    explicit ByteReader(const Blob& b) : data_(b.data()), size_(b.size()) {}

    size_t pos() const { return pos_; }
    size_t size() const { return size_; }
    size_t remaining() const { return pos_ <= size_ ? size_ - pos_ : 0; }
    bool failed() const { return failed_; }
    void seek(size_t p);
    void skip(size_t n);

    u8 readU8();
    u16 readU16();
    u32 readU32();
    i32 readI32();
    float readF32();
    bool readBytes(void* out, size_t n);
    // Fixed-size field holding a NUL-terminated string.
    std::string readFixedString(size_t fieldSize);

private:
    const u8* data_;
    size_t size_;
    size_t pos_ = 0;
    bool failed_ = false;
};

// Lower-cases and converts '/' to '\\', the canonical form for game paths.
std::string normalizePath(const std::string& path);

// Deterministic random numbers for the simulation.
class Rng {
public:
    explicit Rng(u32 seed = 1) : state_(seed ? seed : 1) {}
    u32 next();
    float uniform();   // [0, 1)
    float signedUniform(); // [-1, 1)
private:
    u32 state_;
};

} // namespace as3d
