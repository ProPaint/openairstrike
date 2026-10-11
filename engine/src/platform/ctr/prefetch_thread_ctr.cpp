// The 3DS's PrefetchThreading (as3d::Vfs::prefetchAsync, docs/3ds-port.md "Prefetch overlap
// (round 4, R4.10)"): a libctru thread for the read plan's reads while the main thread parses.
// The thread spends its life blocked in FSFILE_Read (the FS service and the SD card do the work)
// and copies each run's files out when a read returns, so it goes where the audio feeder goes
// (audio_device_ndsp.cpp): core 1 (the system core, the application's 30 % share set once by
// APT_SetAppCpuTimeLimit), one priority step above its creator so that, when core 1 is refused
// and it falls back to core 0, a returned read is handled at once instead of after the main
// thread's parse (the 3DS kernel never time-slices between priorities). LightLocks for the two
// locks, a CondVar for the waits.
#include <3ds.h>

#include <cstdio>
#include <memory>

#include "as3d/core.h"
#include "ctr_context.h"

namespace as3d {
namespace ctr {
namespace {

constexpr size_t kStackBytes = 32 * 1024;
constexpr int kAppCpuLimit = 30; // the ndsp device's share of core 1 (audio_device_ndsp.cpp)

class CtrPrefetchThreading final : public PrefetchThreading {
public:
    CtrPrefetchThreading() {
        LightLock_Init(&data_);
        LightLock_Init(&stream_);
        CondVar_Init(&cv_);
        std::snprintf(desc_, sizeof desc_, "none");
    }
    ~CtrPrefetchThreading() override { join(); }

    bool start(void (*fn)(void*), void* arg) override {
        join();
        s32 prio = 0x30;
        svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
        prio = prio > 0x18 ? prio - 1 : 0x18; // one step above the creator (lower is higher)
        int core = 1;
        thread_ = threadCreate(fn, arg, kStackBytes, prio, core, false);
        if (!thread_) {
            // Core 1 takes application threads only once the application has a share of it
            // (the audio device sets it when it opens; with audio=0 nobody has yet).
            APT_SetAppCpuTimeLimit(kAppCpuLimit);
            thread_ = threadCreate(fn, arg, kStackBytes, prio, core, false);
        }
        if (!thread_) {
            core = -2; // the application's default core
            thread_ = threadCreate(fn, arg, kStackBytes, prio, core, false);
        }
        if (!thread_) {
            AS3D_WARN("prefetch: cannot create the reader thread, reading on the main thread");
            std::snprintf(desc_, sizeof desc_, "refused");
            return false;
        }
        std::snprintf(desc_, sizeof desc_, "core %d prio 0x%lX", core == 1 ? 1 : 0, static_cast<unsigned long>(prio));
        return true;
    }
    void join() override {
        if (!thread_) return;
        threadJoin(thread_, U64_MAX);
        threadFree(thread_);
        thread_ = nullptr;
    }
    const char* description() const override { return desc_; }

    void lock() override { LightLock_Lock(&data_); }
    void unlock() override { LightLock_Unlock(&data_); }
    void wait() override { CondVar_Wait(&cv_, &data_); }
    void notifyAll() override { CondVar_Broadcast(&cv_); }
    void lockStream() override { LightLock_Lock(&stream_); }
    void unlockStream() override { LightLock_Unlock(&stream_); }

private:
    LightLock data_, stream_;
    CondVar cv_;
    Thread thread_ = nullptr;
    char desc_[32];
};

} // namespace

std::unique_ptr<PrefetchThreading> makePrefetchThreading() {
    return std::unique_ptr<PrefetchThreading>(new CtrPrefetchThreading());
}

} // namespace ctr
} // namespace as3d
