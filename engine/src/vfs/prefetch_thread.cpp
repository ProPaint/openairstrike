// The desktop's PrefetchThreading (Vfs::prefetchAsync): std::thread, two mutexes and a
// condition. The 3DS has none of these in its C++ library (its threading is
// engine/src/platform/ctr/prefetch_thread_ctr.cpp, on libctru); the web app never reads ahead on
// a thread. docs/3ds-port.md "Prefetch overlap (round 4, R4.10)".
#include <memory>

#include "as3d/vfs.h"

#if !defined(__3DS__) && !defined(__EMSCRIPTEN__)
#define AS3D_STD_PREFETCH_THREAD 1
#include <condition_variable>
#include <mutex>
#include <thread>
#else
#define AS3D_STD_PREFETCH_THREAD 0
#endif

namespace as3d {

#if AS3D_STD_PREFETCH_THREAD
namespace {

class StdPrefetchThreading final : public PrefetchThreading {
public:
    ~StdPrefetchThreading() override { join(); }
    bool start(void (*fn)(void*), void* arg) override {
        join();
        thread_ = std::thread(fn, arg);
        return true;
    }
    void join() override {
        if (thread_.joinable()) thread_.join();
    }
    const char* description() const override { return "std::thread"; }
    void lock() override { data_.lock(); }
    void unlock() override { data_.unlock(); }
    void wait() override {
        std::unique_lock<std::mutex> held(data_, std::adopt_lock); // the caller holds it
        cv_.wait(held);
        held.release(); // still held, the caller unlocks
    }
    void notifyAll() override { cv_.notify_all(); }
    void lockStream() override { stream_.lock(); }
    void unlockStream() override { stream_.unlock(); }

private:
    std::mutex data_, stream_;
    std::condition_variable cv_;
    std::thread thread_;
};

} // namespace
#endif

std::unique_ptr<PrefetchThreading> makeStdPrefetchThreading() {
#if AS3D_STD_PREFETCH_THREAD
    return std::unique_ptr<PrefetchThreading>(new StdPrefetchThreading());
#else
    return nullptr;
#endif
}

} // namespace as3d
