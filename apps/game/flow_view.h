// What the front end's game (GameFlow, GameStack) needs from the renderer of a host: the level
// start, the frame behind the front end and the sequels' model view. The desktop, Android and
// the web use GameView (game_view.h, GLES); the 3DS app its frame composer
// (apps/3ds/source/ctr_flow_view.h, PICA, two screens). Nothing here touches a graphics API.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "as3d/precache.h"
#include "as3d/profile.h"
#include "as3d/ui.h"

namespace as3d::ui {
class Frontend;
struct ModelView;
} // namespace as3d::ui

namespace as3d_game {

class GameSession;

// config.ini Brightness default (render-pipeline.md 1.5).
constexpr float kDefaultBrightness = 0.6f;

// What one frame shows besides the world, behind the front end (game_flow.h).
struct FrameLayers {
    bool world = true;                     // the 3D level (false: black, e.g. the intro pages)
    bool hud = false;
    as3d::ui::HudState hudState;
    as3d::ui::Frontend* frontend = nullptr; // menus, intro pages, banner
    float brightness = kDefaultBrightness;
};

class FlowView {
public:
    virtual ~FlowView() = default;
    // Once, after the session is loaded (GameStack::build); with `level` also beginLevel (the
    // session must have a level loaded).
    virtual bool init(GameSession& session, std::string* error, bool level = true) = 0;
    // After every level load (GameFlow::levelLoaded, GameSession::kLevelStarted).
    virtual bool beginLevel(GameSession& session, std::string* error) = 0;
    // The frame behind the front end (GameFlow::draw): world, HUD, front end, brightness, in the
    // host's frame of width x height.
    virtual void drawFrame(const GameSession& session, int width, int height, const FrameLayers& layers) = 0;
    // The sequels' model view (GameHost::drawModel); a host without one ignores it.
    virtual void drawModel(GameSession&, const as3d::ui::ModelView&) {}
    // Settings::screenMode in use (GameFlow::screenMode), before every drawFrame.
    virtual void setScreenMode(int) {}

    // With GameOptions::precache: loads the sounds of the level's precache set (the audio side,
    // AudioBridge::precacheSounds); unset = no sound precache. Returns how many it looked up.
    std::function<int(const as3d::PrecacheSet&)> precacheSounds;
};

// How GameStack makes its view (StackConfig::makeView): GameView on GL hosts (makeGameView in
// game_view.h), the composer's adapter on the 3DS.
using FlowViewFactory = std::function<std::unique_ptr<FlowView>()>;

} // namespace as3d_game
