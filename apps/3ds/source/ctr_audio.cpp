#include "ctr_audio.h"

#include "as3d/core.h"
#include "as3d/precache.h"
#include "as3d/world.h"
#include "audio_device_ndsp.h"
#include "ctr_perf.h"
#include "game_session.h"

namespace ctrapp {

bool CtrAudio::start(as3d_game::GameSession& session, bool precache) {
    stop();
    precache_ = precache;
    if (!bridge_.init(session.vfs(), false)) {
        AS3D_WARN("audio: cannot start, no sound");
        return false;
    }
    started_ = true;
    AS3D_INFO("audio: %s", bridge_.nullDevice() ? "null device (silent)" : as3d::audio::ndsp::description());
    if (session.hasLevel()) levelStarted(session);
    return true;
}

void CtrAudio::precache(as3d_game::GameSession& session) {
    precached_ = 0;
    if (!precache_) return;
    as3d::World& w = session.world();
    // The renderer's level precache (M1.10) computes the set too; without it, compute it here.
    // It only loads (world.h): the simulation is the same with or without it.
    if (!w.precacheSet()) w.precacheScripts();
    const as3d::PrecacheSet* set = w.precacheSet();
    precached_ = set ? bridge_.precacheSounds(*set) : 0;
}

void CtrAudio::levelStarted(as3d_game::GameSession& session) {
    if (!started_) return;
    const uint64_t t0 = nowTicks();
    precache(session);
    bridge_.startLevel(session.musicPath()); // music: the stub fails cleanly (M2.4)
    AS3D_INFO("audio: level start, %d sounds precached in %.0f ms", precached_, ticksToMs(nowTicks() - t0));
}

void CtrAudio::afterStep(as3d::World& world) {
    bridge_.drain(world); // without a device it only clears the queue
}

void CtrAudio::apply() { bridge_.setPaused(suspended_ || worldPaused_); }

void CtrAudio::setSuspended(bool suspended) {
    suspended_ = suspended;
    if (started_) apply();
}

void CtrAudio::followPause(bool worldPaused) {
    if (worldPaused == worldPaused_) return;
    worldPaused_ = worldPaused;
    if (started_) apply();
}

void CtrAudio::logSummary() const {
    if (!started_) {
        AS3D_INFO("PERF_SUMMARY audio off");
        return;
    }
    const as3d_game::AudioBridge::Stats& b = bridge_.stats();
    AS3D_INFO("PERF_SUMMARY audio device=%s played=%llu loops=%llu missing=%llu precached=%d",
              bridge_.nullDevice() ? "null" : "ndsp", b.played, b.loopsStarted, b.missing, precached_);
    const as3d::audio::ndsp::NdspStats s = as3d::audio::ndsp::stats();
    if (!s.open) return;
    const double sec = static_cast<double>(s.openUs) / 1e6;
    AS3D_INFO("PERF_SUMMARY audio_feed fills=%lu (%.1f/s) underruns=%lu max_fill_ms=%.2f feeder_cpu=%.1f%% dropped=%lu core1=%d",
              static_cast<unsigned long>(s.fills), sec > 0 ? s.fills / sec : 0.0,
              static_cast<unsigned long>(s.underruns), s.maxFillUs / 1000.0,
              s.openUs ? 100.0 * static_cast<double>(s.renderUs) / static_cast<double>(s.openUs) : 0.0,
              static_cast<unsigned long>(s.droppedFrames), s.feederOnCore1 ? 1 : 0);
}

void CtrAudio::stop() {
    if (started_) bridge_.shutdown();
    started_ = false;
}

} // namespace ctrapp
