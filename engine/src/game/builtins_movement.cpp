// Movement, orientation, terrain, waiting and waypoint builtins
// (docs/spec/rcsl-builtins-semantics.md family C, sleep; hmap.md "Waypoint paths").
#include <cmath>

#include "builtins_common.h"
#include "world_path.h"

namespace as3d {
namespace builtins {

namespace {

float ft(World& w) { return w.frametimeGlobal; }

void addField(World& w, int k, float delta) {
    int s = selfOf(w);
    if (s < 0) return;
    Entity& e = w.entity(s);
    e.setF(k, e.f(k) + delta);
}

void bMove(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    float v[3];
    if (!vecArg(a, 0, v)) return;
    for (int k = 0; k < 3; ++k) addField(w, F_ORIGIN + k, v[k] * ft(w));
}
void bMovex(BuiltinArgs& a, void*) { World& w = worldOf(a); addField(w, F_ORIGIN, a.f32(0) * ft(w)); }
void bMovey(BuiltinArgs& a, void*) { World& w = worldOf(a); addField(w, F_ORIGIN + 1, a.f32(0) * ft(w)); }
void bMovez(BuiltinArgs& a, void*) { World& w = worldOf(a); addField(w, F_ORIGIN + 2, a.f32(0) * ft(w)); }

void bRotate(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    float v[3];
    if (!vecArg(a, 0, v)) return;
    for (int k = 0; k < 3; ++k) addField(w, F_ANGLES + k, v[k] * ft(w));
}
void bRotatex(BuiltinArgs& a, void*) { World& w = worldOf(a); addField(w, F_ANGLES, a.f32(0) * ft(w)); }
void bRotatey(BuiltinArgs& a, void*) { World& w = worldOf(a); addField(w, F_ANGLES + 1, a.f32(0) * ft(w)); }
void bRotatez(BuiltinArgs& a, void*) { World& w = worldOf(a); addField(w, F_ANGLES + 2, a.f32(0) * ft(w)); }

void bSleep(BuiltinArgs& a, void*) {
    if (a.latent()) a.setDone(false);
}

void bTerrainHeight(BuiltinArgs& a, void*) {
    a.setReturnFloat(worldOf(a).terrainHeight(a.f32(0), a.f32(1)));
}

// Inverse of a row-major 3x3 matrix by cofactors, reproducing the defect of 0x41e730:
// element 7 uses m8 where m7 belongs (rcsl-builtins-semantics.md D8).
bool defectiveInverse(const float m[9], float inv[9]) {
    float det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (det == 0.0f) return false;
    inv[0] = (m[4] * m[8] - m[5] * m[7]) / det;
    inv[1] = (m[2] * m[7] - m[1] * m[8]) / det;
    inv[2] = (m[1] * m[5] - m[2] * m[4]) / det;
    inv[3] = (m[5] * m[6] - m[3] * m[8]) / det;
    inv[4] = (m[0] * m[8] - m[2] * m[6]) / det;
    inv[5] = (m[2] * m[3] - m[0] * m[5]) / det;
    inv[6] = (m[3] * m[7] - m[4] * m[6]) / det;
    inv[7] = (m[1] * m[6] - m[0] * m[8]) / det; // the defect
    inv[8] = (m[0] * m[4] - m[1] * m[3]) / det;
    return true;
}

// One turning step toward error e on angle field k (RotateTo steps 5-6). Returns true when
// the axis counts as arrived (the error was already below 0.1 degree).
bool turnAxis(Entity& e, int k, float err, float step) {
    if (err > 180.0f) err -= 360.0f;
    if (err < -180.0f) err += 360.0f;
    if (std::fabs(err) < 0.1f) {
        e.setF(k, e.f(k) + err);
        return true;
    }
    if (std::fabs(err) < step) e.setF(k, e.f(k) + err); // snap, not counted as arrived
    else e.setF(k, e.f(k) + (err >= 0.0f ? step : -step));
    return false;
}

// RotateTo(target, axes): one step (rcsl-builtins-semantics.md 49). Returns the done flag.
bool rotateToStep(World& w, BuiltinArgs& a, int& mask) {
    mask = intArg(a, 1);
    int s = selfOf(w);
    int t = entityArg(w, a, 0);
    if (s < 0 || t < 0) return false; // 0 or stale target: nothing (engine decision)
    Entity& e = w.entity(s);
    float step = e.f(F_WP_TURN_RATE) * ft(w);
    Vec3 d = w.entity(t).v3(F_ORIGIN) - e.v3(F_ORIGIN);
    float m[9], inv[9];
    for (int k = 0; k < 9; ++k) m[k] = e.f(F_AXIS + k);
    float local[3] = {0.0f, 0.0f, 0.0f};
    if (defectiveInverse(m, inv)) {
        for (int j = 0; j < 3; ++j) local[j] = inv[j] * d.x + inv[3 + j] * d.y + inv[6 + j] * d.z;
    }
    float len = std::sqrt(local[0] * local[0] + local[1] * local[1] + local[2] * local[2]);
    if (len != 0.0f) {
        for (float& c : local) c /= len;
    }
    float ang[3];
    vecToAngles(local[0], local[1], local[2], ang);
    int arrived = 0;
    if (mask & 1) {
        if (turnAxis(e, F_ANGLES + 2, ang[2], step)) arrived |= 1;
    }
    if (mask & 2) {
        if (turnAxis(e, F_ANGLES, ang[0], step)) arrived |= 2;
    }
    return arrived == mask;
}

void bRotateTo(BuiltinArgs& a, void*) {
    int mask;
    bool done = rotateToStep(worldOf(a), a, mask);
    if (a.latent()) a.setDone(done);
}

void bRotateToClamp(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    float mx = a.f32(2), mn = a.f32(3);
    int mask;
    bool done = rotateToStep(w, a, mask);
    int s = selfOf(w);
    if (s >= 0 && entityArg(w, a, 0) >= 0) {
        Entity& e = w.entity(s);
        const int fieldsFor[2] = {F_ANGLES + 2, F_ANGLES};
        for (int bit = 0; bit < 2; ++bit) {
            if (!(mask & (1 << bit))) continue;
            float v = e.f(fieldsFor[bit]);
            if (v > mx) v = mx;
            if (v < mn) v = mn;
            e.setF(fieldsFor[bit], v);
        }
    }
    if (a.latent()) a.setDone(done);
}

// MoveToNextWP(align) (rcsl-builtins-semantics.md 53, steps 1-7).
void bMoveToNextWP(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int s = selfOf(w);
    bool done = true;
    if (s >= 0 && w.entity(s).path) {
        Entity& e = w.entity(s);
        const GamePath& p = *e.path;
        if (e.pathFinished) {
            w.setActive(s, false);
        } else {
            float dist = e.pathDistance + e.f(F_WP_SPEED) * ft(w);
            e.pathDistance = dist;
            float n = dist * 0.125f;
            bool evaluate = true;
            if (n > static_cast<float>(p.sampleCount() - 1)) {
                e.pathDistance = 0.0f;
                e.pathLastNode = 0;
                if (!p.loops()) {
                    e.pathFinished = true;
                    e.setF(F_WP_WAIT, p.delay(p.waypointCount() - 1));
                    evaluate = false;
                } else {
                    dist = 0.0f;
                }
            }
            if (evaluate) {
                GamePath::Eval ev = p.evaluate(dist);
                e.setF(F_ORIGIN, ev.pos.x);
                e.setF(F_ORIGIN + 1, ev.pos.y);
                if (ev.segment == e.pathLastNode) {
                    e.setF(F_WP_WAIT, 0.0f);
                    if (intArg(a, 0) != 0) e.setF(F_ANGLES + 2, ev.heading);
                    float bank = e.f(F_WP_BANK);
                    if (bank != 0.0f) e.setF(F_ANGLES + 1, ev.bank * bank / 4.8f);
                    done = false;
                } else {
                    e.pathLastNode = ev.segment;
                    e.setF(F_WP_WAIT, p.delay(ev.segment));
                }
            }
        }
    }
    if (a.latent()) a.setDone(done);
}

// RotateToNextWP() (rcsl-builtins-semantics.md 55): turn toward the waypoint that ends
// the current segment.
void bRotateToNextWP(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int s = selfOf(w);
    bool done = true;
    if (s >= 0 && w.entity(s).path) {
        Entity& e = w.entity(s);
        const GamePath& p = *e.path;
        int seg = p.segmentAt(e.pathDistance);
        Vec2 wp = p.waypointPos(seg + 1);
        float dx = wp.x - e.f(F_ORIGIN), dy = wp.y - e.f(F_ORIGIN + 1);
        float len = std::sqrt(dx * dx + dy * dy);
        if (len != 0.0f) {
            dx /= len;
            dy /= len;
        }
        float ang[3];
        vecToAngles(dx, dy, 0.0f, ang);
        float t = ang[2];
        float err = t - e.f(F_ANGLES + 2);
        for (int guard = 0; err < 0.0f && guard < 64; ++guard) err += 360.0f;
        for (int guard = 0; err > 360.0f && guard < 64; ++guard) err -= 360.0f;
        if (err > 180.0f) err -= 360.0f;
        float step = e.f(F_WP_TURN_RATE) * ft(w);
        if (std::fabs(err) < 0.1f) {
            e.setF(F_ANGLES + 2, t);
        } else if (std::fabs(err) < step) {
            e.setF(F_ANGLES + 2, t);
            done = false;
        } else {
            e.setF(F_ANGLES + 2, e.f(F_ANGLES + 2) + (err >= 0.0f ? step : -step));
            done = false;
        }
    }
    if (a.latent()) a.setDone(done);
}

void bGetWaypointDelay(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int s = selfOf(w);
    float v = 0.0f;
    if (s >= 0 && w.entity(s).path) v = w.entity(s).path->delay(intArg(a, 0));
    a.setReturnFloat(v);
}

const BuiltinDesc kTable[] = {
    {"move", 1, bMove, nullptr, BuiltinStatus::Implemented},
    {"movex", 1, bMovex, nullptr, BuiltinStatus::Implemented},
    {"movey", 1, bMovey, nullptr, BuiltinStatus::Implemented},
    {"movez", 1, bMovez, nullptr, BuiltinStatus::Implemented},
    {"rotate", 1, bRotate, nullptr, BuiltinStatus::Implemented},
    {"rotatex", 1, bRotatex, nullptr, BuiltinStatus::Implemented},
    {"rotatey", 1, bRotatey, nullptr, BuiltinStatus::Implemented},
    {"rotatez", 1, bRotatez, nullptr, BuiltinStatus::Implemented},
    {"sleep", 0, bSleep, nullptr, BuiltinStatus::Implemented},
    {"TerrainHeight", 2, bTerrainHeight, nullptr, BuiltinStatus::Implemented},
    {"RotateTo", 2, bRotateTo, nullptr, BuiltinStatus::Implemented},
    {"lRotateTo", 2, bRotateTo, nullptr, BuiltinStatus::Implemented},
    {"RotateToClamp", 4, bRotateToClamp, nullptr, BuiltinStatus::Implemented},
    {"lRotateToClamp", 4, bRotateToClamp, nullptr, BuiltinStatus::Implemented},
    {"MoveToNextWP", 1, bMoveToNextWP, nullptr, BuiltinStatus::Implemented},
    {"lMoveToNextWP", 1, bMoveToNextWP, nullptr, BuiltinStatus::Implemented},
    {"RotateToNextWP", 0, bRotateToNextWP, nullptr, BuiltinStatus::Implemented},
    {"lRotateToNextWP", 0, bRotateToNextWP, nullptr, BuiltinStatus::Implemented},
    {"GetWaypointDelay", 1, bGetWaypointDelay, nullptr, BuiltinStatus::Implemented},
};

} // namespace

Family movementBuiltins() { return {kTable, sizeof kTable / sizeof kTable[0]}; }

} // namespace builtins
} // namespace as3d
