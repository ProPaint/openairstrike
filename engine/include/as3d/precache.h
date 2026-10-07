// Level-start precache (3DS port, M1.10): everything a loaded level can need, gathered as the
// closure of the definitions it can reach, so that nothing loads on first use during play (on
// the 3DS a first-use load costs a 127 to 355 ms frame). No GL here: the world side
// (World::precacheScripts, world.h) compiles the scripts and loads the simulation's models; the
// renderer (WorldRenderer::precache, world_render.h) loads meshes, skins, particle textures and
// shadows from the same set; the audio side loads the sounds. as3d/load_stats.h proves it.
//
// The closure (collectPrecacheSet), bounded and deduplicated:
//   roots     the level's placements (their object type, drop item and script override), every
//             live entity (definition, model, skin, script, emitter), the game rules' objects
//             (every helicopter, score digit, star item, health bar pieces, lightning effect)
//   object    its model, skin, envmap, skid mark textures, script, and its attachments
//             (particle systems or objects, night-only ones only on a night level)
//   script    its CASH precache list (objects, sounds) and every string of its string table:
//             a string naming an object, particle system or weapon (case-insensitive, as the
//             builtins look them up) joins the closure; a string ending in .mdl, .tga, .wav or
//             .scr is a model, texture, sound or script path (SetModel, SetSkin, PlaySound)
//   weapon    its missile and flash objects
//   particle  its texture
// What it cannot see: a name a script computes at run time (concatenation, numbers), which is
// then loaded lazily and shows in the counters. At most `maxObjects` definitions are taken; more
// sets `capped` and logs a warning.
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace as3d {

class World;
class DefDatabase;
struct ObjectDef;
struct ParticleSystemDef;
namespace script {
class ScriptProgram;
}

struct PrecacheSet {
    std::vector<const ObjectDef*> objects;                 // in discovery order, each once
    std::vector<const ParticleSystemDef*> particleSystems; // each once
    // Normalized game paths, each once. models / textures: those named by scripts and the
    // game's own (objects' own model, skin, envmap are not repeated here).
    std::vector<std::string> scripts, models, textures, sounds;
    bool capped = false;   // the object cap was reached
    int programsScanned = 0;
};

// A precache's work and cost.
struct PrecacheReport {
    int objects = 0;      // definitions in the closure
    int textures = 0;     // texture loads done (ResourceCache, particle textures, UI)
    int models = 0;       // model loads done (GPU meshes and the simulation's copies)
    int scripts = 0;      // programs compiled
    int shadows = 0;      // shadow silhouettes generated
    int sounds = 0;       // sounds decoded
    long long bytes = 0;  // bytes read through the Vfs
    float seconds = 0.0f; // wall clock
    bool capped = false;

    PrecacheReport& operator+=(const PrecacheReport& o);
    // "objects=N textures=N models=N scripts=N shadows=N sounds=N bytes=N seconds=S[ capped]".
    std::string str() const;
};

constexpr size_t kPrecacheMaxObjects = 2048;

// The program at a normalized script path, or null (missing, or not to be loaded).
using PrecacheProgramFn = std::function<const script::ScriptProgram*(const std::string& path)>;

// The closure above for the world's current level. `program` gives the scripts to scan (World::
// precacheScripts passes the compiling GameScriptHost::program); null programs are skipped, so
// their names stay unseen.
PrecacheSet collectPrecacheSet(const World& world, const DefDatabase& db, const PrecacheProgramFn& program,
                               size_t maxObjects = kPrecacheMaxObjects);

} // namespace as3d
