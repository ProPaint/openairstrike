// Game world: entity storage, script dispatch, per-frame update, collision and damage,
// and the level runtime (scroll, map spawner, players). Pure simulation: no GL, no SDL,
// no audio. See docs/spec/engine-behaviour.md (the main source), docs/spec/rcsl-vm.md
// (dispatch rules and the entity/global address model) and docs/spec/hmap.md (spawning).
//
// Deliberate engine decisions (docs/spec/README.md): fixed timestep, all randomness from
// as3d::Rng, collision rectangles computed on a fixed 800x600 viewport.
//
// This header does not include as3d/defs.h (defs.h and gfx.h both define
// as3d::BlendMode); definitions are forward-declared.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/math.h"
#include "as3d/script.h"
#include "as3d/terrain.h"

namespace as3d {

struct ObjectDef;
struct ParticleSystemDef;
struct ModelData;
class DefDatabase;
class Vfs;
class GameScriptHost;
class GamePath;

// ---------------------------------------------------------------------------------------
// Limits and layouts.
// ---------------------------------------------------------------------------------------

// engine-behaviour.md 3.3: 1024 pool (list) entities. Attached children live outside the
// pool in the original; here they share one bounded slot array.
constexpr int kMaxListEntities = 1024;
constexpr int kMaxEntitySlots = 4096;
// Script fields 0..89 cover entity + 0x7B .. + 0x1E3 (rcsl-vm.md, "Field access").
constexpr int kEntityFieldCount = 90;
constexpr int kMaxPlayers = 2;
constexpr int kCameraFieldCount = 32;
constexpr int kMaxAttachDepth = 8;
constexpr int kMaxDispatchDepth = 48;

// Fixed virtual viewport for collision rectangles (docs/spec/README.md deviations).
constexpr float kCollisionViewportW = 800.0f;
constexpr float kCollisionViewportH = 600.0f;

// Script field indices (engine-behaviour.md 3.2).
enum EntityField : int {
    F_SELF = 0,
    F_AGE = 1,
    F_CLASS = 2,
    F_FLAGS = 3,
    F_DEAD = 4,
    F_ORIGIN = 5,
    F_ATTACH_OFFSET = 8,
    F_PREV_ORIGIN = 11,
    F_ANGLES = 14,
    F_VELOCITY = 17,
    F_SCRATCH = 20,
    F_WP_SPEED = 23,
    F_WP_TURN_RATE = 24,
    F_WP_BANK = 25,
    F_COLOR = 28,
    F_SCALE = 32,
    F_FRAME = 33,
    F_HEALTH = 34,
    F_DAMAGE = 35,
    F_SCORE = 36,
    F_WP_WAIT = 37,
    F_RENDER_TYPE = 38,
    F_RENDER_FLAGS = 39,
    F_SORT = 40,
    F_BASE_ORIGIN = 41,
    F_AXIS = 44,
};

// Runtime flags (entity + 0x1E, engine-behaviour.md 3.2).
enum RuntimeFlag : u32 {
    RT_REMOVED = 0x01,
    RT_THOUGHT = 0x02,
    RT_ACTIVE = 0x04,
    RT_COLLIDABLE = 0x08,
    RT_HEALTH_FROZEN = 0x10,
    RT_ATTACHED_ENTITY = 0x20,
    RT_LOCKED = 0x100,
};

// Activation state (entity + 0x1A, engine-behaviour.md 3.5).
enum EntityState : int { ES_ACTIVE = 0, ES_DORMANT = 1, ES_LEAVING = 2 };

// Classes (field 2).
constexpr float kClassPlayer = 1.0f;
constexpr float kClassEnemy = 2.0f;
constexpr float kClassProjectile = 4.0f;

// p_action bits (engine-behaviour.md 7.2).
enum ActionBit : u32 {
    ACT_FIRE = 0x001,
    ACT_MISSILE = 0x002,
    ACT_POWERUP = 0x004,
    ACT_FORWARD = 0x010,
    ACT_BACKWARD = 0x020,
    ACT_LEFT = 0x040,
    ACT_RIGHT = 0x080,
    ACT_NEXT_MISSILE = 0x100,
    ACT_NEXT_POWERUP = 0x200,
    ACT_NEXT_WEAPON = 0x400,
};

// Script-visible address layout used by GameScriptHost (rcsl-vm.md "Entity references"):
// an entity reference is entity base + 0x7B, field k lives at reference + 4k and field 0
// holds the base. Slot i's base is kEntityAddrBase + gen << kRefGenShift + i * stride,
// where gen is the low 9 bits of the slot's generation: references carry a generation
// count (docs/spec/README.md, engine decisions). A builtin given a 0 or stale reference
// does nothing and returns 0; reading fields through a stale reference sees the freed
// entity's last fields with field 4 (dead) = 1.0 ("removed, dead, no thread",
// rcsl-builtins-semantics.md D2); writes through it are ignored.
constexpr u32 kGlobalAddrBase = 0x2000'0000u;   // global g at + 16g
constexpr u32 kCameraAddrBase = 0x2100'0000u;   // camera field n at + 4n
constexpr u32 kEntityAddrBase = 0x4000'0000u;
constexpr u32 kEntityAddrStride = 0x200u;
constexpr u32 kEntityRefOffset = 0x7Bu;
constexpr u32 kRefGenShift = 21;
constexpr u32 kRefGenMask = 0x1FFu;
constexpr u32 kEntityAddrEnd = kEntityAddrBase + ((kRefGenMask + 1u) << kRefGenShift);
static_assert(static_cast<u32>(kMaxEntitySlots) * kEntityAddrStride <= (1u << kRefGenShift), "slot bits overflow");

// A stable C++ handle: slot index plus the slot's generation at the time the handle was
// taken. A script reference carries the low 9 bits of the same generation (see
// World::refOf); the handle carries all 32.
struct EntityHandle {
    u32 index = 0xFFFFFFFFu;
    u32 generation = 0;
    bool isNull() const { return index == 0xFFFFFFFFu; }
    bool operator==(const EntityHandle& o) const { return index == o.index && generation == o.generation; }
    bool operator!=(const EntityHandle& o) const { return !(*this == o); }
};

// Screen-space collision rectangle (entity + 0x1C3): min x, y, depth; max x, y, depth.
// Window coordinates of the 800x600 viewport, y up (GL convention, engine-behaviour.md 7.3).
struct ScreenRect {
    float min[3] = {0, 0, 0};
    float max[3] = {0, 0, 0};
};

struct Entity {
    // Slot bookkeeping.
    bool inUse = false;
    u32 generation = 0;

    // Active list (pool entities only), newest-first iteration: `older` walks toward the
    // oldest entity. -1 terminates.
    bool inList = false;
    int newer = -1;
    int older = -1;

    // Hierarchy (+0x08, +0x0C, +0x10, +0x11, +0x12, +0x1DB/+0x1DF).
    int parent = -1;
    std::vector<int> children;       // definition attachments, in attach order
    std::string tagName;             // parent tag ("origin" = parent origin)
    bool absAttach = false;
    bool countedInRoot = false;      // +0x11
    int attachRefCount = 0;          // +0x12, roots only

    std::string name;   // +0x16: definition name (roots) or attach id (children)
    int state = ES_ACTIVE;
    u32 rt = 0;         // runtime flags

    const ObjectDef* def = nullptr;
    const ObjectDef* drop = nullptr;             // +0x22
    const ParticleSystemDef* emitter = nullptr;  // +0x1BB: emitter holder

    // Waypoint path (+0x4B/+0x4F/+0x53).
    const GamePath* path = nullptr;
    float pathDistance = 0.0f;
    int pathLastNode = 0;
    bool pathFinished = false;

    // Script thread (+0x57).
    const script::ScriptProgram* program = nullptr;
    std::unique_ptr<script::ScriptThread> thread;
    std::string scriptPath;
    bool scriptFaulted = false;

    int touchMode = 0;                       // +0x5B
    float bboxScale[3] = {0.7f, 0.7f, 0.7f}; // +0x5F
    float maxHealth = 0.0f;                  // +0x6B
    float sinceDamage = 0.0f;                // +0x6F
    int playerIndex = 0;                     // +0x77

    const ModelData* model = nullptr;        // +0x153
    std::string modelPath;
    std::string skinPath;                    // +0x157 (setskin; empty = definition skin)
    std::string loopSound;                   // +0x73 looping sample (empty = none)
    Vec3 boundsMin, boundsMax;               // model (or sprite) box, model space
    float radius = 0.0f;                     // +0x1BF

    ScreenRect rect;                         // +0x1C3
    bool hasPrevPoint = false;
    float prevPoint[3] = {0, 0, 0};

    u32 fields[kEntityFieldCount] = {};

    float f(int k) const;
    void setF(int k, float v);
    Vec3 v3(int k) const;
    void setV3(int k, const Vec3& v);
    int flagBits() const; // ftol(field 3)
};

struct PlayerRecord {
    u32 entityRef = 0;          // +0x00 as a script reference (0 = none)
    int heli = 0;               // +0x88
    float action = 0.0f;        // p_action
    float scores = 0.0f;        // p_scores
    int livesAtStart = 2;       // +0x94
    float lives = 2.0f;         // p_lives
    float stars = 0.0f;         // p_stars
    float counter[3] = {0, 0, 0};
    float speedFactor = 0.0f;   // p_speedfactor
    float weapon = 0.0f;        // p_weapon
    int freezeCount = 0;        // +0xB4
    bool actionsDisabled = false;
    int powerups[16] = {};
    int currentPowerup = -1;
    int missiles[5] = {};
    int currentMissile = -1;
    int upgrades[20] = {};
    int banked = 0;
    float rankAccumulator = 0.0f;
    int kills = 0;
    u32 heldInput = 0;          // last programmatic input, for press/release edges
};

// Programmatic input (a test or a bot fills it in): the held action bits per player.
// Presses OR bits into p_action and releases clear them (engine-behaviour.md 7.2).
struct PlayerInput {
    u32 action[kMaxPlayers] = {0, 0};
    bool confirm = false; // the OK button of a tutorial hint box
};

// A dynamic light queued by PlaceLight for this frame (at most 32, cleared each frame).
struct QueuedLight {
    Vec3 pos;
    Vec3 color;
    float radius = 0.0f;
};

// A sound request for the audio layer (StartSound / StartLoopingSound /
// StopLoopingSound); drained by whoever plays sounds. Bounded.
struct SoundEvent {
    enum class Kind : u8 { Play, Loop, StopLoop } kind = Kind::Play;
    EntityHandle entity;
    std::string sample;
};

struct WorldConfig {
    int difficulty = 2;         // 0..4, default Normal
    int players = 1;            // 1 or 2
    int heli[kMaxPlayers] = {1, 0};
    int cameraMode = 1;
    u32 seed = 1;
    float dt = 1.0f / 60.0f;    // fixed timestep
    bool godMode = false;
};

struct CameraState {
    float field[kCameraFieldCount] = {};
    int mode = 1;
    float quakeAmplitude = 0.0f;
    float quakeClock = 0.0f;    // seconds left
    float quakeElapsed = 0.0f;
    float quakeYaw = 0.0f, quakeRoll = 0.0f, fov = 60.0f;
};

struct WorldStats {
    script::u64 scriptErrors = 0;
    script::u64 stalls = 0;
    script::u64 spawnRefused = 0;
    script::u64 entitiesCreated = 0; // pool (list) entities
    script::u64 entitiesFreed = 0;   // pool (list) entities
    int maxListEntities = 0;
    int maxSlotsInUse = 0;
    std::vector<std::string> firstErrors; // at most 16
};

class World {
public:
    World();
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // Binds the world to its data. `vfs` and `db` must outlive the world.
    void init(Vfs& vfs, const DefDatabase& db, const WorldConfig& config);

    // Level runtime (level_runtime.cpp). `ref` as for loadLevelByRef ("1" = mission1).
    // Resets everything, loads map and terrain, builds paths, spawns the players.
    bool loadLevel(const std::string& ref, std::string* error);
    // A level with no map (flat terrain at height 0, no placements): for tests.
    void startEmptyLevel(bool spawnPlayers);

    // One fixed-timestep frame (engine-behaviour.md 2).
    void step(const PlayerInput& input);

    // --- entity API ---------------------------------------------------------------
    // `create` semantics (engine-behaviour.md 3.4): -1 on failure.
    int createEntity(const ObjectDef* def, const Vec3& pos, int creator, bool thinkNow = true);
    int createEntity(const std::string& defName, const Vec3& pos, int creator = -1);
    // Builds a pool entity at `pos` (ground/water snapping, state active) without
    // running init or a think: the common first half of create, Shoot and the spawners.
    int spawnRoot(const ObjectDef* def, const Vec3& pos, bool snap = true);
    // AttachEntity semantics (rcsl-builtins-table.md): child keeps its pool slot.
    void attachEntity(int child, int parent, const std::string& tag, bool absolute);
    void removeEntity(int idx);   // deferred (engine-behaviour.md 3.3)
    void setActive(int idx, bool on);
    bool validIndex(int idx) const { return idx >= 0 && idx < kMaxEntitySlots && ents_[idx].inUse; }
    Entity& entity(int idx) { return ents_[static_cast<size_t>(idx)]; }
    const Entity& entity(int idx) const { return ents_[static_cast<size_t>(idx)]; }
    EntityHandle handleOf(int idx) const;
    Entity* get(EntityHandle h);
    u32 refOf(int idx) const {
        return kEntityAddrBase + ((ents_[static_cast<size_t>(idx)].generation & kRefGenMask) << kRefGenShift) +
               static_cast<u32>(idx) * kEntityAddrStride + kEntityRefOffset;
    }
    // Decodes an entity-window address into slot, generation bits and byte offset from
    // the slot base; false outside the window.
    static bool decodeEntityAddr(u32 addr, int& slot, u32& gen, u32& offset);
    // In-use slot index of a script reference whose generation matches, or -1 (0, stale,
    // malformed).
    int liveIndexFromRef(u32 ref) const;
    // Reads field k through a reference (live, or stale: the tombstone), false if the
    // reference is not an entity reference at all.
    bool readRefField(u32 ref, int k, u32& out) const;
    // Newest-first list of pool entities (a snapshot).
    std::vector<int> listEntities() const;
    int listCount() const { return listCount_; }
    int slotsInUse() const { return slotsInUse_; }

    // --- script dispatch (rcsl-vm.md "Event dispatch") ----------------------------
    script::DispatchResult dispatch(int idx, script::EntryPoint ep);
    void runInit(int idx);                              // entity then children (recursive)
    void runCallback(int idx, float msg, float p1, float p2);
    void runTouch(int idx, int otherIdx);
    int currentEntity() const { return current_; }
    int currentPlayerIndex() const;

    // --- damage (engine-behaviour.md 6) --------------------------------------------
    void damageEntity(int idx, float amount, int attacker);
    int attackerFor(int callerIdx) const;
    void awardScore(int player, int idx);
    bool isPlayerEntity(int idx, int* playerOut = nullptr) const;

    // --- collision (collision.cpp) ------------------------------------------------
    // Projects a world point with the previous frame's matrices into window pixels (y up).
    bool projectPoint(const Vec3& p, float out[3]) const;
    bool sphereInFrustum(const Vec3& c, float r) const;
    void computeScreenBounds(int idx);
    static bool rectsOverlap(const ScreenRect& a, const ScreenRect& b);
    static bool pointInRect(const float p[3], const ScreenRect& r);
    static bool segmentHitsRect(const float a[3], const float b[3], const ScreenRect& r);
    bool isPointCollider(const Entity& e) const;
    void runCollisions();
    void touchEntity(int idx);

    // --- movement helpers used by builtins ---------------------------------------
    float terrainHeight(float x, float y) const;
    void setupTransform(int idx);
    void attachToTag(int idx);
    bool tagLocal(int idx, const std::string& tag, Vec3& out) const; // tag in idx's model
    Vec3 tagWorldPosition(int idx, const std::string& tag) const;
    void think(int idx);

    // --- effects, sound, hints -----------------------------------------------------
    const std::vector<QueuedLight>& lights() const { return lights_; }
    void placeLight(const Vec3& pos, const Vec3& color, float radius);
    std::vector<SoundEvent>& soundEvents() { return sounds_; }
    void queueSound(SoundEvent::Kind kind, int idx, const std::string& sample);
    const std::string& hintText() const { return hintText_; }
    bool hintShowing() const { return hintShowing_; }
    void showHint(const std::string& text);
    void dismissHint();
    script::u64 hintsShown() const { return hintsShown_; }

    // --- paths (world_path.cpp) ---------------------------------------------------
    const GamePath* pathOfPlacement(size_t placementIndex) const;

    // --- players ------------------------------------------------------------------
    void spawnPlayer(int p);
    PlayerRecord& player(int p) { return players_[p]; }
    const PlayerRecord& player(int p) const { return players_[p]; }
    int playerEntityIndex(int p) const;    // in-use slot of the player's entity, or -1
    int numPlayers() const { return config_.players; }

    // --- state --------------------------------------------------------------------
    CameraState& camera() { return camera_; }
    const CameraState& camera() const { return camera_; }
    float mapPos() const { return mapPos_; }
    void setMapPos(float v) { mapPos_ = v; }
    float frametime() const { return frametime_; }
    float time() const { return time_; }
    u32 frame() const { return frame_; }
    bool paused() const { return paused_; }
    void setPaused(bool p) { paused_ = p; }
    bool levelComplete() const { return levelComplete_; }
    bool gameOver() const { return gameOver_; }
    bool intermission() const { return intermission_; }
    float damageFactor() const { return damageFactor_; }
    float healthFactor() const { return healthFactor_; }
    int enemiesInLevel() const { return enemiesInLevel_; }
    float maxLevelScore() const { return maxLevelScore_; }
    int starTotal() const { return starTotal_; }
    const WorldConfig& config() const { return config_; }
    Rng& rng() { return rng_; }
    const DefDatabase& db() const { return *db_; }
    Vfs& vfs() { return *vfs_; }
    const Terrain* terrain() const { return terrainValid_ ? &terrain_ : nullptr; }
    float waterLevel() const { return waterLevel_; }
    bool hasWater() const { return hasWater_; }
    bool night() const { return night_; }
    const Mat4& previousViewProj() const { return prevViewProj_; }
    GameScriptHost& host() { return *host_; }
    script::BuiltinReport& report() { return report_; }
    const WorldStats& stats() const { return stats_; }

    // Script globals (the host reads/writes them; rcsl-vm.md "Globals").
    u32 selfBits = 0, otherBits = 0, cbMsgBits = 0, cbParm1Bits = 0, cbParm2Bits = 0;
    float lNight = 0.0f, lWater = 0.0f, lWaterLevel = 0.0f;
    float frametimeGlobal = 0.0f, timeGlobal = 0.0f;

    // Used by the host.
    void noteScriptError(const char* scriptName, const char* message);
    const ModelData* loadModel(const std::string& path);

    // Deterministic JSON dump of the whole simulation state.
    std::string dumpStateJson() const;

    // Level-flow actions (builtins EndLevel etc.).
    void endLevel();
    void setGameOver();
    // Camera quake (CameraQuake builtin).
    void startQuake(float amplitude);

private:
    friend class GameScriptHost;

    void resetPools();
    int allocSlot();
    void freeSlot(int idx);
    void freeTree(int idx);
    void linkNewest(int idx);
    void unlink(int idx);
    int buildEntity(const ObjectDef* def, int depth);
    void applyDef(int idx, const ObjectDef* def);
    void attachScript(int idx, const std::string& path);
    void setStateRecursive(int idx, int state);
    void setPlayerIndexRecursive(int idx, int p);
    void snapToGround(Entity& e);
    int rootOf(int idx) const;
    void markRemoved(int idx);

    // Frame phases.
    void applyInput(const PlayerInput& input);
    void updateCamera();
    void activateMapObjects();
    void playerFrame();
    void runEntities();
    void freeRemoved();
    bool inActivationArea(const Entity& e) const;
    void renderPass(); // computes the matrices the next frame's collision uses
    void spawnPlacement(const Placement& p);
    void resetCamera();
    void resetLevelState();
    void resetPlayersForLevel();

    Vfs* vfs_ = nullptr;
    const DefDatabase* db_ = nullptr;
    WorldConfig config_;
    Rng rng_{1};
    std::unique_ptr<GameScriptHost> host_;
    script::BuiltinReport report_;
    WorldStats stats_;

    std::vector<Entity> ents_;
    std::vector<int> freeList_; // LIFO
    int newest_ = -1;           // head of the active list
    int listCount_ = 0;
    int slotsInUse_ = 0;
    int current_ = -1;          // entity whose handler is running
    int dispatchDepth_ = 0;

    PlayerRecord players_[kMaxPlayers];
    CameraState camera_;

    // Level.
    std::unique_ptr<LoadedLevel> level_;
    Terrain terrain_;
    bool terrainValid_ = false;
    SpawnCursor cursor_;
    bool levelLoaded_ = false;
    float hmin_ = -1.0e9f;
    float waterLevel_ = 0.0f;
    bool hasWater_ = false;
    bool night_ = false;
    bool intermission_ = false;
    float intermissionCam_[6] = {};

    float mapPos_ = 32.0f;
    float frametime_ = 0.0f;
    float time_ = 0.0f;
    float levelClock_ = 0.0f;
    u32 frame_ = 0;
    bool paused_ = false;
    bool levelComplete_ = false;
    bool gameOver_ = false;
    bool hudHidden_ = false;

    float damageFactor_ = 1.0f;
    float healthFactor_ = 1.0f;
    float scoreFactor_ = 1.0f;
    float rankFactor_ = 1.0f;
    int enemiesInLevel_ = 0;
    float maxLevelScore_ = 0.0f;
    int starTotal_ = 0;

    Mat4 prevViewProj_;
    Vec4 planes_[6]; // left, right, bottom, top, near, far of prevViewProj_

    std::vector<std::vector<u32>> tombs_;  // per slot: fields at free time, dead = 1
    std::vector<QueuedLight> lights_;
    std::vector<SoundEvent> sounds_;
    std::string hintText_;
    bool hintShowing_ = false;
    script::u64 hintsShown_ = 0;
    std::vector<std::unique_ptr<GamePath>> gamePaths_; // per placement (null if none)

    std::map<std::string, std::unique_ptr<ModelData>> models_;
    std::map<std::string, bool> modelMissing_;
};

} // namespace as3d
