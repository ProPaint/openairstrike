// Load counters (3DS port, M1.10): how many resources were loaded since the last reset, per
// kind, so a level-start precache (WorldRenderer::precache, World::precacheScripts) can be shown
// to leave nothing for the frames that follow. On the 3DS a first-use load costs a 127 to 355 ms
// frame (docs/3ds-port.md); after a precache the counters must stay at zero during play.
//
// Counting sites (each counts one load when its cache misses; a file that fails to load counts
// once):
//   Texture  ResourceCache::texture, UiAssets::texture / textureRepeat
//   Model    ResourceCache::mesh (GPU mesh), World::loadModel (the simulation's copy)
//   Script   GameScriptHost::program (a script compiled from the pak)
//   Sound    Audio::loadSound (a WAV decoded)
//   Shadow   WorldRenderer's late shadow silhouettes (lateShadowMaps())
//   Other    any Vfs::read outside the sites above: the catch-all for loaders without a counter
//            of their own (ParticleRenderer's per-emitter textures, the terrain, music, the
//            front end's files)
// `bytes` sums every Vfs::read, counted site or not.
//
// The counters are process-wide (one game runs per process) and atomic: read them with
// LoadStats::lazyLoads(), clear them with LoadStats::reset(). The pattern: load the level,
// precache, LoadStats::reset(), play; lazyLoads() then holds the lazy loads of play. Counting
// changes no behaviour.
#pragma once

#include <cstddef>
#include <string>

namespace as3d {

enum class LoadKind : int { Texture = 0, Model, Script, Sound, Shadow, Other };
constexpr int kLoadKindCount = 6;
// "textures", "models", "scripts", "sounds", "shadows", "other".
const char* loadKindName(LoadKind kind);

struct LoadCounts {
    long long count[kLoadKindCount] = {};
    long long bytes = 0; // bytes read through Vfs::read

    long long of(LoadKind k) const { return count[static_cast<int>(k)]; }
    long long total() const;
    // "textures=N models=N scripts=N sounds=N shadows=N other=N bytes=N".
    std::string str() const;
    // Per-kind difference (this - since), e.g. the loads a precache did.
    LoadCounts operator-(const LoadCounts& since) const;
};

class LoadStats {
public:
    // One load of `kind` (a cache miss at a counting site).
    static void count(LoadKind kind);
    // Called by Vfs::read for every successful read: adds the bytes, and counts one Other load
    // when the read is not inside a Site.
    static void fileRead(size_t bytes);
    // The counts since the last reset (or the start of the process).
    static LoadCounts lazyLoads();
    static void reset();

    // Marks a counting site for its lifetime (per thread, nestable): the Vfs reads inside it are
    // the site's own load and are not counted again as Other.
    class Site {
    public:
        Site();
        ~Site();
        Site(const Site&) = delete;
        Site& operator=(const Site&) = delete;
    };
};

} // namespace as3d
