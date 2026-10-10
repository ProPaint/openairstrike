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
//   Stamp    a TerraMorph stamp (morphmaps\*.tga, the sequels) read by World::terraMorph. Not
//            precachable: the lookup compares the script's name byte for byte with the stored
//            "<name>.tga" (the original's), so a call by a name without the extension reads the
//            file every time, and filling the stamp table ahead would change its order and cap.
//   Other    any Vfs::read outside the sites above: the catch-all for loaders without a counter
//            of their own (ParticleRenderer's per-emitter textures, the terrain, music, the
//            front end's files)
// `bytes` sums every Vfs::read, counted site or not.
//
// The lazy-load log (3DS run.ini log_lazy=1, as3d_game --log-lazy): with setLogLazy(true), every
// load counted after a reset() and outside a PrecacheScope logs one line
//   AS3D_LAZY path=<game path> kind=<kind> site=<counting site> frame=<setFrame value>
// (the Other bucket names the Vfs path), so a counter that is not zero after a precache says what
// the closure missed. Logging changes no behaviour and no counter.
//
// The counters are process-wide (one game runs per process) and atomic: read them with
// LoadStats::lazyLoads(), clear them with LoadStats::reset(). The pattern: load the level,
// precache, LoadStats::reset(), play; lazyLoads() then holds the lazy loads of play. Counting
// changes no behaviour.
#pragma once

#include <cstddef>
#include <string>

namespace as3d {

enum class LoadKind : int { Texture = 0, Model, Script, Sound, Shadow, Stamp, Other };
constexpr int kLoadKindCount = 7;
// "textures", "models", "scripts", "sounds", "shadows", "stamps", "other".
const char* loadKindName(LoadKind kind);

struct LoadCounts {
    long long count[kLoadKindCount] = {};
    long long bytes = 0; // bytes read through Vfs::read

    long long of(LoadKind k) const { return count[static_cast<int>(k)]; }
    long long total() const;
    // "textures=N models=N scripts=N sounds=N shadows=N stamps=N other=N bytes=N".
    std::string str() const;
    // Per-kind difference (this - since), e.g. the loads a precache did.
    LoadCounts operator-(const LoadCounts& since) const;
};

class LoadStats {
public:
    // One load of `kind` (a cache miss at a counting site); `path` and `site` (a short name of
    // the counting site, e.g. "resource_cache") are only used by the lazy-load log.
    static void count(LoadKind kind, const char* path = nullptr, const char* site = nullptr);
    static void count(LoadKind kind, const std::string& path, const char* site) { count(kind, path.c_str(), site); }
    // Called by Vfs::read for every successful read: adds the bytes, and counts one Other load
    // when the read is not inside a Site.
    static void fileRead(size_t bytes, const char* path = nullptr);
    // The counts since the last reset (or the start of the process).
    static LoadCounts lazyLoads();
    static void reset();

    // The lazy-load log (see above). setFrame: the frame number the log lines carry.
    static void setLogLazy(bool on);
    static bool logLazy();
    static void setFrame(long frame);
    static long frame();
    // The path of the last load counted on this thread (count()'s `path`; "" before any): the
    // PICA's late texture queue names it in AS3D_LATE_TEX.
    static const char* lastPath();

    // Marks a level-start precache (process-wide, nestable): its loads are expected, they are
    // counted as usual but never logged as lazy.
    class PrecacheScope {
    public:
        PrecacheScope();
        ~PrecacheScope();
        PrecacheScope(const PrecacheScope&) = delete;
        PrecacheScope& operator=(const PrecacheScope&) = delete;
    };

    // A level load (World::loadLevel: the map, the placements' scripts and models), behind the
    // loading screen and never inside play: its loads are neither logged nor kept in the
    // counters (they are put back as they were when the scope ends), so that the window of the
    // level before (the front end's attract level) holds only that window's own loads.
    class LevelLoadScope {
    public:
        LevelLoadScope();
        ~LevelLoadScope();
        LevelLoadScope(const LevelLoadScope&) = delete;
        LevelLoadScope& operator=(const LevelLoadScope&) = delete;

    private:
        LoadCounts saved_;
        PrecacheScope quiet_;
    };

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
