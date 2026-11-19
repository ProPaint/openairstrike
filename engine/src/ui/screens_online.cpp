// Online co-op in the front end (docs/spec/issues/180-online-coop.md 8): the Frontend members of
// the online session (setOnlineHost, online, onlineApplyFlow, onlineStartCampaign, onlineEnded)
// and the screens Online, OnlineHostCode, OnlineJoinCode, OnlineLobby and OnlineNotice in the
// three front-end styles (the first game's Start Game look of screens_players.cpp, the sequels'
// widgets, Gulf Thunder's plain list; sequel() / plain() choose).
//
// T0 stub: every online screen shows "ONLINE" and Back; the session members only record the
// host. T5 builds the real screens and flow (texts of section 9 in frontend_texts.cpp).
#include "as3d/frontend.h"

namespace as3d::ui {

void Frontend::setOnlineHost(OnlineHost* h) {
    online_ = h;
    content_.onlineMode = h != nullptr;
}

bool Frontend::online() const {
    if (!online_) return false;
    const OnlinePhase p = online_->status().phase;
    return p >= OnlinePhase::Lobby && p <= OnlinePhase::Playing;
}

void Frontend::onlineApplyFlow(OnlineFlowChoice) {}         // T5
void Frontend::onlineStartCampaign(const OnlineLobby&) {}  // T5
void Frontend::onlineEnded(std::string) {}                 // T5

Menu Frontend::buildOnlineScreen(Screen) {
    enum { kBack = 1 };
    Menu m;
    m.addTextButton(kBack, {50, 470, 160, 40}, texts_.get("plain.back"));
    m.onItem = [this](Menu&, MenuItem& it, int ev) {
        if (ev == kActivate && it.id == kBack) menus_.pop();
    };
    m.drawBack = [](MenuDrawContext& c) {
        widgets::letterbox(c);
        widgets::text(c, 400, 80, "ONLINE", orange(), Align::Center);
    };
    return m;
}

} // namespace as3d::ui
