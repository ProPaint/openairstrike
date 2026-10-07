// The level-start precache closure and the world side of the precache (as3d/precache.h,
// World::precacheScripts in as3d/world.h).
#include "as3d/precache.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <unordered_set>

#include "as3d/defs.h"
#include "as3d/level.h"
#include "as3d/load_stats.h"
#include "as3d/script.h"
#include "as3d/script_host.h"
#include "as3d/terrain.h"
#include "as3d/world.h"

namespace as3d {

PrecacheReport& PrecacheReport::operator+=(const PrecacheReport& o) {
    objects = std::max(objects, o.objects);
    textures += o.textures;
    models += o.models;
    scripts += o.scripts;
    shadows += o.shadows;
    sounds += o.sounds;
    bytes += o.bytes;
    seconds += o.seconds;
    capped = capped || o.capped;
    return *this;
}

std::string PrecacheReport::str() const {
    char buf[256];
    std::snprintf(buf, sizeof buf, "objects=%d textures=%d models=%d scripts=%d shadows=%d sounds=%d bytes=%lld seconds=%.3f%s",
                  objects, textures, models, scripts, shadows, sounds, bytes, static_cast<double>(seconds),
                  capped ? " capped" : "");
    return buf;
}

namespace {

bool endsWith(const std::string& s, const char* suffix) {
    const size_t n = std::char_traits<char>::length(suffix);
    return s.size() > n && s.compare(s.size() - n, n, suffix) == 0;
}

class Closure {
public:
    Closure(const World& world, const DefDatabase& db, const PrecacheProgramFn& program, size_t maxObjects, PrecacheSet& out)
        : world_(world), db_(db), program_(program), max_(maxObjects), out_(out) {}

    void object(const ObjectDef* def) {
        if (!def || !objects_.insert(def).second) return;
        if (out_.objects.size() >= max_) {
            out_.capped = true;
            return;
        }
        out_.objects.push_back(def);
        pending_.push_back(def);
    }
    void object(const std::string& name) {
        if (!name.empty()) object(db_.findObject(name));
    }
    void particles(const ParticleSystemDef* ps) {
        if (ps && particles_.insert(ps).second) out_.particleSystems.push_back(ps);
    }
    void weapon(const WeaponDef* w) {
        if (!w || !weapons_.insert(w).second) return;
        object(w->missileName);
        object(w->flashName);
    }
    void script(const std::string& path) {
        if (path.empty()) return;
        std::string p = normalizePath(path);
        if (!scripts_.insert(p).second) return;
        out_.scripts.push_back(p);
        pendingScripts_.push_back(p);
    }
    void path(std::unordered_set<std::string>& seen, std::vector<std::string>& list, const std::string& raw) {
        if (raw.empty()) return;
        std::string p = normalizePath(raw);
        if (seen.insert(p).second) list.push_back(std::move(p));
    }
    void model(const std::string& p) { path(models_, out_.models, p); }
    void texture(const std::string& p) { path(textures_, out_.textures, p); }
    void sound(const std::string& p) { path(sounds_, out_.sounds, p); }

    // A string of a script: a definition name or a resource path.
    void name(const std::string& s) {
        if (s.empty() || s.size() > 260) return;
        if (const ObjectDef* o = db_.findObject(s)) object(o);
        if (const ParticleSystemDef* ps = db_.findParticleSystem(s)) particles(ps);
        if (const WeaponDef* w = db_.findWeapon(s)) weapon(w);
        const std::string p = normalizePath(s);
        if (endsWith(p, ".mdl")) model(p);
        else if (endsWith(p, ".tga")) texture(p);
        else if (endsWith(p, ".wav")) sound(p);
        else if (endsWith(p, ".scr")) script(p);
    }

    void run() {
        const bool night = world_.night();
        while (!pending_.empty() || !pendingScripts_.empty()) {
            while (!pending_.empty()) {
                const ObjectDef* def = pending_.front();
                pending_.pop_front();
                for (const SkidMarkDef& sk : def->skidMarks) texture(sk.texture);
                script(def->script);
                for (const AttachDef& at : def->attachments) {
                    if (at.nightOnly && !night) continue;
                    // Particle systems resolve first (obj.md "attach"), as World::buildEntity.
                    if (const ParticleSystemDef* ps = db_.findParticleSystem(at.targetName)) particles(ps);
                    else object(at.targetName);
                }
            }
            if (!pendingScripts_.empty()) {
                const std::string path = pendingScripts_.front();
                pendingScripts_.pop_front();
                const script::ScriptProgram* prog = program_ ? program_(path) : nullptr;
                if (!prog) continue;
                ++out_.programsScanned;
                for (const script::CashEntry& c : prog->cash()) {
                    if (c.kind == static_cast<u8>(script::CashKind::Sound)) sound(c.name);
                    else name(c.name);
                }
                const std::vector<u8>& strg = prog->strg();
                size_t start = 0;
                for (size_t i = 0; i < strg.size(); ++i) {
                    if (strg[i] != 0) continue;
                    if (i > start) name(std::string(reinterpret_cast<const char*>(strg.data()) + start, i - start));
                    start = i + 1;
                }
            }
        }
    }

private:
    const World& world_;
    const DefDatabase& db_;
    const PrecacheProgramFn& program_;
    size_t max_;
    PrecacheSet& out_;
    std::unordered_set<const ObjectDef*> objects_;
    std::unordered_set<const ParticleSystemDef*> particles_;
    std::unordered_set<const WeaponDef*> weapons_;
    std::unordered_set<std::string> scripts_, models_, textures_, sounds_;
    std::deque<const ObjectDef*> pending_;
    std::deque<std::string> pendingScripts_;
};

} // namespace

PrecacheSet collectPrecacheSet(const World& world, const DefDatabase& db, const PrecacheProgramFn& program, size_t maxObjects) {
    PrecacheSet set;
    Closure c(world, db, program, maxObjects, set);
    // The level's placements.
    if (const Terrain* t = world.terrain()) {
        if (const LevelData* level = t->level()) {
            for (const Placement& p : level->placements) {
                c.object(level->typeName(p));
                if (const std::string* item = level->itemName(p)) c.object(*item);
                c.script(p.scriptOverride);
            }
        }
    }
    // The live entities (the players, what the load-time pass spawned).
    for (int i = 0; i < kMaxEntitySlots; ++i) {
        if (!world.validIndex(i)) continue;
        const Entity& e = world.entity(i);
        c.object(e.def);
        c.object(e.drop);
        c.particles(e.emitter);
        c.model(e.modelPath);
        c.texture(e.skinPath);
        c.script(e.scriptPath);
    }
    // The objects the engine itself spawns by name (game_profile.h).
    const GameRules& rl = world.rules();
    for (int h = 0; rl.heliObjects && h < rl.helicopterCount; ++h)
        if (rl.heliObjects[h]) c.object(rl.heliObjects[h]);
    const char* named[] = {rl.scoreDigitObject, rl.starItemObject, rl.healthBarEmptyObject, rl.healthBarFullObject,
                           rl.lightningEffectObject};
    for (const char* n : named)
        if (n) c.object(n);
    c.run();
    if (set.capped)
        AS3D_WARN("precache: the closure reached %zu definitions; the rest load on first use", maxObjects);
    return set;
}

// ---------------------------------------------------------------------------------------
// World side.
// ---------------------------------------------------------------------------------------

PrecacheReport World::precacheScripts() {
    const auto t0 = std::chrono::steady_clock::now();
    const LoadCounts before = LoadStats::lazyLoads();
    PrecacheReport r;
    if (!db_ || !host_) return r;
    GameScriptHost& host = *host_;
    std::unique_ptr<PrecacheSet> set(new PrecacheSet(
        collectPrecacheSet(*this, *db_, [&host](const std::string& path) { return host.program(path); })));
    // The simulation's copy of every model an entity can take (applyDef, SetModel).
    for (const ObjectDef* def : set->objects)
        if (def->type == ObjectType::Model && !def->model.empty()) loadModel(def->model);
    for (const std::string& m : set->models) loadModel(m);
    precache_ = std::move(set);
    const LoadCounts d = LoadStats::lazyLoads() - before;
    r.objects = static_cast<int>(precache_->objects.size());
    r.scripts = static_cast<int>(d.of(LoadKind::Script));
    r.models = static_cast<int>(d.of(LoadKind::Model));
    r.bytes = d.bytes;
    r.capped = precache_->capped;
    r.seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

const script::ScriptProgram* World::compiledProgram(const std::string& path) const {
    return host_ ? host_->loadedProgram(path) : nullptr;
}

} // namespace as3d
