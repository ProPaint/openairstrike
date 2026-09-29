// Loads a level (map + levels.txt style) by mission reference.
#include <cctype>
#include <cstdlib>

#include "as3d/defs.h"
#include "as3d/terrain.h"

namespace as3d {

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string baseName(const std::string& p) {
    size_t i = p.find_last_of("/\\");
    return lower(i == std::string::npos ? p : p.substr(i + 1));
}
} // namespace

bool loadLevelByRef(Vfs& vfs, const std::string& ref, LoadedLevel& out, std::string* error) {
    out = LoadedLevel{};
    DefDatabase db;
    // Only levels.txt is needed; DefDatabase::load also parses objects (needs at least one .obj).
    db.load(vfs);
    const LevelDef* found = nullptr;
    std::string r = ref;
    bool numeric = !r.empty();
    for (char c : r) if (!std::isdigit(static_cast<unsigned char>(c))) numeric = false;
    std::string id = numeric ? "mission" + std::to_string(std::atoi(r.c_str())) : lower(r);
    for (const LevelDef& d : db.levels()) {
        if (lower(d.id) == id) { found = &d; break; }
    }
    std::string mapPath;
    if (found) {
        mapPath = found->map;
    } else {
        mapPath = ref;
        for (const LevelDef& d : db.levels()) {
            if (baseName(d.map) == baseName(ref)) { found = &d; break; }
        }
    }
    Blob blob;
    if (!vfs.read(mapPath, blob)) {
        if (error) *error = "level '" + ref + "' not found (map path '" + mapPath + "')";
        return false;
    }
    if (!loadLevel(blob.data(), blob.size(), out.data, error)) return false;
    if (found) out.style = terrainStyleFromDef(*found);
    else {
        out.style.mapPath = mapPath;
        out.style.texturesDir = "textures\\desert";
        out.style.hmin = -120.0f;
        out.style.hmax = 130.0f;
        out.style.sun[0] = out.style.sun[1] = out.style.sun[2] = 0.9f;
        out.style.sun[5] = 1.0f;
        out.style.sun[6] = out.style.sun[7] = out.style.sun[8] = 0.3f;
    }
    return true;
}

} // namespace as3d
