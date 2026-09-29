// Movement, orientation, terrain, waiting and waypoint builtins
// (docs/spec/rcsl-builtins-table.md "Movement of self", "Latent / waypoint builtins";
// engine-behaviour.md 4.5; hmap.md "Waypoint paths").
#include <cmath>

#include "builtins_common.h"

namespace as3d {
namespace builtins {

namespace {

float ft(World& w) { return w.frametimeGlobal; }

// Path heading (terrain.h convention: 0 = +y, 90 = +x) to entity yaw (models face +y).
float headingToYaw(float heading) { return -heading; }

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

// Turns `angle` toward `target` by at most `step` degrees; true once arrived.
bool turnToward(float& angle, float delta, float step) {
    if (std::fabs(delta) <= step) {
        angle += delta;
        return true;
    }
    angle += delta > 0.0f ? step : -step;
    return false;
}

// RotateTo(target, axes): target.origin - self.origin in self's local frame (axis rows),
// converted to angles with the engine's vec_toangles convention, then turned by at most
// field 24 * frametime degrees per call. Bit 1 turns field 16 (yaw), bit 2 field 14.
// Done when every requested axis arrived within this step.
bool rotateToStep(World& w, BuiltinArgs& a, int& axesOut) {
    int s = selfOf(w);
    int t = w.indexFromRef(a.bits(0));
    int axes = intArg(a, 1);
    axesOut = axes;
    if (s < 0 || t < 0) return false;
    Entity& e = w.entity(s);
    const Entity& te = w.entity(t);
    Vec3 d = te.v3(F_ORIGIN) - e.v3(F_ORIGIN);
    Vec3 fw = e.v3(F_AXIS), lf = e.v3(F_AXIS + 3), up = e.v3(F_AXIS + 6);
    float ang[3];
    vecToAngles(dot(d, fw), dot(d, lf), dot(d, up), ang);
    float step = e.f(F_WP_TURN_RATE) * ft(w);
    int arrived = 0;
    if (axes & 1) {
        float yaw = e.f(F_ANGLES + 2);
        if (turnToward(yaw, wrap180(ang[2]), step)) arrived |= 1;
        e.setF(F_ANGLES + 2, yaw);
    }
    if (axes & 2) {
        float pitch = e.f(F_ANGLES);
        if (turnToward(pitch, wrap180(ang[0]), step)) arrived |= 2;
        e.setF(F_ANGLES, pitch);
    }
    return arrived == axes;
}

void bRotateTo(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int axes;
    bool done = rotateToStep(w, a, axes);
    if (a.latent()) a.setDone(done);
}

void bRotateToClamp(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int axes;
    bool done = rotateToStep(w, a, axes);
    int s = selfOf(w);
    if (s >= 0) {
        Entity& e = w.entity(s);
        float mx = a.f32(2), mn = a.f32(3);
        const int fieldsFor[2] = {F_ANGLES + 2, F_ANGLES};
        for (int bit = 0; bit < 2; ++bit) {
            if (!(axes & (1 << bit))) continue;
            float v = e.f(fieldsFor[bit]);
            if (v > mx) v = mx;
            if (v < mn) v = mn;
            e.setF(fieldsFor[bit], v);
        }
    }
    if (a.latent()) a.setDone(done);
}

// MoveToNextWP(align): s += field 23 * frametime along the path; x, y follow it (z comes
// from ground snapping in the think); align sets the yaw to the path heading; field 25
// banks field 15 by the heading change to the next 8-unit sample * field 25 / 4.8.
// Done on passing a waypoint (field 37 = its delay) or at the end of an open path, which
// also deactivates the entity. Done immediately without a path.
void bMoveToNextWP(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int s = selfOf(w);
    bool done = true;
    if (s >= 0) {
        Entity& e = w.entity(s);
        if (e.path && !e.pathFinished) {
            done = false;
            WaypointPath::AdvanceResult r = e.path->advance(e.pathDistance, e.f(F_WP_SPEED) * ft(w));
            e.setF(F_ORIGIN, r.position.x);
            e.setF(F_ORIGIN + 1, r.position.y);
            if (intArg(a, 0) != 0) e.setF(F_ANGLES + 2, headingToYaw(r.headingDegrees));
            float bank = e.f(F_WP_BANK);
            if (bank != 0.0f) {
                float next = e.path->sampleAtDistance(e.pathDistance + 8.0f).headingDegrees;
                float turn = -wrap180(next - r.headingDegrees);
                e.setF(F_ANGLES + 1, turn * bank / 4.8f);
            }
            if (r.waypointPassed) {
                e.setF(F_WP_WAIT, e.path->waypointDelay(r.passedWaypointIndex));
                done = true;
            }
            if (r.finished) {
                e.pathFinished = true;
                w.setActive(s, false);
                done = true;
            }
        }
    }
    if (a.latent()) a.setDone(done);
}

void bRotateToNextWP(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int s = selfOf(w);
    bool done = true;
    if (s >= 0 && w.entity(s).path) {
        Entity& e = w.entity(s);
        float target = headingToYaw(e.path->sampleAtDistance(e.pathDistance).headingDegrees);
        float yaw = e.f(F_ANGLES + 2);
        turnToward(yaw, wrap180(target - yaw), e.f(F_WP_TURN_RATE) * ft(w));
        e.setF(F_ANGLES + 2, yaw);
        done = std::fabs(wrap180(target - yaw)) < 0.1f;
    }
    if (a.latent()) a.setDone(done);
}

void bGetWaypointDelay(BuiltinArgs& a, void*) {
    World& w = worldOf(a);
    int s = selfOf(w);
    float v = 0.0f;
    if (s >= 0 && w.entity(s).path) v = w.entity(s).path->waypointDelay(intArg(a, 0));
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
    {"RotateTo", 2, bRotateTo, nullptr, BuiltinStatus::Approximate},
    {"lRotateTo", 2, bRotateTo, nullptr, BuiltinStatus::Approximate},
    {"RotateToClamp", 4, bRotateToClamp, nullptr, BuiltinStatus::Approximate},
    {"lRotateToClamp", 4, bRotateToClamp, nullptr, BuiltinStatus::Approximate},
    {"MoveToNextWP", 1, bMoveToNextWP, nullptr, BuiltinStatus::Approximate},
    {"lMoveToNextWP", 1, bMoveToNextWP, nullptr, BuiltinStatus::Approximate},
    {"RotateToNextWP", 0, bRotateToNextWP, nullptr, BuiltinStatus::Approximate},
    {"lRotateToNextWP", 0, bRotateToNextWP, nullptr, BuiltinStatus::Approximate},
    {"GetWaypointDelay", 1, bGetWaypointDelay, nullptr, BuiltinStatus::Implemented},
};

} // namespace

Family movementBuiltins() { return {kTable, sizeof kTable / sizeof kTable[0]}; }

} // namespace builtins
} // namespace as3d
