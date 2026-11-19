// Which game controller flies for which player (as3d/player_pads.h).
#include "as3d/player_pads.h"

#include <algorithm>

namespace as3d {

int PlayerPads::find(int id) const {
    for (size_t i = 0; i < pads_.size(); ++i)
        if (pads_[i].id == id) return static_cast<int>(i);
    return -1;
}

int PlayerPads::padsOf(int player) const {
    int n = 0;
    for (const Pad& p : pads_) n += p.player == player ? 1 : 0;
    return n;
}

int PlayerPads::automaticSide() const {
    const int p1 = padsOf(0) + (touch_ ? 1 : 0), p2 = padsOf(1);
    return p2 < p1 ? 1 : 0;
}

int PlayerPads::connect(int id, const std::string& name, const std::string& key) {
    const int known = find(id);
    if (known >= 0) return known;
    Pad p;
    p.id = id;
    p.name = name;
    // The rank among the pads of the same model already here: two identical pads have the same
    // name and GUID, and each keeps its own side.
    int rank = 0;
    for (const Pad& o : pads_)
        if (o.memory.compare(0, key.size() + 1, key + "#") == 0) ++rank;
    p.memory = key + "#" + std::to_string(rank);
    // The smallest free number, so that "Gamepad 1" stays 1 while another comes and goes.
    for (p.number = 1;; ++p.number)
        if (std::none_of(pads_.begin(), pads_.end(), [&](const Pad& o) { return o.number == p.number; })) break;
    const auto it = remembered_.find(p.memory);
    p.player = it != remembered_.end() ? it->second : automaticSide();
    remembered_[p.memory] = p.player;
    pads_.push_back(p);
    return static_cast<int>(pads_.size()) - 1;
}

void PlayerPads::disconnect(int id) {
    const int i = find(id);
    if (i >= 0) pads_.erase(pads_.begin() + i);
}

void PlayerPads::setHeld(int id, uint32_t held) {
    const int i = find(id);
    if (i >= 0) pads_[static_cast<size_t>(i)].held = held;
}

void PlayerPads::pushMove(int id, int dir) {
    const int i = find(id);
    if (i >= 0 && dir != 0) pads_[static_cast<size_t>(i)].move = dir < 0 ? -1 : 1;
}

int PlayerPads::takeMove(int index) {
    if (index < 0 || index >= count()) return 0;
    const int m = pads_[static_cast<size_t>(index)].move;
    pads_[static_cast<size_t>(index)].move = 0;
    return m;
}

void PlayerPads::clearMoves() {
    for (Pad& p : pads_) p.move = 0;
}

void PlayerPads::setPlayer(int index, int player) {
    if (index < 0 || index >= count() || (player != 0 && player != 1)) return;
    Pad& p = pads_[static_cast<size_t>(index)];
    p.player = player;
    remembered_[p.memory] = player;
}

uint32_t PlayerPads::heldOf(int player) const {
    uint32_t held = 0;
    for (const Pad& p : pads_)
        if (p.player == player) held |= p.held;
    return held;
}

bool PlayerPads::hasDevice(int player) const {
    return keyboard_ || (player == 0 && touch_) || padsOf(player) > 0;
}

bool PlayerPads::twoPlayersPossible() const {
    return keyboard_ || count() >= (touch_ ? 1 : 2);
}

} // namespace as3d
