// Typed loader for objects/*.obj, weapons/*.wpn, particles/*.ps and
// maps/levels.txt. Mirrors tools/ref/defs.py exactly (same enum values,
// same defaults, same canonical serialization) -- see docs/spec/obj.md,
// wpn.md, ps.md, levels-txt.md.
#include "as3d/defs.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#include "defs_enums.h"

namespace as3d {

namespace {

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const TextToken& argAt(const std::vector<TextToken>& args, size_t i) {
    static const TextToken kEmpty{};
    return i < args.size() ? args[i] : kEmpty;
}

float numAt(const std::vector<TextToken>& args, size_t i) { return argAt(args, i).asFloat(); }
int intAt(const std::vector<TextToken>& args, size_t i) { return argAt(args, i).asInt(); }
const std::string& textAt(const std::vector<TextToken>& args, size_t i) { return argAt(args, i).text; }

AttachDef parseAttach(const std::vector<TextToken>& args, int line) {
    AttachDef ad;
    ad.line = line;
    size_t i = 0, n = args.size();
    while (i < n) {
        const TextToken& t = args[i];
        if (!t.quoted && t.text == "abs") {
            ad.absolute = true;
            i++;
        } else if (!t.quoted && t.text == "night") {
            ad.nightOnly = true;
            i++;
        } else if (!t.quoted && t.text == "id" && i + 1 < n) {
            ad.idName = args[i + 1].text;
            i += 2;
        } else {
            break;
        }
    }
    if (i < n) ad.targetName = args[i++].text;
    if (i < n) ad.tagName = args[i++].text;
    return ad;
}

void loadObjectsFile(const TextFile& tf, const std::string& baseName, std::vector<ObjectDef>& out,
                      std::vector<std::string>& warnings) {
    using namespace defs_detail;
    for (const auto& b : tf.blocks) {
        ObjectDef o;
        o.name = b.name;
        o.line = b.line;
        o.source = &b;
        bool capped = false;
        for (const auto& s : b.statements) {
            std::string key = toLower(s.key);
            const auto& a = s.args;
            if (key == "type") {
                o.type = parseObjectType(textAt(a, 0));
            } else if (key == "model") {
                o.model = textAt(a, 0);
            } else if (key == "skin") {
                o.skin = textAt(a, 0);
            } else if (key == "envmap") {
                o.envmap = textAt(a, 0);
                o.rflag |= RF_ENVMAP_IMPLIED; // see docs/spec/obj.md
            } else if (key == "blend") {
                o.blend = parseBlendMode(textAt(a, 0));
            } else if (key == "envmode") {
                o.envmode = parseEnvMode(textAt(a, 0));
            } else if (key == "rflag") {
                if (!a.empty()) o.rflag |= parseObjRFlag(textAt(a, 0));
            } else if (key == "shadow") {
                o.shadow = parseShadowMode(textAt(a, 0));
            } else if (key == "sort") {
                o.sort = parseSortMode(textAt(a, 0));
            } else if (key == "health") {
                o.health = intAt(a, 0);
            } else if (key == "damage") {
                o.damage = intAt(a, 0);
            } else if (key == "score") {
                o.score = intAt(a, 0);
            } else if (key == "flag") {
                if (!a.empty()) o.flags |= parseFlag(textAt(a, 0));
            } else if (key == "touch") {
                o.touch = parseTouchMode(textAt(a, 0));
            } else if (key == "player") {
                o.kind = ObjectKind::Player;
            } else if (key == "enemy") {
                o.kind = ObjectKind::Enemy;
            } else if (key == "item") {
                o.kind = ObjectKind::Item;
            } else if (key == "scale") {
                o.scale = numAt(a, 0);
            } else if (key == "bbox_scale") {
                o.bboxScale[0] = numAt(a, 0);
                o.bboxScale[1] = numAt(a, 1);
                o.bboxScale[2] = numAt(a, 2);
            } else if (key == "min") {
                o.hasBbox = true;
                for (int i = 0; i < 4; i++) o.bboxMin[i] = numAt(a, static_cast<size_t>(i));
            } else if (key == "max") {
                o.hasBbox = true;
                for (int i = 0; i < 4; i++) o.bboxMax[i] = numAt(a, static_cast<size_t>(i));
            } else if (key == "frames") {
                o.hasFrames = true;
                o.frameStart = intAt(a, 0);
                o.frameEnd = intAt(a, 1);
            } else if (key == "light") {
                o.hasLight = true;
                o.lightRadius = intAt(a, 0);
                o.lightColor[0] = numAt(a, 1);
                o.lightColor[1] = numAt(a, 2);
                o.lightColor[2] = numAt(a, 3);
            } else if (key == "light_dir") {
                o.hasLight = true;
                o.hasLightDir = true;
                o.lightRadius = intAt(a, 0);
                o.lightColor[0] = numAt(a, 1);
                o.lightColor[1] = numAt(a, 2);
                o.lightColor[2] = numAt(a, 3);
                o.lightDir[0] = numAt(a, 4);
                o.lightDir[1] = numAt(a, 5);
                o.lightDir[2] = numAt(a, 6);
                o.lightConeAngle = static_cast<float>(intAt(a, 7));
            } else if (key == "script") {
                o.script = textAt(a, 0);
            } else if (key == "attach") {
                if (o.attachments.size() < 64) {
                    o.attachments.push_back(parseAttach(a, s.line));
                } else {
                    capped = true;
                }
            }
            // Any other key would be genuinely unrecognized; none occur in
            // the shipped data (VERIFIED-DATA, tools/ref/test_defs.py).
        }
        if (capped) {
            warnings.push_back(baseName + ": object '" + b.name + "' at line " +
                                std::to_string(b.line) +
                                ": too many attach statements (>64), extra ones dropped");
        }
        out.push_back(std::move(o));
    }
}

void loadWeaponsFile(const TextFile& tf, std::vector<WeaponDef>& out) {
    for (const auto& b : tf.blocks) {
        WeaponDef w;
        w.name = b.name;
        w.line = b.line;
        w.source = &b;
        for (const auto& s : b.statements) {
            std::string key = toLower(s.key);
            const auto& a = s.args;
            if (key == "missile") {
                w.missileName = textAt(a, 0);
            } else if (key == "flash") {
                w.flashName = textAt(a, 0);
            } else if (key == "speed") {
                w.speed = numAt(a, 0);
            }
        }
        out.push_back(std::move(w));
    }
}

void loadParticleSystemsFile(const TextFile& tf, std::vector<ParticleSystemDef>& out) {
    using namespace defs_detail;
    for (const auto& b : tf.blocks) {
        ParticleSystemDef p;
        p.name = b.name;
        p.line = b.line;
        p.source = &b;
        for (const auto& s : b.statements) {
            std::string key = toLower(s.key);
            const auto& a = s.args;
            if (key == "texture") {
                p.texture = textAt(a, 0);
                p.textureFrameW = intAt(a, 1);
                p.textureFrameH = intAt(a, 2);
            } else if (key == "blend_mode") {
                p.blendMode = parseBlendMode(textAt(a, 0));
            } else if (key == "rflag") {
                if (!a.empty()) p.rflag |= parsePsRFlag(textAt(a, 0));
            } else if (key == "coords") {
                p.coords = parseCoordMode(textAt(a, 0));
            } else if (key == "draw_mode") {
                p.drawMode = parseDrawMode(textAt(a, 0));
            } else if (key == "emit_mode") {
                p.emitMode = parseEmitMode(textAt(a, 0));
            } else if (key == "emit_rate") {
                p.emitRate = numAt(a, 0);
            } else if (key == "life_time") {
                p.lifeTime = numAt(a, 0);
            } else if (key == "init_offset") {
                for (int i = 0; i < 6; i++) p.initOffset[i] = numAt(a, static_cast<size_t>(i));
            } else if (key == "init_velocity") {
                for (int i = 0; i < 6; i++) p.initVelocity[i] = numAt(a, static_cast<size_t>(i));
            } else if (key == "init_size") {
                p.initSize[0] = numAt(a, 0);
                p.initSize[1] = numAt(a, 1);
            } else if (key == "init_frame") {
                p.hasInitFrame = true;
                p.initFrame[0] = intAt(a, 0);
                p.initFrame[1] = intAt(a, 1);
            } else if (key == "init_color") {
                for (int i = 0; i < 4; i++) p.initColor[i] = numAt(a, static_cast<size_t>(i));
            } else if (key == "accel") {
                for (int i = 0; i < 3; i++) p.accel[i] = numAt(a, static_cast<size_t>(i));
            } else if (key == "fade_mode") {
                p.fadeMode = parseFadeMode(textAt(a, 0));
            } else if (key == "fade_factor") {
                p.fadeFactor = numAt(a, 0);
            } else if (key == "size") {
                p.size = numAt(a, 0);
            } else if (key == "anim_mode") {
                p.animMode = parseAnimMode(textAt(a, 0));
            } else if (key == "damage") {
                p.hasDamage = true;
                p.damageTouch = parseTouchMode(textAt(a, 0));
                p.damageAmount = numAt(a, 1);
                p.damageParam2 = numAt(a, 2);
                p.damageParam3 = numAt(a, 3);
            }
        }
        out.push_back(std::move(p));
    }
}

void loadLevelsFile(const TextFile& tf, std::vector<LevelDef>& out) {
    for (const auto& b : tf.blocks) {
        LevelDef lv;
        lv.line = b.line;
        lv.source = &b;
        for (const auto& s : b.statements) {
            std::string key = toLower(s.key);
            const auto& a = s.args;
            if (key == "id") {
                lv.id = textAt(a, 0);
            } else if (key == "name") {
                lv.name = textAt(a, 0);
            } else if (key == "map") {
                lv.map = textAt(a, 0);
            } else if (key == "music") {
                lv.music = textAt(a, 0);
            } else if (key == "textures") {
                lv.textures = textAt(a, 0);
            } else if (key == "hmin") {
                lv.hmin = numAt(a, 0);
            } else if (key == "hmax") {
                lv.hmax = numAt(a, 0);
            } else if (key == "fog") {
                lv.hasFog = true;
                lv.fogColor[0] = numAt(a, 0);
                lv.fogColor[1] = numAt(a, 1);
                lv.fogColor[2] = numAt(a, 2);
                lv.fogNear = numAt(a, 3);
                lv.fogFar = numAt(a, 4);
            } else if (key == "sun") {
                for (int i = 0; i < 9; i++) lv.sun[i] = numAt(a, static_cast<size_t>(i));
            } else if (key == "water") {
                lv.hasWater = true;
                lv.waterTexture = textAt(a, 0);
                lv.waterLevel = numAt(a, 1);
                lv.waterAlpha = numAt(a, 2);
            } else if (key == "night") {
                lv.night = true;
            } else if (key == "enablehelic") {
                lv.enableHelic = a.empty() ? -1 : intAt(a, 0);
            } else if (key == "intermission") {
                lv.hasIntermission = true;
                for (int i = 0; i < 6; i++) lv.intermission[i] = numAt(a, static_cast<size_t>(i));
            }
        }
        out.push_back(std::move(lv));
    }
}

std::string basenameOf(const std::string& path) {
    size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

} // namespace

bool DefDatabase::load(Vfs& vfs) {
    std::vector<std::string> objFiles, wpnFiles, psFiles;
    for (auto& p : vfs.list("objects\\")) {
        if (p.size() > 4 && p.compare(p.size() - 4, 4, ".obj") == 0) objFiles.push_back(p);
    }
    for (auto& p : vfs.list("weapons\\")) {
        if (p.size() > 4 && p.compare(p.size() - 4, 4, ".wpn") == 0) wpnFiles.push_back(p);
    }
    for (auto& p : vfs.list("particles\\")) {
        if (p.size() > 3 && p.compare(p.size() - 3, 3, ".ps") == 0) psFiles.push_back(p);
    }
    std::sort(objFiles.begin(), objFiles.end());
    std::sort(wpnFiles.begin(), wpnFiles.end());
    std::sort(psFiles.begin(), psFiles.end());

    if (objFiles.empty()) {
        warnings_.push_back("no objects\\*.obj files found");
        return false;
    }

    // Reserve so sourceFiles_ doesn't reallocate mid-loop more than needed;
    // correctness does not depend on this (TextFile's vectors are moved,
    // not copied, on any reallocation, so TextBlock addresses stay valid).
    sourceFiles_.reserve(objFiles.size() + wpnFiles.size() + psFiles.size() + 1);

    for (auto& path : objFiles) {
        Blob blob;
        if (!vfs.read(path, blob)) continue;
        sourceFiles_.push_back(parseTextBlocks(blob.data(), blob.size()));
        for (auto& e : sourceFiles_.back().errors) warnings_.push_back(path + ": " + e);
        loadObjectsFile(sourceFiles_.back(), basenameOf(path), objects_, warnings_);
    }
    for (auto& path : wpnFiles) {
        Blob blob;
        if (!vfs.read(path, blob)) continue;
        sourceFiles_.push_back(parseTextBlocks(blob.data(), blob.size()));
        for (auto& e : sourceFiles_.back().errors) warnings_.push_back(path + ": " + e);
        loadWeaponsFile(sourceFiles_.back(), weapons_);
    }
    for (auto& path : psFiles) {
        Blob blob;
        if (!vfs.read(path, blob)) continue;
        sourceFiles_.push_back(parseTextBlocks(blob.data(), blob.size()));
        for (auto& e : sourceFiles_.back().errors) warnings_.push_back(path + ": " + e);
        loadParticleSystemsFile(sourceFiles_.back(), particleSystems_);
    }
    {
        Blob blob;
        if (vfs.read("maps\\levels.txt", blob)) {
            sourceFiles_.push_back(parseTextBlocks(blob.data(), blob.size()));
            for (auto& e : sourceFiles_.back().errors) {
                warnings_.push_back(std::string("maps\\levels.txt: ") + e);
            }
            loadLevelsFile(sourceFiles_.back(), levels_);
        } else {
            warnings_.push_back("maps\\levels.txt not found");
        }
    }

    // objects_/weapons_/particleSystems_ never reallocate again after this
    // point (nothing appends to them later), and DefDef::source pointers
    // into sourceFiles_ are unaffected either way (see reserve() note).
    return true;
}

const ObjectDef* DefDatabase::findObject(const std::string& name) const {
    std::string key = toLower(name);
    for (const auto& o : objects_) {
        if (!o.name.empty() && toLower(o.name) == key) return &o; // first-defined wins (VERIFIED-CODE)
    }
    return nullptr;
}

const WeaponDef* DefDatabase::findWeapon(const std::string& name) const {
    std::string key = toLower(name);
    for (const auto& w : weapons_) {
        if (!w.name.empty() && toLower(w.name) == key) return &w;
    }
    return nullptr;
}

const ParticleSystemDef* DefDatabase::findParticleSystem(const std::string& name) const {
    std::string key = toLower(name);
    for (const auto& p : particleSystems_) {
        if (!p.name.empty() && toLower(p.name) == key) return &p;
    }
    return nullptr;
}

namespace {

bool dirHasAny(Vfs& vfs, const std::string& rel) {
    if (rel.empty()) return true;
    std::string prefix = normalizePath(rel);
    if (!prefix.empty() && prefix.back() != '\\') prefix += '\\';
    return !vfs.list(prefix).empty();
}

} // namespace

void DefDatabase::validate(Vfs& vfs) {
    for (const auto& o : objects_) {
        if (!o.model.empty() && !vfs.exists(o.model)) {
            warnings_.push_back("object '" + o.name + "': model '" + o.model + "' not found");
        }
        if (!o.skin.empty() && !vfs.exists(o.skin)) {
            warnings_.push_back("object '" + o.name + "': skin '" + o.skin + "' not found");
        }
        if (!o.envmap.empty() && !vfs.exists(o.envmap)) {
            warnings_.push_back("object '" + o.name + "': envmap '" + o.envmap + "' not found");
        }
        if (!o.script.empty() && !vfs.exists(o.script)) {
            warnings_.push_back("object '" + o.name + "': script '" + o.script + "' not found");
        }
        for (const auto& at : o.attachments) {
            if (at.targetName.empty()) continue;
            // VERIFIED-CODE resolution order (G_InitObject @0x00409ba0):
            // particle system first, then object.
            if (findParticleSystem(at.targetName) == nullptr && findObject(at.targetName) == nullptr) {
                warnings_.push_back("object '" + o.name + "': attach target '" + at.targetName +
                                     "' at line " + std::to_string(at.line) +
                                     " does not resolve to any object or particle system");
            }
        }
    }
    for (const auto& w : weapons_) {
        if (!w.missileName.empty() && findObject(w.missileName) == nullptr) {
            warnings_.push_back("weapon '" + w.name + "': missile '" + w.missileName +
                                 "' does not resolve to an object");
        }
        if (!w.flashName.empty() && findObject(w.flashName) == nullptr) {
            warnings_.push_back("weapon '" + w.name + "': flash '" + w.flashName +
                                 "' does not resolve to an object");
        }
    }
    for (const auto& p : particleSystems_) {
        if (!p.texture.empty() && !vfs.exists(p.texture)) {
            warnings_.push_back("particle system '" + p.name + "': texture '" + p.texture +
                                 "' not found");
        }
    }
    for (const auto& lv : levels_) {
        std::string label = lv.id.empty() ? ("(line " + std::to_string(lv.line) + ")") : lv.id;
        if (!lv.map.empty() && !vfs.exists(lv.map)) {
            warnings_.push_back("level '" + label + "': map '" + lv.map + "' not found");
        }
        if (!lv.music.empty() && !vfs.exists(lv.music)) {
            warnings_.push_back("level '" + label + "': music '" + lv.music + "' not found");
        }
        if (!lv.textures.empty() && !dirHasAny(vfs, lv.textures)) {
            warnings_.push_back("level '" + label + "': textures '" + lv.textures +
                                 "' has no files");
        }
        if (lv.hasWater && !lv.waterTexture.empty() && !vfs.exists(lv.waterTexture)) {
            warnings_.push_back("level '" + label + "': water texture '" + lv.waterTexture +
                                 "' not found");
        }
    }
}

// ---------------------------------------------------------------------
// Canonical serialization. See docs/spec/*.md "Canonical serialization";
// must match tools/ref/defs.py's canonical_*() byte for byte.
// ---------------------------------------------------------------------

namespace {

std::string fhex(float v) {
    u32 bits;
    std::memcpy(&bits, &v, 4);
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08x", bits);
    return buf;
}

std::string uhex(u32 v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08x", v);
    return buf;
}

template <size_t N>
std::string farr(const float (&v)[N]) {
    std::string out;
    for (size_t i = 0; i < N; i++) {
        if (i) out += ',';
        out += fhex(v[i]);
    }
    return out;
}

template <size_t N>
std::string iarr(const int (&v)[N]) {
    std::string out;
    for (size_t i = 0; i < N; i++) {
        if (i) out += ',';
        out += std::to_string(v[i]);
    }
    return out;
}

void line(std::string& out, const char* key, const std::string& value) {
    if (!out.empty()) out += '\n';
    out += key;
    out += '=';
    out += value;
}

} // namespace

std::string canonicalObject(const ObjectDef& o) {
    std::string out;
    line(out, "name", o.name);
    line(out, "type", std::to_string(static_cast<int>(o.type)));
    line(out, "kind", std::to_string(static_cast<int>(o.kind)));
    line(out, "model", o.model);
    line(out, "skin", o.skin);
    line(out, "envmap", o.envmap);
    line(out, "blend", std::to_string(static_cast<int>(o.blend)));
    line(out, "envmode", std::to_string(static_cast<int>(o.envmode)));
    line(out, "rflag", uhex(o.rflag));
    line(out, "shadow", std::to_string(static_cast<int>(o.shadow)));
    line(out, "sort", std::to_string(static_cast<int>(o.sort)));
    line(out, "health", std::to_string(o.health));
    line(out, "score", std::to_string(o.score));
    line(out, "damage", std::to_string(o.damage));
    line(out, "flags", uhex(o.flags));
    line(out, "touch", std::to_string(static_cast<int>(o.touch)));
    line(out, "scale", fhex(o.scale));
    line(out, "bboxScale", farr(o.bboxScale));
    line(out, "hasBbox", o.hasBbox ? "1" : "0");
    line(out, "bboxMin", farr(o.bboxMin));
    line(out, "bboxMax", farr(o.bboxMax));
    line(out, "hasFrames", o.hasFrames ? "1" : "0");
    line(out, "frameStart", std::to_string(o.frameStart));
    line(out, "frameEnd", std::to_string(o.frameEnd));
    line(out, "hasLight", o.hasLight ? "1" : "0");
    line(out, "lightRadius", std::to_string(o.lightRadius));
    line(out, "lightColor", farr(o.lightColor));
    line(out, "hasLightDir", o.hasLightDir ? "1" : "0");
    line(out, "lightDir", farr(o.lightDir));
    line(out, "lightConeAngle", fhex(o.lightConeAngle));
    line(out, "script", o.script);
    line(out, "attachCount", std::to_string(o.attachments.size()));
    for (size_t i = 0; i < o.attachments.size(); i++) {
        const auto& a = o.attachments[i];
        std::string p = "attach" + std::to_string(i) + ".";
        line(out, (p + "target").c_str(), a.targetName);
        line(out, (p + "tag").c_str(), a.tagName);
        line(out, (p + "id").c_str(), a.idName);
        line(out, (p + "abs").c_str(), a.absolute ? "1" : "0");
        line(out, (p + "night").c_str(), a.nightOnly ? "1" : "0");
    }
    return out;
}

std::string canonicalWeapon(const WeaponDef& w) {
    std::string out;
    line(out, "name", w.name);
    line(out, "missile", w.missileName);
    line(out, "flash", w.flashName);
    line(out, "speed", fhex(w.speed));
    return out;
}

std::string canonicalParticleSystem(const ParticleSystemDef& p) {
    std::string out;
    line(out, "name", p.name);
    line(out, "texture", p.texture);
    line(out, "textureFrameW", std::to_string(p.textureFrameW));
    line(out, "textureFrameH", std::to_string(p.textureFrameH));
    line(out, "blendMode", std::to_string(static_cast<int>(p.blendMode)));
    line(out, "rflag", uhex(p.rflag));
    line(out, "coords", std::to_string(static_cast<int>(p.coords)));
    line(out, "drawMode", std::to_string(static_cast<int>(p.drawMode)));
    line(out, "emitMode", std::to_string(static_cast<int>(p.emitMode)));
    line(out, "emitRate", fhex(p.emitRate));
    line(out, "lifeTime", fhex(p.lifeTime));
    line(out, "initOffset", farr(p.initOffset));
    line(out, "initVelocity", farr(p.initVelocity));
    line(out, "initSize", farr(p.initSize));
    line(out, "hasInitFrame", p.hasInitFrame ? "1" : "0");
    line(out, "initFrame", iarr(p.initFrame));
    line(out, "initColor", farr(p.initColor));
    line(out, "accel", farr(p.accel));
    line(out, "fadeMode", std::to_string(static_cast<int>(p.fadeMode)));
    line(out, "fadeFactor", fhex(p.fadeFactor));
    line(out, "size", fhex(p.size));
    line(out, "animMode", std::to_string(static_cast<int>(p.animMode)));
    line(out, "hasDamage", p.hasDamage ? "1" : "0");
    line(out, "damageTouch", std::to_string(static_cast<int>(p.damageTouch)));
    line(out, "damageAmount", fhex(p.damageAmount));
    line(out, "damageParam2", fhex(p.damageParam2));
    line(out, "damageParam3", fhex(p.damageParam3));
    return out;
}

std::string canonicalLevel(const LevelDef& lv) {
    std::string out;
    line(out, "id", lv.id);
    line(out, "name", lv.name);
    line(out, "map", lv.map);
    line(out, "music", lv.music);
    line(out, "textures", lv.textures);
    line(out, "hmin", fhex(lv.hmin));
    line(out, "hmax", fhex(lv.hmax));
    line(out, "hasFog", lv.hasFog ? "1" : "0");
    line(out, "fogColor", farr(lv.fogColor));
    line(out, "fogNear", fhex(lv.fogNear));
    line(out, "fogFar", fhex(lv.fogFar));
    line(out, "sun", farr(lv.sun));
    line(out, "hasWater", lv.hasWater ? "1" : "0");
    line(out, "waterTexture", lv.waterTexture);
    line(out, "waterLevel", fhex(lv.waterLevel));
    line(out, "waterAlpha", fhex(lv.waterAlpha));
    line(out, "night", lv.night ? "1" : "0");
    line(out, "enableHelic", std::to_string(lv.enableHelic));
    line(out, "hasIntermission", lv.hasIntermission ? "1" : "0");
    line(out, "intermission", farr(lv.intermission));
    return out;
}

} // namespace as3d
