// Typed object/weapon/particle-system/level definitions, built from the
// generic brace-block parse trees (as3d/textblock.h) read through the Vfs.
// See docs/spec/obj.md, docs/spec/wpn.md, docs/spec/ps.md,
// docs/spec/levels-txt.md. Owned by WP-1B (typed loaders package).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/textblock.h"
#include "as3d/vfs.h"

namespace as3d {

// ---------------------------------------------------------------------
// objects/*.obj
// ---------------------------------------------------------------------

// obj "type" statement. VERIFIED-CODE: ParseObject @0x0040a160.
enum class ObjectType { Model = 0, Sprite = 1, Mark = 2, HSprite = 3, VSprite = 4 };

// "player" / "enemy" / "item" are three mutually exclusive spellings of one
// underlying field, not independent booleans (VERIFIED-CODE: all three set
// the same DAT_005545c5 field to 1/2/3 respectively). "item" never appears
// in the shipped data.
enum class ObjectKind { None = 0, Player = 1, Enemy = 2, Item = 3 };

// obj "blend" and ps "blend_mode" share these numeric values (VERIFIED-CODE). The GL blend
// state helper of as3d/gfx.h is a different enum, as3d::GlBlend.
enum class BlendMode { None = 0, Alpha = 1, Add = 2, Filter = 3 };

// obj "envmode". VERIFIED-CODE.
enum class EnvMode { None = 0, Glitter = 1, Chrome = 2, Quad = 3 };

// obj "shadow". VERIFIED-CODE: the *_PLANAR* keywords are parsed to 4/5/6
// and then immediately remapped to alias the *_PLANAR_PROJECTED* slots
// (7/8/9); see docs/spec/obj.md. The plain (non-planar) PROJECTED_* values
// are not remapped.
enum class ShadowMode {
    None = 0,
    Projected = 1,
    ProjectedLow = 2,
    ProjectedHigh = 3,
    Planar = 7,
    PlanarLow = 8,
    PlanarHigh = 9,
    PlanarProjected = 7,
    PlanarProjectedLow = 8,
    PlanarProjectedHigh = 9,
};

// obj "sort". VERIFIED-CODE: value 1 has no keyword and is never produced.
enum class SortMode { Opaque = 0, Trans = 2, Effect = 3 };

// obj "touch" and ps "damage"'s first argument. VERIFIED-CODE.
enum class TouchMode { None = 0, Enemies = 1, Player = 2, All = 3 };

// obj "rflag" and ps "rflag" OR-combine these bits across repeated
// statements (VERIFIED-CODE). ps only recognizes the first four bits;
// RF_NODLIGHT / RF_BANNER are obj-only (unrecognized, and so silently
// ignored, if written under a ps block -- not observed in the data).
enum RFlagBits : u32 {
    RF_NOLIGHTING = 0x1,
    RF_NOCULLING = 0x2,
    RF_NODEPTHTEST = 0x4,
    RF_NODEPTHWRITE = 0x8,
    // Auto-applied whenever "envmap" is set; VERIFIED-CODE bit value, but it
    // has no keyword of its own -- not reachable via any "rflag" statement.
    RF_ENVMAP_IMPLIED = 0x20,
    RF_NODLIGHT = 0x200,
    RF_BANNER = 0x10000,
};

// obj "flag", OR-combined across repeated statements (VERIFIED-CODE).
enum FlagBits : u32 {
    FL_ONGROUND = 0x1,
    // FL_ONGROUND_NORMAL literally ORs in 3 (this bit plus FL_ONGROUND);
    // the original has no separate keyword for this bit alone.
    FL_ONGROUND_NORMAL_EXTRA = 0x2,
    FL_ONWATER = 0x4,
    FL_NODRAW = 0x10,
    FL_TEMPORARY = 0x20,
    FL_NONTARGET = 0x100,
    // Also auto-applied to every object whose type isn't Model
    // (VERIFIED-CODE: `if (type != TYPE_MODEL) flags |= 0x1000;`).
    FL_POINT_COLLISION = 0x1000,
};

// One "attach" statement. See docs/spec/obj.md for the full grammar
// (modifier* target tag) and the resolution order (particle system first,
// then object).
struct AttachDef {
    std::string targetName; // resolves to an object name or a particle-system name
    std::string tagName;    // a model tag name, or the literal "origin"
    std::string idName;     // from the "id NAME" modifier; empty if absent
    bool absolute = false;  // "abs" modifier
    bool nightOnly = false; // "night" modifier: only instantiated in night missions
    int line = 0;
};

struct ObjectDef {
    std::string name; // empty only for a malformed block; see loader warnings
    int line = 0;

    ObjectType type = ObjectType::Model;
    ObjectKind kind = ObjectKind::None;

    std::string model; // "model", a .mdl path
    std::string skin;  // "skin", a .tga path
    std::string envmap; // "envmap", a .tga path; presence also sets RF_NODLIGHT-like envmap flag (see obj.md)
    BlendMode blend = BlendMode::None;
    EnvMode envmode = EnvMode::None;
    u32 rflag = 0; // OR of RFlagBits

    ShadowMode shadow = ShadowMode::None;
    SortMode sort = SortMode::Opaque;

    int health = 0;
    int score = 0;
    int damage = 0;
    u32 flags = 0; // OR of FlagBits, plus FL_POINT_COLLISION auto-applied for non-Model types
    TouchMode touch = TouchMode::None;

    // "scale": never used in the shipped data; VERIFIED-CODE default 0.0
    // (raw zero-init, not remapped to 1.0 anywhere in ParseObject).
    float scale = 0.0f;
    // "bbox_scale": VERIFIED-CODE default (0.7, 0.7, 0.7) when absent.
    float bboxScale[3] = {0.7f, 0.7f, 0.7f};

    bool hasBbox = false; // "min"/"max" always appear together in the data
    float bboxMin[4] = {0, 0, 0, 0};
    float bboxMax[4] = {0, 0, 0, 0};

    bool hasFrames = false;
    int frameStart = 0;
    int frameEnd = 0;

    // "light": radius + RGB multiplier (names GUESS, argument count/types VERIFIED-CODE).
    bool hasLight = false;
    int lightRadius = 0;
    float lightColor[3] = {0, 0, 0};
    // "light_dir": everything "light" has, plus a direction vector and a
    // trailing value (GUESS: a spotlight cone angle in degrees).
    bool hasLightDir = false;
    float lightDir[3] = {0, 0, 0};
    float lightConeAngle = 0.0f;

    std::string script; // "script", a scripts\...\*.scr path; see docs/spec/rcsl-container.md

    std::vector<AttachDef> attachments;

    // Not owned; valid as long as the owning DefDatabase is alive. Lets
    // callers reach any statement, including ones with no typed field yet.
    const TextBlock* source = nullptr;

    const TextStatement* find(const char* key) const { return source ? source->find(key) : nullptr; }
};

// ---------------------------------------------------------------------
// weapons/*.wpn
// ---------------------------------------------------------------------

struct WeaponDef {
    std::string name;
    int line = 0;
    // VERIFIED-CODE (FUN_0040c770 @0x0040c770, object lookup FUN_00409860
    // @0x00409860): both resolve as OBJECT names, never particle systems.
    std::string missileName;
    std::string flashName;
    float speed = 0.0f;
    const TextBlock* source = nullptr;
    const TextStatement* find(const char* key) const { return source ? source->find(key) : nullptr; }
};

// ---------------------------------------------------------------------
// particles/*.ps
// ---------------------------------------------------------------------

// ps "coords". VERIFIED-CODE: the numeric values are NOT in keyword order
// (DECART=0, CILINDER=1, SPHERE=2).
enum class CoordMode { Decart = 0, Cilinder = 1, Sphere = 2 };
enum class DrawMode { None = 0, Vert = 1, Horiz = 2 };
enum class EmitMode { None = 0, Once = 1, Duration = 2 };
enum class AnimMode { None = 0, Linear = 1, Normal = 2, Loop = 3 };
enum class FadeMode { None = 0, Linear = 1, Exp = 2 };

struct ParticleSystemDef {
    std::string name;
    int line = 0;

    std::string texture; // "texture", a .tga path
    int textureFrameW = 0;
    int textureFrameH = 0;

    BlendMode blendMode = BlendMode::None;
    u32 rflag = 0; // OR of RFlagBits (only the first 4 bits are recognized here)
    CoordMode coords = CoordMode::Decart;
    DrawMode drawMode = DrawMode::None;
    EmitMode emitMode = EmitMode::None;

    float emitRate = 0.0f;
    float lifeTime = 0.0f;

    float initOffset[6] = {0, 0, 0, 0, 0, 0};
    float initVelocity[6] = {0, 0, 0, 0, 0, 0};
    float initSize[2] = {0, 0};
    bool hasInitFrame = false;
    int initFrame[2] = {0, 0};
    // VERIFIED-CODE default: alpha (4th component) is 1.0, RGB default 0.
    float initColor[4] = {0, 0, 0, 1.0f};

    float accel[3] = {0, 0, 0};

    FadeMode fadeMode = FadeMode::None;
    float fadeFactor = 1.0f; // VERIFIED-CODE default
    float size = 0.0f;
    AnimMode animMode = AnimMode::None;

    bool hasDamage = false;
    TouchMode damageTouch = TouchMode::None;
    float damageAmount = 0.0f;
    float damageParam2 = 0.0f;
    float damageParam3 = 0.0f;

    const TextBlock* source = nullptr;
    const TextStatement* find(const char* key) const { return source ? source->find(key) : nullptr; }
};

// ---------------------------------------------------------------------
// maps/levels.txt
// ---------------------------------------------------------------------

struct LevelDef {
    int line = 0;
    std::string id;
    std::string name; // absent for the 4 intro/intermission entries
    std::string map;
    std::string music;
    std::string textures; // a directory prefix, e.g. "textures\\desert"
    float hmin = 0.0f;
    float hmax = 0.0f;

    bool hasFog = false;
    float fogColor[3] = {0, 0, 0};
    float fogNear = 0.0f;
    float fogFar = 0.0f;

    float sun[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};

    bool hasWater = false;
    std::string waterTexture;
    float waterLevel = 0.0f;
    float waterAlpha = 0.0f;

    bool night = false;
    // VERIFIED-CODE default: -1 (memset 0xff) when the key is absent.
    int enableHelic = -1;

    bool hasIntermission = false;
    float intermission[6] = {0, 0, 0, 0, 0, 0};

    const TextBlock* source = nullptr;
    const TextStatement* find(const char* key) const { return source ? source->find(key) : nullptr; }
};

// ---------------------------------------------------------------------
// Database
// ---------------------------------------------------------------------

class DefDatabase {
public:
    // Reads objects\*.obj, weapons\*.wpn, particles\*.ps and maps\levels.txt
    // through the Vfs. Returns false only on a hard failure (e.g. no object
    // files found at all); parse/reference problems go to warnings() and
    // never abort the load.
    bool load(Vfs& vfs);

    // Resolves every model/skin/texture/script/music/map path, and every
    // attach/missile/flash reference, appending a warning for each miss.
    // Safe to call more than once; safe to call without load() (does nothing).
    void validate(Vfs& vfs);

    // Case-insensitive name lookup, or null.
    const ObjectDef* findObject(const std::string& name) const;
    const WeaponDef* findWeapon(const std::string& name) const;
    const ParticleSystemDef* findParticleSystem(const std::string& name) const;

    const std::vector<ObjectDef>& objects() const { return objects_; }
    const std::vector<WeaponDef>& weapons() const { return weapons_; }
    const std::vector<ParticleSystemDef>& particleSystems() const { return particleSystems_; }
    const std::vector<LevelDef>& levels() const { return levels_; } // file order

    const std::vector<std::string>& warnings() const { return warnings_; }

private:
    std::vector<ObjectDef> objects_;
    std::vector<WeaponDef> weapons_;
    std::vector<ParticleSystemDef> particleSystems_;
    std::vector<LevelDef> levels_;
    std::vector<std::string> warnings_;

    // Keeps every parsed file alive: ObjectDef::source (etc.) point into
    // the TextBlocks owned here.
    std::vector<TextFile> sourceFiles_;
};

// Canonical text serialization of a definition's typed fields, used to hash
// it for testdata/golden/defs_summary.json. Field order, integer/float/bool
// formatting and the sha1 preimage are specified in docs/spec/obj.md (etc.,
// "Canonical serialization"); tools/ref/defs.py's canonical_*() functions
// must produce byte-identical output for the same data. Floats are
// formatted as "0x" + 8 lowercase hex digits of their IEEE-754 bit pattern.
std::string canonicalObject(const ObjectDef& o);
std::string canonicalWeapon(const WeaponDef& w);
std::string canonicalParticleSystem(const ParticleSystemDef& p);
std::string canonicalLevel(const LevelDef& lv);

} // namespace as3d
