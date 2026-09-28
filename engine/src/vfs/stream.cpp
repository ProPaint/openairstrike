// IStream backends: plain files and in-memory blobs.
#include <cstdio>
#include <cstring>
#include <memory>

#include "as3d/vfs.h"

namespace as3d {
namespace {

class FileStream : public IStream {
public:
    FileStream(std::FILE* file, size_t size) : file_(file), size_(size) {}
    ~FileStream() override {
        if (file_) std::fclose(file_);
    }

    size_t size() override { return size_; }

    bool readAt(size_t offset, void* out, size_t n) override {
        if (n == 0) return true;
        if (offset > size_ || n > size_ - offset) return false;
        if (std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0) return false;
        return std::fread(out, 1, n, file_) == n;
    }

private:
    std::FILE* file_;
    size_t size_;
};

class MemoryStream : public IStream {
public:
    explicit MemoryStream(Blob data) : data_(std::move(data)) {}

    size_t size() override { return data_.size(); }

    bool readAt(size_t offset, void* out, size_t n) override {
        if (n == 0) return true;
        if (offset > data_.size() || n > data_.size() - offset) return false;
        std::memcpy(out, data_.data() + offset, n);
        return true;
    }

private:
    Blob data_;
};

} // namespace

std::unique_ptr<IStream> openFileStream(const std::string& osPath) {
    std::FILE* f = std::fopen(osPath.c_str(), "rb");
    if (!f) return nullptr;
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return nullptr;
    }
    long n = std::ftell(f);
    if (n < 0) {
        std::fclose(f);
        return nullptr;
    }
    std::fseek(f, 0, SEEK_SET);
    return std::unique_ptr<IStream>(new FileStream(f, static_cast<size_t>(n)));
}

std::unique_ptr<IStream> makeMemoryStream(Blob data) {
    return std::unique_ptr<IStream>(new MemoryStream(std::move(data)));
}

} // namespace as3d
