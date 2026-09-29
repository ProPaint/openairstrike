// Level runtime: level start, the fixed-timestep frame, scroll and camera, the map
// spawner and the player frame (docs/spec/engine-behaviour.md 1.2, 2, 3.4, 7, 9, 10.2;
// docs/spec/hmap.md "Objects: spawning and activation").
#include <algorithm>
#include <cmath>

#include "as3d/defs.h"
#include "as3d/game_camera.h"
#include "as3d/player_select.h"
#include "as3d/world.h"
#include "world_internal.h"
#include "world_path.h"

namespace as3d {

namespace {
Vec4 planeFromRows(const Mat4& m, int row, float sign) {
    Vec4 r3{m.at(0, 3), m.at(1, 3), m.at(2, 3), m.at(3, 3)};
    Vec4 r{m.at(0, row), m.at(1, row), m.at(2, row), m.at(3, row)};
    Vec4 p = r3 + r * sign;
    float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    if (len > 0.0f) p = p * (1.0f / len);
    return p;
}
} // namespace

// ---------------------------------------------------------------------------------------
// Level start.
// ---------------------------------------------------------------------------------------

void World::resetCamera() {
    camera_ = CameraState();
    camera_.mode = config_.cameraMode;
    mapPos_ = 32.0f;
    camera_.field[0] = 640.0f;
    camera_.field[9] = 1.0f;
    camera_.field[7] = 42.0f;
    const CameraPreset& p = kGameCameraPresets[camera_.mode];
    camera_.field[1] = mapPos_ + p.yOffset;
    camera_.field[2] = p.height;
    camera_.field[3] = p.pitchDegrees;
    camera_.fov = p.fovDegrees;
    camera_.field[10] = 0.0f;
    camera_.field[11] = 0.0f;
    camera_.field[12] = kCollisionViewportW;
    camera_.field[13] = kCollisionViewportH;
    camera_.field[14] = p.fovDegrees;
    for (int k = 0; k < 3; ++k) {
        camera_.field[15 + k] = camera_.field[k];
        camera_.field[18 + k] = camera_.field[3 + k];
    }
    camera_.field[21] = mapPos_ - 64.0f;
    camera_.field[22] = mapPos_ + 1000.0f;
    if (intermission_) {
        for (int k = 0; k < 3; ++k) camera_.field[k] = intermissionCam_[k];
        for (int k = 0; k < 3; ++k) camera_.field[3 + k] = intermissionCam_[3 + k];
        camera_.field[14] = 60.0f;
    }
}

bool World::loadLevel(const std::string& ref, std::string* error) {
    if (!vfs_ || !db_) {
        if (error) *error = "world not initialised";
        return false;
    }
    std::unique_ptr<LoadedLevel> lvl(new LoadedLevel());
    if (!loadLevelByRef(*vfs_, ref, *lvl, error)) return false;

    // G_BeginLevel (10.2): difficulty, flags, per-player state.
    resetLevelState();
    resetPlayersForLevel();

    // G_StartLevel.
    level_ = std::move(lvl);
    const TerrainStyle& st = level_->style;
    terrainValid_ = terrain_.build(level_->data, st);
    intermission_ = st.hasIntermission;
    for (int k = 0; k < 6; ++k) intermissionCam_[k] = st.intermission[k];
    hmin_ = st.hmin;
    hasWater_ = st.hasWater;
    waterLevel_ = st.waterLevel;
    night_ = st.night;

    const std::vector<Placement>& pls = level_->data.placements;
    gamePaths_.clear();
    gamePaths_.resize(pls.size());
    starTotal_ = 0;
    for (size_t i = 0; i < pls.size(); ++i) {
        if (pls[i].waypoints.size() >= 2) {
            std::unique_ptr<GamePath> gp(new GamePath());
            if (gp->build(pls[i])) gamePaths_[i] = std::move(gp);
        }
        const std::string* item = level_->data.itemName(pls[i]);
        if (item && *item == "item_star") ++starTotal_;
    }

    resetPools();
    resetCamera();
    if (!intermission_) {
        for (int p = 0; p < config_.players; ++p) spawnPlayer(p);
    }
    cursor_.build(pls);
    cursor_.reset();
    lNight = night_ ? 1.0f : 0.0f;
    lWater = hasWater_ ? 1.0f : 0.0f;
    lWaterLevel = waterLevel_;
    levelClock_ = 0.0f;
    // G_BeginLevel step 5.
    enemiesInLevel_ = 0;
    maxLevelScore_ = 0.0f;
    for (PlayerRecord& pr : players_) pr.kills = 0;
    levelLoaded_ = true;
    renderPass();
    return true;
}

void World::startEmptyLevel(bool spawnPlayers) {
    resetLevelState();
    resetPlayersForLevel();
    level_.reset();
    terrainValid_ = false;
    gamePaths_.clear();
    static const std::vector<Placement> kNone;
    cursor_.build(kNone);
    intermission_ = false;
    hmin_ = -1000.0f;
    hasWater_ = false;
    waterLevel_ = 0.0f;
    night_ = false;
    resetPools();
    resetCamera();
    if (spawnPlayers) {
        for (int p = 0; p < config_.players; ++p) spawnPlayer(p);
    }
    lNight = lWater = lWaterLevel = 0.0f;
    levelLoaded_ = true;
    renderPass();
}

// ---------------------------------------------------------------------------------------
// The frame.
// ---------------------------------------------------------------------------------------

void World::step(const PlayerInput& input) {
    if (input.confirm && hintShowing_) dismissHint(); // the hint box's OK button
    lights_.clear(); // R_BeginFrame clears the dynamic light list
    bolts_.clear();  // and the render lists
    frametime_ = config_.dt;
    frametimeGlobal = frametime_;
    time_ += frametime_;
    levelClock_ += frametime_;
    applyInput(input); // the message pump runs before G_Frame
    if (paused_) {
        time_ -= frametime_;
        levelClock_ -= frametime_;
    }
    timeGlobal = time_;
    updateCamera();
    activateMapObjects();
    if (!intermission_ && !gameOver_) playerFrame();
    runEntities();
    updateParticles();
    renderPass();
    ++frame_;
}

void World::applyInput(const PlayerInput& input) {
    for (int p = 0; p < config_.players; ++p) {
        PlayerRecord& pr = players_[p];
        u32 now = input.action[p];
        u32 pressed = now & ~pr.heldInput;
        u32 released = pr.heldInput & ~now;
        pr.heldInput = now;
        if (pr.actionsDisabled) continue;
        // Held level bits (fire, missile, power-up, directions) are re-applied every frame,
        // like the keyboard autorepeat of the original's WM_KEYDOWN stream, so a script
        // that clears p_action (the player's fly-in) does not lose a held key for good.
        // The one-shot switch bits (0x100, 0x200, 0x400) are applied on the press only.
        // Engine decision, docs/spec/issues/033.
        constexpr u32 kLevelBits = 0x0FFu;
        u32 a = static_cast<u32>(ftol(pr.action));
        a = (a | pressed | (now & kLevelBits)) & ~released;
        pr.action = static_cast<float>(a);
    }
}

void World::updateCamera() {
    if (paused_) return;
    CameraState& c = camera_;
    if (intermission_) {
        // 9.6: fixed camera, pitch and roll sway, FOV 60, no scroll.
        for (int k = 0; k < 3; ++k) c.field[k] = intermissionCam_[k];
        c.field[3] = intermissionCam_[3] + 0.5f * std::sin(0.5f * time_);
        c.field[4] = intermissionCam_[4];
        c.field[5] = intermissionCam_[5] + 1.4f * std::sin(0.75f * time_);
        c.field[14] = 60.0f;
    } else {
        const CameraPreset& preset = kGameCameraPresets[c.mode];
        c.field[7] = c.field[9] * 42.0f;
        c.field[8] = 0.0f;
        mapPos_ += c.field[7] * frametime_;

        // Players are kept inside the side planes of the previous frame (10 units).
        float sumX = 0.0f;
        int living = 0;
        for (int p = 0; p < config_.players; ++p) {
            int pi = playerEntityIndex(p);
            if (pi < 0) continue;
            Entity& pe = ents_[static_cast<size_t>(pi)];
            if (pe.f(F_DEAD) != 0.0f) continue;
            // V_ClampToFrustumX (docs/spec/issues/120, finding 3 and rule 8): x_k solves plane
            // k at the origin's y and z; x is kept in [x_left + 10, x_right - 10], a margin in
            // world x units, of the collision camera's frustum (planes_ 0 left, 1 right).
            Vec3 o = pe.v3(F_ORIGIN);
            for (int k = 0; k < 2; ++k) {
                const Vec4& pl = planes_[k];
                if (!(std::fabs(pl.x) > 1e-6f)) continue;
                float xk = (-pl.w - pl.y * o.y - pl.z * o.z) / pl.x;
                if (pl.x > 0.0f) o.x = std::max(o.x, xk + 10.0f); // left plane: lower bound
                else o.x = std::min(o.x, xk - 10.0f);              // right plane: upper bound
            }
            pe.setF(F_ORIGIN, o.x);
            sumX += o.x;
            ++living;
        }
        c.field[0] += frametime_ * c.field[6];
        if (living > 0) {
            float target = sumX / static_cast<float>(living);
            float d = target - c.field[0];
            if (d > 48.0f) c.field[0] = target - 48.0f;
            else if (d < -48.0f) c.field[0] = target + 48.0f;
        }
        c.field[0] = std::min(std::max(c.field[0], kGameCameraMinX), kGameCameraMaxX);
        c.field[1] = mapPos_ + preset.yOffset;
        c.field[2] = preset.height;
        c.field[3] = preset.pitchDegrees;
        float fov = preset.fovDegrees;
        if (c.quakeClock > 0.0f) {
            float t = c.quakeElapsed;
            float e = std::exp(-2.0f * t);
            float a = c.quakeAmplitude;
            c.quakeYaw = 2.0f * a * e * std::sin(24.0f * t);
            c.quakeRoll = a * e * std::sin(12.0f * t);
            fov += a * e * std::sin(18.0f * t);
            c.quakeElapsed += frametime_;
            c.quakeClock -= frametime_;
        } else {
            c.quakeYaw = c.quakeRoll = 0.0f;
        }
        c.field[4] = c.quakeYaw;
        c.field[5] = c.quakeRoll;
        c.field[14] = fov;
    }
    for (int k = 0; k < 3; ++k) {
        c.field[15 + k] = c.field[k];
        c.field[18 + k] = c.field[3 + k];
    }
    c.field[21] = mapPos_ - 64.0f;
    c.field[22] = mapPos_ + 1000.0f;
}

void World::renderPass() {
    const CameraState& c = camera_;
    float farPlane = 2000.0f;
    if (level_ && level_->style.hasFog) farPlane = std::max(level_->style.fogFar, 1000.0f);
    float fov = c.field[14] > 1.0f ? c.field[14] : 60.0f;
    Mat4 view = rotationX(c.field[3]) * rotationY(c.field[4]) * rotationZ(c.field[5]) *
                translation(Vec3{-c.field[0], -c.field[1], -c.field[2]});
    Mat4 proj = perspective(fov, kCollisionViewportW / kCollisionViewportH, kGameCameraNear, farPlane);
    prevViewProj_ = proj * view;
    planes_[0] = planeFromRows(prevViewProj_, 0, 1.0f);  // left
    planes_[1] = planeFromRows(prevViewProj_, 0, -1.0f); // right
    planes_[2] = planeFromRows(prevViewProj_, 1, 1.0f);  // bottom
    planes_[3] = planeFromRows(prevViewProj_, 1, -1.0f); // top
    planes_[4] = planeFromRows(prevViewProj_, 2, 1.0f);  // near
    planes_[5] = planeFromRows(prevViewProj_, 2, -1.0f); // far
}

// ---------------------------------------------------------------------------------------
// Map spawner (3.4).
// ---------------------------------------------------------------------------------------

void World::activateMapObjects() {
    if (!level_) return;
    SpawnCursor::Result r = cursor_.update(mapPos_);
    for (const Placement* p : r.toSpawn) spawnPlacement(*p);
}

void World::spawnPlacement(const Placement& pl) {
    const ObjectDef* def = db_->findObject(level_->data.typeName(pl));
    if (!def) return;
    if (listCount_ >= kMaxListEntities) {
        ++stats_.spawnRefused;
        return;
    }
    int idx = buildEntity(def, 0);
    if (idx < 0) return;
    ++stats_.entitiesCreated;
    linkNewest(idx);
    Entity& e = ents_[static_cast<size_t>(idx)];
    setStateRecursive(idx, ES_ACTIVE);
    // 1. Position and snapping.
    e.setV3(F_ORIGIN, pl.spawnPosition());
    snapToGround(e);
    // 2. Script override.
    if (!pl.scriptOverride.empty()) attachScript(idx, pl.scriptOverride);
    // 3. Player index.
    int pidx = 0;
    if (config_.players == 2) {
        bool alive0 = players_[0].lives >= 0.0f, alive1 = players_[1].lives >= 0.0f;
        if (alive0 && alive1) pidx = (rng_.next() & 1u) ? 1 : 0;
        else pidx = alive1 && !alive0 ? 1 : 0;
    }
    setPlayerIndexRecursive(idx, pidx);
    // 5. Yaw, path.
    e.setF(F_ANGLES + 2, pl.yawDegrees());
    e.shadowKey = pl.rotationSteps;
    e.hasShadowKey = true;
    size_t pi = static_cast<size_t>(&pl - level_->data.placements.data());
    if (const GamePath* path = pathOfPlacement(pi)) {
        e.path = path;
        e.pathDistance = 0.0f;
        e.pathLastNode = 0;
        e.pathFinished = false;
        e.setF(F_ANGLES + 2, path->headingAtStart());
        e.setF(F_WP_WAIT, path->delay(0));
    }
    // 6. init.
    runInit(idx);
    // 7. Drop.
    if (const std::string* item = level_->data.itemName(pl)) e.drop = db_->findObject(*item);
    // 8. State.
    setStateRecursive(idx, (e.flagBits() & FL_TEMPORARY) ? ES_ACTIVE : ES_DORMANT);
    // 9. Health.
    e.setF(F_HEALTH, e.f(F_HEALTH) * healthFactor_);
    e.maxHealth *= healthFactor_;
    // 10. Score.
    float score = static_cast<float>(10 * ftol(e.f(F_SCORE) * scoreFactor_ / 10.0f));
    e.setF(F_SCORE, score);
    // 11, 12. Level totals.
    if (e.f(F_CLASS) == kClassEnemy && !(e.flagBits() & FL_NONTARGET)) ++enemiesInLevel_;
    maxLevelScore_ += score;
}

// ---------------------------------------------------------------------------------------
// Player frame (7.2, 1.3 game over).
// ---------------------------------------------------------------------------------------

void World::playerFrame() {
    for (int p = 0; p < config_.players; ++p) {
        PlayerRecord& pr = players_[p];
        u32 a = static_cast<u32>(ftol(pr.action));
        bool found = false;
        if (a & ACT_NEXT_POWERUP) {
            const int t = nextOwnedIndex(pr.powerups, kPowerupKindsOwned, pr.currentPowerup, &found);
            if (found) pr.currentPowerup = t;
            a &= ~ACT_NEXT_POWERUP;
        }
        if (a & ACT_NEXT_MISSILE) {
            const int t = nextOwnedIndex(pr.missiles, kMissileKindsOwned, pr.currentMissile, &found);
            if (found) pr.currentMissile = t;
            a &= ~ACT_NEXT_MISSILE;
        }
        if (a & ACT_NEXT_WEAPON) {
            const int t = nextOwnedIndex(pr.upgrades, kWeaponKindsOwned, ftol(pr.weapon), &found);
            if (found) pr.weapon = static_cast<float>(t);
            a &= ~ACT_NEXT_WEAPON;
        }
        pr.action = static_cast<float>(a);
    }
    if (!gameOver_) {
        bool over = players_[0].lives < 0.0f;
        if (config_.players == 2) over = over && players_[1].lives < 0.0f;
        if (over) setGameOver();
    }
}

} // namespace as3d
