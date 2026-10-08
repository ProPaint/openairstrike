#include "ctr_audio.h"

#include <string>

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
    if (!bridge().init(session.vfs(), false)) {
        AS3D_WARN("audio: cannot start, no sound");
        return false;
    }
    started_ = true;
    bridge().setMusicEnabled(music_);
    bridge().audio().setMusicInterpolation(taps_);
    if (bridge().nullDevice()) AS3D_INFO("audio: null device (silent)"); // the ndsp device logs its own line
    if (session.hasLevel()) levelStarted(session);
    return true;
}

void CtrAudio::attach(as3d_game::AudioBridge& b) {
    stop();
    external_ = &b;
    b.setMusicEnabled(music_);
    b.audio().setMusicInterpolation(taps_);
    started_ = b.active();
    if (started_ && b.nullDevice()) AS3D_INFO("audio: null device (silent)");
}

void CtrAudio::precache(as3d_game::GameSession& session) {
    precached_ = 0;
    if (!precache_) return;
    as3d::World& w = session.world();
    // The renderer's level precache (M1.10) computes the set too; without it, compute it here.
    // It only loads (world.h): the simulation is the same with or without it.
    if (!w.precacheSet()) w.precacheScripts();
    const as3d::PrecacheSet* set = w.precacheSet();
    precached_ = set ? bridge().precacheSounds(*set) : 0;
}

void CtrAudio::levelStarted(as3d_game::GameSession& session) {
    if (!started_) return;
    const uint64_t t0 = nowTicks();
    precache(session);
    bridge().startLevel(session.musicPath()); // no module when the music is off (setMusic)
    AS3D_INFO("audio: level start, %d sounds precached, music %s, in %.0f ms", precached_,
              !music_ ? (AS3D_WITH_OPENMPT ? "off (run.ini music=0)" : "off (AS3D_WITH_OPENMPT=OFF)")
                      : (bridge().audio().isMusicPlaying() ? session.musicPath().c_str() : "none"),
              ticksToMs(nowTicks() - t0));
}

void CtrAudio::afterStep(as3d::World& world) {
    bridge().drain(world); // without a device it only clears the queue
}

void CtrAudio::apply() { bridge().setPaused(suspended_ || worldPaused_); }

void CtrAudio::setSuspended(bool suspended) {
    suspended_ = suspended;
    if (started_) apply();
}

void CtrAudio::followPause(bool worldPaused) {
    if (worldPaused == worldPaused_) return;
    worldPaused_ = worldPaused;
    if (started_) apply();
}

void CtrAudio::logSummary() {
    if (!started_) {
        AS3D_INFO("PERF_SUMMARY audio off");
        return;
    }
    const as3d_game::AudioBridge::Stats& b = bridge().stats();
    AS3D_INFO("PERF_SUMMARY audio device=%s played=%llu loops=%llu missing=%llu precached=%d",
              bridge().nullDevice() ? "null" : "ndsp", b.played, b.loopsStarted, b.missing, precached_);
    // The music decoder's cost (M2.4): its time per second of audio decoded, against the plan's
    // budget of 25 % of core 1; the longest decode of one mixer call (512 frames, 15.6 ms of audio).
    const as3d::MusicDecodeStats m = bridge().audio().musicDecodeStats();
    const as3d::audio::ndsp::NdspStats s = as3d::audio::ndsp::stats();
    const double musicSec = static_cast<double>(m.frames) / 32728.0;
    AS3D_INFO("PERF_SUMMARY music decoder=%s decoded_s=%.1f cpu_share=%.1f%% max_decode_ms=%.2f underruns=%lu",
              !music_ ? (AS3D_WITH_OPENMPT ? "off" : "none") : "openmpt", musicSec,
              musicSec > 0 ? 100.0 * static_cast<double>(m.decodeUs) / (musicSec * 1e6) : 0.0,
              m.maxCallUs / 1000.0, static_cast<unsigned long>(s.underruns));
    if (!s.open) return;
    const double sec = static_cast<double>(s.openUs) / 1e6;
    AS3D_INFO("PERF_SUMMARY audio_feed fills=%lu (%.1f/s) underruns=%lu max_fill_ms=%.2f feeder_cpu=%.1f%% dropped=%lu core1=%d",
              static_cast<unsigned long>(s.fills), sec > 0 ? s.fills / sec : 0.0,
              static_cast<unsigned long>(s.underruns), s.maxFillUs / 1000.0,
              s.openUs ? 100.0 * static_cast<double>(s.renderUs) / static_cast<double>(s.openUs) : 0.0,
              static_cast<unsigned long>(s.droppedFrames), s.feederOnCore1 ? 1 : 0);
}

void CtrAudio::stop() {
    if (started_ && !external_) bridge().shutdown();
    started_ = false;
    external_ = nullptr;
}

} // namespace ctrapp
