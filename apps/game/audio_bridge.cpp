#include "audio_bridge.h"

#include <algorithm>

namespace as3d_game {

using namespace as3d;

namespace {
constexpr float kSfxVolume = 0.5f;   // config.ini SfxVolume as shipped
constexpr float kMusicVolume = 0.5f; // config.ini MusicVolume as shipped
constexpr size_t kMaxLoops = 32;     // entity-attached channels (engine-behaviour.md 12)
} // namespace

bool AudioBridge::init(Vfs& vfs, bool nullDevice) {
    shutdown();
    null_ = nullDevice;
    ok_ = audio_.init(vfs, nullDevice ? AudioBackend::Null : AudioBackend::Sdl);
    if (!ok_ && !nullDevice) {
        AS3D_WARN("audio: no output device, continuing silent");
        null_ = true;
        ok_ = audio_.init(vfs, AudioBackend::Null);
    }
    if (ok_) {
        audio_.setSfxVolume(kSfxVolume);
        audio_.setMusicVolume(kMusicVolume);
    }
    return ok_;
}

void AudioBridge::shutdown() {
    if (ok_) audio_.shutdown();
    ok_ = false;
    loops_.clear();
}

void AudioBridge::startLevel(const std::string& musicPath) {
    if (!ok_) return;
    audio_.stopAll();
    audio_.stopMusic();
    loops_.clear();
    if (!musicPath.empty() && !audio_.playMusic(musicPath.c_str(), true))
        AS3D_WARN("audio: cannot play music '%s'", musicPath.c_str());
}

void AudioBridge::stopLoopsOf(const EntityHandle& h, const std::string* sample) {
    for (size_t i = 0; i < loops_.size();) {
        if (loops_[i].entity == h && (!sample || normalizePath(loops_[i].sample) == normalizePath(*sample))) {
            audio_.stop(loops_[i].voice);
            ++stats_.loopsStopped;
            loops_.erase(loops_.begin() + static_cast<long>(i));
        } else {
            ++i;
        }
    }
}

void AudioBridge::drain(World& world) {
    std::vector<SoundEvent>& q = world.soundEvents();
    if (!ok_) {
        q.clear();
        return;
    }
    for (const SoundEvent& ev : q) {
        switch (ev.kind) {
            case SoundEvent::Kind::Play: {
                SoundId id = audio_.loadSound(ev.sample.c_str());
                if (id == kInvalidSoundId) {
                    ++stats_.missing;
                    break;
                }
                audio_.play(id);
                ++stats_.played;
                break;
            }
            case SoundEvent::Kind::Loop: {
                stopLoopsOf(ev.entity, nullptr); // one loop per entity (+0x73)
                SoundId id = audio_.loadSound(ev.sample.c_str());
                if (id == kInvalidSoundId) {
                    ++stats_.missing;
                    break;
                }
                if (loops_.size() >= kMaxLoops) {
                    audio_.stop(loops_.front().voice);
                    loops_.erase(loops_.begin());
                }
                PlayParams p;
                p.loop = true;
                loops_.push_back({ev.entity, ev.sample, audio_.play(id, p)});
                ++stats_.loopsStarted;
                break;
            }
            case SoundEvent::Kind::StopLoop: stopLoopsOf(ev.entity, &ev.sample); break;
        }
    }
    q.clear();
    // A loop dies with its entity.
    for (size_t i = 0; i < loops_.size();) {
        Entity* e = world.get(loops_[i].entity);
        if (!e || (e->rt & RT_REMOVED)) {
            audio_.stop(loops_[i].voice);
            ++stats_.loopsStopped;
            loops_.erase(loops_.begin() + static_cast<long>(i));
        } else {
            ++i;
        }
    }
}

void AudioBridge::setPaused(bool paused) {
    if (!ok_ || paused == paused_) return;
    paused_ = paused;
    audio_.pause(paused);
}

void AudioBridge::pump(int frames) {
    if (!ok_ || !null_ || frames <= 0) return;
    frames = std::min(frames, 16384);
    scratch_.resize(static_cast<size_t>(frames) * 2);
    audio_.render(scratch_.data(), frames);
}

} // namespace as3d_game
