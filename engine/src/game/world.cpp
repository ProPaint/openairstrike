// World: entity storage, lifecycle, hierarchy and script dispatch
// (docs/spec/engine-behaviour.md 3, docs/spec/rcsl-vm.md "Event dispatch").
#include "as3d/world.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "as3d/defs.h"
#include "as3d/model.h"
#include "as3d/script_host.h"
#include "as3d/vfs.h"
#include "as3d/world_particles.h"
#include "world_internal.h"
#include "world_path.h"

namespace as3d {

using script::DispatchResult;
using script::EntryPoint;

// ---------------------------------------------------------------------------------------
// Entity field helpers.
// ---------------------------------------------------------------------------------------

float Entity::f(int k) const { return bitsf(fields[k]); }
void Entity::setF(int k, float v) { fields[k] = fbits(v); }
Vec3 Entity::v3(int k) const { return {f(k), f(k + 1), f(k + 2)}; }
void Entity::setV3(int k, const Vec3& v) {
    setF(k, v.x);
    setF(k + 1, v.y);
    setF(k + 2, v.z);
}
int Entity::flagBits() const { return ftol(f(F_FLAGS)); }

// ---------------------------------------------------------------------------------------
// Construction.
// ---------------------------------------------------------------------------------------

namespace {
// engine-behaviour.md 6.3.
struct DifficultyRow {
    float health, damage, score, rank;
};
constexpr DifficultyRow kDifficulty[5] = {
    {0.3f, 0.5f, 0.6f, 0.7f},  {0.5f, 0.7f, 0.8f, 0.85f}, {0.75f, 0.8f, 1.0f, 1.07f},
    {1.5f, 1.25f, 1.2f, 1.2f}, {2.0f, 1.4f, 1.4f, 1.3f},
};
} // namespace

World::World()
    : particles_(new WorldParticles()),
      ents_(static_cast<size_t>(kMaxEntitySlots)),
      tombs_(static_cast<size_t>(kMaxEntitySlots), std::vector<u32>(static_cast<size_t>(kEntityFieldCount), 0u)) {}

World::~World() {
    // Threads reference programs owned by the host: destroy them first.
    for (Entity& e : ents_) e.thread.reset();
}

void World::init(Vfs& vfs, const DefDatabase& db, const WorldConfig& config) {
    vfs_ = &vfs;
    db_ = &db;
    config_ = config;
    if (const char* god = std::getenv("AS3D_GOD_MODE")) {
        if (god[0] == '1') config_.godMode = true;
    }
    config_.players = std::min(std::max(config_.players, 1), kMaxPlayers);
    config_.difficulty = std::min(std::max(config_.difficulty, 0), 4);
    config_.cameraMode = std::min(std::max(config_.cameraMode, 0), 3);
    if (!(config_.dt > 0.0f) || config_.dt > 0.1f) config_.dt = 1.0f / 60.0f;
    for (int p = 0; p < kMaxPlayers; ++p) config_.heli[p] = std::min(std::max(config_.heli[p], 0), 9);
    rng_ = Rng(config_.seed);
    retreg_ = 0;
    for (Entity& e : ents_) e.thread.reset();
    host_.reset(new GameScriptHost(*this));
    report_ = script::BuiltinReport();
    stats_ = WorldStats();
    for (int p = 0; p < kMaxPlayers; ++p) {
        players_[p] = PlayerRecord();
        players_[p].heli = config_.heli[p];
    }
    resetPools();
    resetCamera();
}

// ---------------------------------------------------------------------------------------
// Pool.
// ---------------------------------------------------------------------------------------

void World::resetPools() {
    for (int i = 0; i < kMaxEntitySlots; ++i) {
        Entity& e = ents_[static_cast<size_t>(i)];
        u32 gen = e.generation;
        e.thread.reset();
        e = Entity();
        e.generation = gen;
    }
    freeList_.clear();
    freeList_.reserve(kMaxEntitySlots);
    for (int i = kMaxEntitySlots - 1; i >= 0; --i) freeList_.push_back(i);
    newest_ = -1;
    listCount_ = 0;
    slotsInUse_ = 0;
    current_ = -1;
    dispatchDepth_ = 0;
}

int World::allocSlot() {
    if (freeList_.empty()) return -1;
    int idx = freeList_.back();
    freeList_.pop_back();
    Entity& e = ents_[static_cast<size_t>(idx)];
    u32 gen = e.generation + 1;
    e.thread.reset();
    e = Entity();
    e.generation = gen;
    e.inUse = true;
    e.fields[F_SELF] = refOf(idx) - kEntityRefOffset; // field 0: the entity base
    ++slotsInUse_;
    stats_.maxSlotsInUse = std::max(stats_.maxSlotsInUse, slotsInUse_);
    return idx;
}

void World::freeSlot(int idx) {
    Entity& e = ents_[static_cast<size_t>(idx)];
    if (!e.inUse) return;
    // Stale references read this snapshot: "removed, dead, no thread"
    // (rcsl-builtins-semantics.md D2), the entity's last fields with field 4 = 1.0.
    std::vector<u32>& tomb = tombs_[static_cast<size_t>(idx)];
    std::copy(e.fields, e.fields + kEntityFieldCount, tomb.begin());
    tomb[F_DEAD] = fbits(1.0f);
    e.thread.reset();
    e.program = nullptr;
    e.children.clear();
    e.inUse = false;
    e.inList = false;
    e.newer = e.older = -1;
    freeList_.push_back(idx);
    --slotsInUse_;
}

void World::freeTree(int idx) {
    Entity& e = ents_[static_cast<size_t>(idx)];
    if (!e.inUse) return;
    std::vector<int> kids = e.children;
    for (int c : kids) {
        if (c >= 0 && c < kMaxEntitySlots && ents_[static_cast<size_t>(c)].parent == idx) freeTree(c);
    }
    // Pool entities attached to this one with AttachEntity lose their parent.
    for (int i = newest_; i != -1; i = ents_[static_cast<size_t>(i)].older) {
        Entity& o = ents_[static_cast<size_t>(i)];
        if (o.parent == idx && i != idx) o.parent = -1;
    }
    freeSlot(idx);
}

void World::linkNewest(int idx) {
    Entity& e = ents_[static_cast<size_t>(idx)];
    e.inList = true;
    e.newer = -1;
    e.older = newest_;
    if (newest_ >= 0) ents_[static_cast<size_t>(newest_)].newer = idx;
    newest_ = idx;
    ++listCount_;
    stats_.maxListEntities = std::max(stats_.maxListEntities, listCount_);
}

void World::unlink(int idx) {
    Entity& e = ents_[static_cast<size_t>(idx)];
    if (!e.inList) return;
    if (e.newer >= 0) ents_[static_cast<size_t>(e.newer)].older = e.older;
    else newest_ = e.older;
    if (e.older >= 0) ents_[static_cast<size_t>(e.older)].newer = e.newer;
    e.inList = false;
    e.newer = e.older = -1;
    --listCount_;
}

std::vector<int> World::listEntities() const {
    std::vector<int> out;
    for (int i = newest_; i != -1; i = ents_[static_cast<size_t>(i)].older) out.push_back(i);
    return out;
}

EntityHandle World::handleOf(int idx) const {
    EntityHandle h;
    if (!validIndex(idx)) return h;
    h.index = static_cast<u32>(idx);
    h.generation = ents_[static_cast<size_t>(idx)].generation;
    return h;
}

Entity* World::get(EntityHandle h) {
    if (h.isNull() || h.index >= static_cast<u32>(kMaxEntitySlots)) return nullptr;
    Entity& e = ents_[h.index];
    if (!e.inUse || e.generation != h.generation) return nullptr;
    return &e;
}

bool World::decodeEntityAddr(u32 addr, int& slot, u32& gen, u32& offset) {
    if (addr < kEntityAddrBase || addr >= kEntityAddrEnd) return false;
    u32 rel = addr - kEntityAddrBase;
    gen = rel >> kRefGenShift;
    u32 low = rel & ((1u << kRefGenShift) - 1u);
    u32 s = low / kEntityAddrStride;
    if (s >= static_cast<u32>(kMaxEntitySlots)) return false;
    slot = static_cast<int>(s);
    offset = low % kEntityAddrStride;
    return true;
}

int World::liveIndexFromRef(u32 ref) const {
    int slot;
    u32 gen, off;
    if (!decodeEntityAddr(ref, slot, gen, off) || off != kEntityRefOffset) return -1;
    const Entity& e = ents_[static_cast<size_t>(slot)];
    if (!e.inUse || (e.generation & kRefGenMask) != gen) return -1;
    return slot;
}

bool World::readRefField(u32 ref, int k, u32& out) const {
    int slot;
    u32 gen, off;
    if (k < 0 || k >= kEntityFieldCount || !decodeEntityAddr(ref, slot, gen, off) || off != kEntityRefOffset) return false;
    const Entity& e = ents_[static_cast<size_t>(slot)];
    if (e.inUse && (e.generation & kRefGenMask) == gen) out = e.fields[k];
    else out = tombs_[static_cast<size_t>(slot)][static_cast<size_t>(k)];
    return true;
}

int World::rootOf(int idx) const {
    int guard = 0;
    while (idx >= 0 && ents_[static_cast<size_t>(idx)].parent >= 0 && guard++ < 64) {
        idx = ents_[static_cast<size_t>(idx)].parent;
    }
    return idx;
}

// ---------------------------------------------------------------------------------------
// Building entities from definitions (G_InitObject, engine-behaviour.md 3.1-3.4).
// ---------------------------------------------------------------------------------------

const ModelData* World::loadModel(const std::string& rawPath) {
    if (rawPath.empty()) return nullptr;
    std::string path = normalizePath(rawPath);
    auto it = models_.find(path);
    if (it != models_.end()) return it->second.get();
    if (modelMissing_.count(path)) return nullptr;
    Blob blob;
    std::unique_ptr<ModelData> m(new ModelData());
    std::string err;
    if (!vfs_ || !vfs_->read(path, blob) || !as3d::loadModel(blob.data(), blob.size(), *m, &err)) {
        modelMissing_[path] = true;
        return nullptr;
    }
    const ModelData* p = m.get();
    models_[path] = std::move(m);
    return p;
}

namespace {
float cornerRadius(const Vec3& mn, const Vec3& mx) {
    float x = std::max(std::fabs(mn.x), std::fabs(mx.x));
    float y = std::max(std::fabs(mn.y), std::fabs(mx.y));
    float z = std::max(std::fabs(mn.z), std::fabs(mx.z));
    return std::sqrt(x * x + y * y + z * z);
}
} // namespace

void World::attachScript(int idx, const std::string& path) {
    Entity& e = ents_[static_cast<size_t>(idx)];
    e.thread.reset();
    e.program = nullptr;
    e.scriptPath = normalizePath(path);
    e.scriptFaulted = false;
    if (path.empty() || !host_) return;
    const script::ScriptProgram* prog = host_->program(path);
    if (!prog) return;
    e.program = prog;
    e.thread.reset(new script::ScriptThread(*prog, *host_, &report_, nullptr, e.scriptPath.c_str()));
    e.thread->setSharedReturnRegister(&retreg_); // one engine global (docs/spec/issues/032)
    if (!e.thread->valid()) {
        AS3D_WARN("script '%s': bind failed: %s", e.scriptPath.c_str(), e.thread->bindError().c_str());
        e.thread.reset();
        e.program = nullptr;
    }
}

void World::applyDef(int idx, const ObjectDef* def) {
    Entity& e = ents_[static_cast<size_t>(idx)];
    e.def = def;
    e.name = def->name;
    e.setF(F_CLASS, static_cast<float>(static_cast<int>(def->kind)));
    e.setF(F_FLAGS, static_cast<float>(def->flags));
    e.setF(F_HEALTH, static_cast<float>(def->health));
    e.setF(F_DAMAGE, static_cast<float>(def->damage));
    e.setF(F_SCORE, static_cast<float>(def->score));
    for (int k = 0; k < 4; ++k) e.setF(F_COLOR + k, 1.0f);
    e.setF(F_SCALE, 0.0f); // field 32 starts at 0; the obj "scale" key never reaches it
    e.fields[F_RENDER_TYPE] = static_cast<u32>(def->type);
    e.fields[F_RENDER_FLAGS] = def->rflag;
    e.fields[F_SORT] = static_cast<u32>(def->sort);
    for (int k = 0; k < 9; ++k) e.setF(F_AXIS + k, (k % 4 == 0) ? 1.0f : 0.0f);
    e.touchMode = static_cast<int>(def->touch);
    for (int k = 0; k < 3; ++k) e.bboxScale[k] = def->bboxScale[k];
    e.maxHealth = static_cast<float>(def->health);
    e.rt = RT_ACTIVE;
    if (def->type == ObjectType::Model && !def->model.empty()) {
        e.modelPath = normalizePath(def->model);
        e.model = loadModel(def->model);
        if (e.model) {
            e.boundsMin = e.model->boundsMin;
            e.boundsMax = e.model->boundsMax;
            e.radius = cornerRadius(e.boundsMin, e.boundsMax);
        }
    } else if (def->hasBbox) {
        // Sprite extent (GUESS for the layout of the 4 numbers: x, y first).
        e.boundsMin = {def->bboxMin[0], def->bboxMin[1], 0.0f};
        e.boundsMax = {def->bboxMax[0], def->bboxMax[1], 0.0f};
        e.radius = cornerRadius(e.boundsMin, e.boundsMax);
    }
    if (!def->script.empty()) attachScript(idx, def->script);
}

int World::buildEntity(const ObjectDef* def, int depth) {
    if (!def) return -1;
    int idx = allocSlot();
    if (idx < 0) {
        ++stats_.spawnRefused;
        return -1;
    }
    applyDef(idx, def);
    if (depth >= kMaxAttachDepth) return idx;
    for (const AttachDef& at : def->attachments) {
        if (at.nightOnly && !night_) continue;
        int c = -1;
        if (const ParticleSystemDef* ps = db_->findParticleSystem(at.targetName)) {
            // Particle systems resolve first (obj.md "attach"): an emitter holder.
            c = allocSlot();
            if (c < 0) {
                ++stats_.spawnRefused;
                continue;
            }
            Entity& ce = ents_[static_cast<size_t>(c)];
            ce.emitter = ps;
            ce.rt = RT_ACTIVE;
            for (int k = 0; k < 4; ++k) ce.setF(F_COLOR + k, 1.0f);
            for (int k = 0; k < 9; ++k) ce.setF(F_AXIS + k, (k % 4 == 0) ? 1.0f : 0.0f);
        } else if (const ObjectDef* cd = db_->findObject(at.targetName)) {
            c = buildEntity(cd, depth + 1);
            if (c < 0) continue;
        } else {
            continue; // "Unknown attach" in the original: skipped
        }
        Entity& ce = ents_[static_cast<size_t>(c)];
        ce.parent = idx;
        ce.tagName = at.tagName;
        ce.absAttach = at.absolute;
        ce.name = at.idName;
        ents_[static_cast<size_t>(idx)].children.push_back(c);
    }
    return idx;
}

void World::setStateRecursive(int idx, int state) {
    Entity& e = ents_[static_cast<size_t>(idx)];
    e.state = state;
    for (int c : e.children) setStateRecursive(c, state);
}

void World::setPlayerIndexRecursive(int idx, int p) {
    Entity& e = ents_[static_cast<size_t>(idx)];
    e.playerIndex = p;
    for (int c : e.children) setPlayerIndexRecursive(c, p);
}

void World::setActive(int idx, bool on) {
    if (!validIndex(idx)) return;
    Entity& e = ents_[static_cast<size_t>(idx)];
    if (on) e.rt |= RT_ACTIVE;
    else e.rt &= ~RT_ACTIVE;
    for (int c : e.children) setActive(c, on);
}

float World::terrainHeight(float x, float y) const { return terrainValid_ ? terrain_.heightAt(x, y) : 0.0f; }

void World::snapToGround(Entity& e) {
    int fl = e.flagBits();
    if (fl & FL_ONGROUND) e.setF(F_ORIGIN + 2, terrainHeight(e.f(F_ORIGIN), e.f(F_ORIGIN + 1)));
    else if (fl & FL_ONWATER) e.setF(F_ORIGIN + 2, waterLevel_);
}

int World::createEntity(const std::string& defName, const Vec3& pos, int creator) {
    return createEntity(db_ ? db_->findObject(defName) : nullptr, pos, creator, true);
}

int World::spawnRoot(const ObjectDef* def, const Vec3& pos, bool snap) {
    if (!def) return -1;
    if (listCount_ >= kMaxListEntities) {
        ++stats_.spawnRefused; // the original crashes here (engine-behaviour.md 3.3)
        return -1;
    }
    int idx = buildEntity(def, 0);
    if (idx < 0) return -1;
    ++stats_.entitiesCreated;
    linkNewest(idx);
    Entity& e = ents_[static_cast<size_t>(idx)];
    setStateRecursive(idx, ES_ACTIVE);
    e.setV3(F_ORIGIN, pos);
    if (snap) snapToGround(e);
    return idx;
}

int World::createEntity(const ObjectDef* def, const Vec3& pos, int creator, bool thinkNow) {
    int idx = spawnForCreate(def, pos, creator);
    if (idx >= 0) finishCreate(idx, thinkNow);
    return idx;
}

int World::spawnForCreate(const ObjectDef* def, const Vec3& pos, int creator) {
    int idx = spawnRoot(def, pos);
    if (idx < 0) return -1;
    Entity& e = ents_[static_cast<size_t>(idx)];
    if (validIndex(creator)) {
        const Entity& c = ents_[static_cast<size_t>(creator)];
        for (int k = 0; k < 3; ++k) e.fields[F_ANGLES + k] = c.fields[F_ANGLES + k];
        setPlayerIndexRecursive(idx, c.playerIndex);
    }
    return idx;
}

void World::finishCreate(int idx, bool thinkNow) {
    if (!validIndex(idx)) return;
    runInit(idx);
    Entity& e = ents_[static_cast<size_t>(idx)];
    if (e.f(F_CLASS) == kClassEnemy && !(e.flagBits() & FL_NONTARGET)) ++enemiesInLevel_;
    if (thinkNow) think(idx);
}

void World::attachEntity(int child, int parent, const std::string& tag, bool absolute) {
    if (!validIndex(child)) return;
    Entity& c = ents_[static_cast<size_t>(child)];
    c.parent = validIndex(parent) ? parent : -1;
    c.tagName = tag;
    c.absAttach = absolute;
    c.rt |= RT_ATTACHED_ENTITY | RT_ACTIVE;
    if (c.parent >= 0 && !c.countedInRoot) {
        // The original increments on every call; counting once keeps a double attach
        // from pinning the root forever.
        int r = rootOf(c.parent);
        if (r >= 0 && r != child) {
            c.countedInRoot = true;
            ++ents_[static_cast<size_t>(r)].attachRefCount;
        }
    }
}

void World::markRemoved(int idx) {
    Entity& e = ents_[static_cast<size_t>(idx)];
    e.rt |= RT_REMOVED;
    for (int c : e.children) {
        if (c >= 0 && c < kMaxEntitySlots && ents_[static_cast<size_t>(c)].inUse) markRemoved(c);
    }
}

void World::removeEntity(int idx) {
    if (!validIndex(idx)) return;
    Entity& e = ents_[static_cast<size_t>(idx)];
    if (e.rt & RT_REMOVED) return;
    markRemoved(idx);
    if (e.countedInRoot && e.parent >= 0) {
        int r = rootOf(e.parent);
        if (r >= 0) --ents_[static_cast<size_t>(r)].attachRefCount;
        e.countedInRoot = false;
    }
}

// ---------------------------------------------------------------------------------------
// Dispatch.
// ---------------------------------------------------------------------------------------

int World::currentPlayerIndex() const {
    int i = liveIndexFromRef(selfBits);
    if (i < 0) return 0;
    int p = ents_[static_cast<size_t>(i)].playerIndex;
    return (p == 1) ? 1 : 0;
}

DispatchResult World::dispatch(int idx, EntryPoint ep) {
    DispatchResult none;
    if (!validIndex(idx)) return none;
    Entity& e = ents_[static_cast<size_t>(idx)];
    if (!e.thread || e.scriptFaulted || !e.program || !e.program->hasEntry(ep)) return none;
    if (dispatchDepth_ >= kMaxDispatchDepth) {
        noteScriptError(e.scriptPath.c_str(), "dispatch nesting too deep");
        return none;
    }
    int savedCurrent = current_;
    u32 savedSelf = selfBits;
    current_ = idx;
    selfBits = refOf(idx);
    ++dispatchDepth_;
    // The thread object stays alive for the whole call: entities are freed only at the
    // start of the entity pass, never while a handler runs.
    script::ScriptThread* th = e.thread.get();
    DispatchResult r = (ep == EntryPoint::Main) ? th->runMain(frame_, frametime_) : th->runEvent(ep, frame_);
    --dispatchDepth_;
    if (!r.ok) e.scriptFaulted = true; // a faulted thread is not run again
    current_ = savedCurrent;
    selfBits = savedSelf;
    return r;
}

void World::runInit(int idx) {
    if (!validIndex(idx)) return;
    Entity& e = ents_[static_cast<size_t>(idx)];
    e.sinceDamage = 2.0f;
    dispatch(idx, EntryPoint::Init);
    for (size_t i = 0; i < e.children.size(); ++i) {
        int c = e.children[i];
        if (validIndex(c) && ents_[static_cast<size_t>(c)].parent == idx) runInit(c);
    }
}

void World::runCallback(int idx, float msg, float p1, float p2) {
    cbMsgBits = fbits(msg);
    cbParm1Bits = fbits(p1);
    cbParm2Bits = fbits(p2);
    dispatch(idx, EntryPoint::Callback);
}

void World::runTouch(int idx, int otherIdx) {
    u32 saved = otherBits;
    otherBits = refOf(otherIdx);
    dispatch(idx, EntryPoint::Touch);
    otherBits = saved;
}

void World::noteScriptError(const char* scriptName, const char* message) {
    ++stats_.scriptErrors;
    std::string msg = message ? message : "";
    if (msg.find("stall") != std::string::npos) ++stats_.stalls;
    if (stats_.firstErrors.size() < 16) {
        std::string line = std::string(scriptName ? scriptName : "?") + ": " + msg;
        AS3D_WARN("script error at frame %u: %s", frame_, line.c_str());
        stats_.firstErrors.push_back(line);
    }
    if (validIndex(current_)) ents_[static_cast<size_t>(current_)].scriptFaulted = true;
}

// ---------------------------------------------------------------------------------------
// Players (engine-behaviour.md 7.4).
// ---------------------------------------------------------------------------------------

namespace {
const char* const kHeliNames[10] = {
    "p_apache",        "p_comanche",      "p_apache_white", "p_apache_impala", "p_comanche_white",
    "p_comanche_lava", "p_apache_blue",   "p_comanche_sand", "p_comanche_blue", "p_comanche_green",
};
} // namespace

bool World::isPlayerEntity(int idx, int* playerOut) const {
    if (idx < 0) return false;
    for (int p = 0; p < config_.players; ++p) {
        if (players_[p].entityRef != 0 && players_[p].entityRef == refOf(idx)) {
            if (playerOut) *playerOut = p;
            return true;
        }
    }
    return false;
}

int World::playerEntityIndex(int p) const {
    if (p < 0 || p >= config_.players) return -1;
    int i = liveIndexFromRef(players_[p].entityRef);
    if (i < 0 || (ents_[static_cast<size_t>(i)].rt & RT_REMOVED)) return -1;
    return i;
}

void World::spawnPlayer(int p) {
    if (p < 0 || p >= config_.players || !db_) return;
    PlayerRecord& pr = players_[p];
    int old = liveIndexFromRef(pr.entityRef);
    if (old >= 0 && ents_[static_cast<size_t>(old)].playerIndex != p) old = -1; // slot reused
    if (old >= 0) removeEntity(old);
    if (pr.lives < 0.0f) {
        if (old >= 0) ents_[static_cast<size_t>(old)].setF(F_DEAD, 1.0f);
        return;
    }
    // Clears missiles and power-ups of both players (VERIFIED-CODE, kept).
    for (int q = 0; q < kMaxPlayers; ++q) {
        for (int& n : players_[q].missiles) n = 0;
        for (int& n : players_[q].powerups) n = 0;
        players_[q].currentMissile = -1;
        players_[q].currentPowerup = -1;
    }
    const ObjectDef* def = db_->findObject(kHeliNames[std::min(std::max(pr.heli, 0), 9)]);
    if (!def || listCount_ >= kMaxListEntities) return;
    int idx = buildEntity(def, 0);
    if (idx < 0) return;
    ++stats_.entitiesCreated;
    linkNewest(idx);
    Entity& e = ents_[static_cast<size_t>(idx)];
    setStateRecursive(idx, ES_ACTIVE);
    e.setV3(F_ORIGIN, {0.0f, 0.0f, 0.0f}); // no ground snap (G_SpawnPlayer)
    setPlayerIndexRecursive(idx, p);
    pr.entityRef = refOf(idx);
    pr.freezeCount = 0;
    pr.actionsDisabled = false;
    runInit(idx);
    // Two-player offset, applied after init (rcsl-builtins-semantics.md RespawnPlayer).
    if (config_.players == 2) e.setF(F_ORIGIN, e.f(F_ORIGIN) + (p == 0 ? -100.0f : 100.0f));
}

// ---------------------------------------------------------------------------------------
// Effects, sound, hints.
// ---------------------------------------------------------------------------------------

void World::placeLight(const Vec3& pos, const Vec3& color, float radius) {
    if (lights_.size() >= 32) return;
    lights_.push_back({pos, color, radius});
}

void World::queueLightning(const Vec3& start, const Vec3& end) {
    if (bolts_.size() >= kMaxLightningBolts) return;
    bolts_.push_back({start, end});
}

void World::queueSound(SoundEvent::Kind kind, int idx, const std::string& sample) {
    if (sounds_.size() >= 1024) sounds_.erase(sounds_.begin()); // nobody drains it headless
    SoundEvent ev;
    ev.kind = kind;
    ev.entity = handleOf(idx);
    ev.sample = sample;
    sounds_.push_back(ev);
}

void World::showHint(const std::string& text) {
    hintText_ = text;
    hintShowing_ = true;
    paused_ = true;
    ++hintsShown_;
}

void World::dismissHint() {
    hintShowing_ = false;
    paused_ = false;
}

// ---------------------------------------------------------------------------------------
// Level flow.
// ---------------------------------------------------------------------------------------

void World::endLevel() {
    hudHidden_ = true;
    paused_ = true;
    levelComplete_ = true;
}

void World::setGameOver() {
    gameOver_ = true;
    hudHidden_ = true;
    paused_ = true;
}

void World::startQuake(float amplitude) {
    camera_.quakeAmplitude = amplitude;
    camera_.quakeClock = 3.0f;
    camera_.quakeElapsed = 0.0f;
}

void World::resetLevelState() {
    const DifficultyRow& d = kDifficulty[config_.difficulty];
    healthFactor_ = d.health;
    damageFactor_ = d.damage;
    scoreFactor_ = d.score;
    rankFactor_ = d.rank;
    paused_ = false;
    hudHidden_ = false;
    gameOver_ = false;
    levelComplete_ = false;
    enemiesInLevel_ = 0;
    maxLevelScore_ = 0.0f;
    starTotal_ = 0;
    hintShowing_ = false;
    hintText_.clear();
    lights_.clear();
    bolts_.clear();
    sounds_.clear();
    frame_ = 0;
    time_ = 0.0f;
    levelClock_ = 0.0f;
    selfBits = otherBits = cbMsgBits = cbParm1Bits = cbParm2Bits = 0;
    // The particle instances have their own random stream, restarted with every level so a
    // level's particles do not depend on what ran before (seed never 0 for xorshift).
    particles_->reset(config_.seed * 2654435761u + 1u);
}

void World::updateParticles() {
    if (paused_) return;
    particles_->update(*this, frametime_);
}

void World::resetPlayersForLevel() {
    for (int p = 0; p < kMaxPlayers; ++p) {
        PlayerRecord& pr = players_[p];
        pr.lives = static_cast<float>(pr.livesAtStart);
        pr.scores = 0.0f;
        pr.stars = 0.0f;
        pr.kills = 0;
        pr.counter[2] = 1.0f;
        for (int& u : pr.upgrades) u = 0;
        pr.upgrades[0] = 1;
        pr.weapon = 0.0f;
        pr.action = 0.0f;
        pr.heldInput = 0;
        pr.entityRef = 0;
    }
}

} // namespace as3d
