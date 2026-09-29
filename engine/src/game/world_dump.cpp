// Deterministic JSON dump of the simulation state (used by the determinism test and by
// as3d_sim --dump-state). Floats are written as the 8 hex digits of their bits so that
// equal states give byte-identical dumps; a few readable decimals are added for humans.
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>

#include "as3d/world.h"
#include "as3d/world_particles.h"
#include "world_internal.h"

namespace as3d {

namespace {
void appendf(std::string& s, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void appendf(std::string& s, const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0) s.append(buf, static_cast<size_t>(std::min(n, static_cast<int>(sizeof buf) - 1)));
}
void appendJsonString(std::string& s, const std::string& v) {
    s.push_back('"');
    for (char c : v) {
        if (c == '"' || c == '\\') {
            s.push_back('\\');
            s.push_back(c);
        } else if (static_cast<unsigned char>(c) < 0x20) {
            appendf(s, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
        } else {
            s.push_back(c);
        }
    }
    s.push_back('"');
}
} // namespace

std::string World::dumpStateJson() const {
    std::string s;
    s.reserve(1 << 20);
    appendf(s, "{\n  \"frame\": %u,\n  \"time\": \"%08x\",\n  \"map_pos\": \"%08x\",\n  \"map_pos_f\": %.3f,\n",
            frame_, fbits(time_), fbits(mapPos_), static_cast<double>(mapPos_));
    appendf(s, "  \"paused\": %d, \"game_over\": %d, \"level_complete\": %d, \"intermission\": %d,\n",
            paused_ ? 1 : 0, gameOver_ ? 1 : 0, levelComplete_ ? 1 : 0, intermission_ ? 1 : 0);
    appendf(s, "  \"list_entities\": %d, \"slots_in_use\": %d, \"enemies_in_level\": %d, \"max_level_score\": \"%08x\",\n",
            listCount_, slotsInUse_, enemiesInLevel_, fbits(maxLevelScore_));
    appendf(s,
            "  \"stats\": {\"script_errors\": %llu, \"stalls\": %llu, \"spawn_refused\": %llu, \"created\": %llu, "
            "\"freed\": %llu, \"max_list\": %d, \"max_slots\": %d},\n",
            static_cast<unsigned long long>(stats_.scriptErrors), static_cast<unsigned long long>(stats_.stalls),
            static_cast<unsigned long long>(stats_.spawnRefused), static_cast<unsigned long long>(stats_.entitiesCreated),
            static_cast<unsigned long long>(stats_.entitiesFreed), stats_.maxListEntities, stats_.maxSlotsInUse);
    appendf(s,
            "  \"particles\": {\"emitters\": %d, \"live\": %d, \"refused\": %llu, \"hash\": \"%08x\", "
            "\"damage_players\": \"%08x\", \"damage_enemies\": \"%08x\"},\n",
            particles_->emitterCount(), particles_->liveParticles(), static_cast<unsigned long long>(particles_->refused()),
            particles_->stateHash(), fbits(particles_->damageToPlayers()), fbits(particles_->damageToEnemies()));
    appendf(s,
            "  \"globals\": {\"self\": \"%08x\", \"other\": \"%08x\", \"cb\": [\"%08x\", \"%08x\", \"%08x\"], "
            "\"retreg\": \"%08x\"},\n",
            selfBits, otherBits, cbMsgBits, cbParm1Bits, cbParm2Bits, retreg_);
    s += "  \"camera\": [";
    for (int k = 0; k < kCameraFieldCount; ++k) appendf(s, "%s\"%08x\"", k ? ", " : "", fbits(camera_.field[k]));
    s += "],\n  \"players\": [\n";
    for (int p = 0; p < config_.players; ++p) {
        const PlayerRecord& pr = players_[p];
        appendf(s,
                "    {\"entity\": \"%08x\", \"action\": \"%08x\", \"scores\": \"%08x\", \"lives\": \"%08x\", "
                "\"stars\": \"%08x\", \"speedfactor\": \"%08x\", \"counters\": [\"%08x\", \"%08x\", \"%08x\"], "
                "\"weapon\": \"%08x\", \"freeze\": %d, \"disabled\": %d, \"kills\": %d, \"missile\": %d, "
                "\"powerup\": %d, \"upgrades\": [",
                pr.entityRef, fbits(pr.action), fbits(pr.scores), fbits(pr.lives), fbits(pr.stars),
                fbits(pr.speedFactor), fbits(pr.counter[0]), fbits(pr.counter[1]), fbits(pr.counter[2]),
                fbits(pr.weapon), pr.freezeCount, pr.actionsDisabled ? 1 : 0, pr.kills, pr.currentMissile,
                pr.currentPowerup);
        for (int k = 0; k < 20; ++k) appendf(s, "%s%d", k ? "," : "", pr.upgrades[k]);
        s += "], \"missiles\": [";
        for (int k = 0; k < 5; ++k) appendf(s, "%s%d", k ? "," : "", pr.missiles[k]);
        s += "], \"powerups\": [";
        for (int k = 0; k < 16; ++k) appendf(s, "%s%d", k ? "," : "", pr.powerups[k]);
        appendf(s, "]}%s\n", p + 1 < config_.players ? "," : "");
    }
    s += "  ],\n  \"list\": [";
    bool first = true;
    for (int i = newest_; i != -1; i = ents_[static_cast<size_t>(i)].older) {
        appendf(s, "%s%d", first ? "" : ",", i);
        first = false;
    }
    s += "],\n  \"entities\": [\n";
    first = true;
    for (int i = 0; i < kMaxEntitySlots; ++i) {
        const Entity& e = ents_[static_cast<size_t>(i)];
        if (!e.inUse) continue;
        if (!first) s += ",\n";
        first = false;
        appendf(s, "    {\"slot\": %d, \"gen\": %u, \"name\": ", i, e.generation);
        appendJsonString(s, e.name);
        appendf(s, ", \"parent\": %d, \"state\": %d, \"rt\": %u, \"player\": %d, \"path_d\": \"%08x\", ",
                e.parent, e.state, e.rt, e.playerIndex, fbits(e.pathDistance));
        appendf(s, "\"pos\": [%.2f, %.2f, %.2f], \"script\": ", static_cast<double>(e.f(F_ORIGIN)),
                static_cast<double>(e.f(F_ORIGIN + 1)), static_cast<double>(e.f(F_ORIGIN + 2)));
        appendJsonString(s, e.scriptPath);
        appendf(s, ", \"pc\": %u, \"faulted\": %d, \"fields\": \"", e.thread ? e.thread->pc() : 0u,
                e.scriptFaulted ? 1 : 0);
        for (int k = 0; k < kEntityFieldCount; ++k) appendf(s, "%08x", e.fields[k]);
        s += "\"}";
    }
    s += "\n  ]\n}\n";
    return s;
}

} // namespace as3d
