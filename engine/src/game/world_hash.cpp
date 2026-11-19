// World::stateHash and stateHashFull (as3d/world.h, docs/spec/issues/180-online-coop.md 4.4):
// FNV-1a 64 over the simulation's cheap identifying state in a fixed order (frame, RNG, time,
// map position, level flags, the camera, the players, the entities in use, the particles), sent
// every kHashEvery frames by online peers; the full form hashes dumpStateJson() for the tests.
//
// Floats go in as their bits, except that every NaN counts as the canonical one: the sign and
// payload of a NaN made by arithmetic differ between x86, ARM and wasm, and the simulation
// behaves alike whichever NaN it holds (every comparison is false), so they must not look like
// a desync. Everything is fed byte by byte, little-endian, so the hash does not depend on the
// memory layout.
#include <string>

#include "as3d/world.h"
#include "as3d/world_particles.h"
#include "world_internal.h"

namespace as3d {

namespace {

constexpr u64 kFnvOffset = 0xcbf29ce484222325ull;
constexpr u64 kFnvPrime = 0x100000001b3ull;

struct Fnv {
    u64 h = kFnvOffset;
    void u32v(u32 v) {
        for (int k = 0; k < 4; ++k) {
            h ^= (v >> (8 * k)) & 0xff;
            h *= kFnvPrime;
        }
    }
    void i32v(int v) { u32v(static_cast<u32>(v)); }
    void flag(bool b) { u32v(b ? 1u : 0u); }
    void fbitsv(u32 bits) {
        // A NaN (exponent all ones, mantissa non-zero) as the canonical quiet NaN.
        if ((bits & 0x7f800000u) == 0x7f800000u && (bits & 0x007fffffu) != 0) bits = 0x7fc00000u;
        u32v(bits);
    }
    void f32v(float v) { fbitsv(fbits(v)); }
};

} // namespace

u64 World::stateHash() const {
    Fnv h;
    h.u32v(frame_);
    // The RNG's state, through the next value of a copy (xorshift32's step is a bijection).
    Rng rng = rng_;
    h.u32v(rng.next());
    h.f32v(time_);
    h.f32v(mapPos_);
    h.flag(levelComplete_);
    h.flag(gameOver_);
    h.flag(paused_);
    h.flag(hintShowing_);
    h.flag(intermission_);
    for (int k = 0; k < kCameraFieldCount; ++k) h.f32v(camera_.field[k]);

    h.i32v(config_.players);
    for (int p = 0; p < config_.players; ++p) {
        const PlayerRecord& pr = players_[p];
        const int pi = playerEntityIndex(p);
        h.i32v(pi);
        if (pi >= 0) {
            const Entity& pe = ents_[static_cast<size_t>(pi)];
            for (int k = 0; k < 3; ++k) h.fbitsv(pe.fields[F_ORIGIN + k]);
            h.fbitsv(pe.fields[F_HEALTH]);
        }
        h.f32v(pr.scores);
        h.f32v(pr.lives);
        h.f32v(pr.action);
        h.f32v(pr.weapon);
        for (int k = 0; k < 20; ++k) h.i32v(pr.upgrades[k]);
        for (int k = 0; k < 5; ++k) h.i32v(pr.missiles[k]);
        for (int k = 0; k < 16; ++k) h.i32v(pr.powerups[k]);
    }

    h.i32v(slotsInUse_);
    h.i32v(listCount_);
    for (int i = 0; i < kMaxEntitySlots; ++i) {
        const Entity& e = ents_[static_cast<size_t>(i)];
        if (!e.inUse) continue;
        h.i32v(i);
        for (int k = 0; k < 3; ++k) h.fbitsv(e.fields[F_ORIGIN + k]);
        for (int k = 0; k < 3; ++k) h.fbitsv(e.fields[F_ANGLES + k]);
        h.fbitsv(e.fields[F_HEALTH]);
        h.fbitsv(e.fields[F_FLAGS]);
        h.i32v(e.state);
    }

    h.i32v(particles_->liveParticles());
    h.u32v(particles_->stateHash());
    return h.h;
}

u64 World::stateHashFull() const {
    const std::string dump = dumpStateJson();
    u64 h = kFnvOffset;
    for (char c : dump) {
        h ^= static_cast<u8>(c);
        h *= kFnvPrime;
    }
    return h;
}

} // namespace as3d
