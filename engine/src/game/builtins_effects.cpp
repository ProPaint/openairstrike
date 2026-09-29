// Light and sound builtins (docs/spec/rcsl-builtins-semantics.md family E). The world is
// headless: lights go to a per-frame queue (at most 32, cleared at frame start) for the
// renderer, sounds to an event queue for the audio layer.
#include "builtins_common.h"

namespace as3d {
namespace builtins {

namespace {

void bPlaceLight(BuiltinArgs& a, void*) {
    float pos[3], col[3];
    if (!vecArg(a, 0, pos) || !vecArg(a, 1, col)) return;
    worldOf(a).placeLight(Vec3{pos[0], pos[1], pos[2]}, Vec3{col[0], col[1], col[2]}, a.f32(2));
}

void bStartSound(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    const char* file = strArg(a, 0);
    int s = selfOf(w);
    if (s < 0 || !file) return;
    w.queueSound(SoundEvent::Kind::Play, s, file);
}

void bStartLoopingSound(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int s = selfOf(w);
    if (s < 0) return;
    Entity& e = w.entity(s);
    if (!e.loopSound.empty()) {
        w.queueSound(SoundEvent::Kind::StopLoop, s, e.loopSound);
        e.loopSound.clear();
    }
    const char* file = strArg(a, 0);
    if (!file) return;
    e.loopSound = file;
    w.queueSound(SoundEvent::Kind::Loop, s, file);
}

void bStopLoopingSound(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int s = selfOf(w);
    if (s < 0) return;
    Entity& e = w.entity(s);
    if (e.loopSound.empty()) return;
    w.queueSound(SoundEvent::Kind::StopLoop, s, e.loopSound);
    e.loopSound.clear();
}

const BuiltinDesc kTable[] = {
    {"PlaceLight", 3, bPlaceLight, nullptr, BuiltinStatus::Implemented},
    {"StartSound", 1, bStartSound, nullptr, BuiltinStatus::Implemented},
    {"StartLoopingSound", 1, bStartLoopingSound, nullptr, BuiltinStatus::Implemented},
    {"StopLoopingSound", 0, bStopLoopingSound, nullptr, BuiltinStatus::Implemented},
};

} // namespace

Family effectsBuiltins() { return {kTable, sizeof kTable / sizeof kTable[0]}; }

} // namespace builtins
} // namespace as3d
