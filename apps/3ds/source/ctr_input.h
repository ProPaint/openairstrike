// The 3DS input (M1.9): hid (and irrst on a New 3DS) read once per displayed frame into the
// neutral PadState of as3d/console_pad.h, the bottom screen as the touch buttons of the
// TouchMapper (the 320x240 preset), and the per-step FrameInput = pad merged with touch.
//
// Per frame:  readPad()  then, for each simulation step,  frame(paused)  (at most two steps run
// per frame: the pressed/released edges and the touch latches are given to the first step only).
// The front end (M2.1) calls uiInputs(bottomMapping) once per frame instead of frame().
#pragma once

#include <3ds.h>

#include "as3d/console_pad.h"
#include "as3d/input.h"
#include "run_config.h"

namespace ctrapp {

class CtrInput {
public:
    // `new3ds`: also start irrst for ZL / ZR and the C-stick.
    CtrInput(const RunConfig& run, bool new3ds);
    ~CtrInput();
    CtrInput(const CtrInput&) = delete;
    CtrInput& operator=(const CtrInput&) = delete;

    // hidScanInput (+ irrstScanInput), then the snapshot; feeds the touch screen to the touch
    // mapper and logs (log_input=1). Call once per displayed frame.
    const as3d::PadState& readPad();
    const as3d::PadState& pad() const { return now_; }

    // The step's input: padToFrameInput(bindings) merged with the touch buttons. `paused`
    // tells the touch mapper that a tap only continues the game.
    as3d::FrameInput frame(bool paused);

    // The front end's input for this frame while a menu is up (padToUiInputs: D-pad = arrows
    // with repeat while held, A = Enter, B / START = Escape) plus the touch as the pointer, with
    // `bottom` the mapping of the 320x240 bottom screen to the virtual screen. Once per frame.
    as3d::ui::UiInput uiInputs(const as3d::ui::Mapping& bottom);
    // The front end's input during play: Escape on the pause button's down edge, nothing else.
    as3d::ui::UiInput playingUiInputs() const;

    // The player's helicopter on the top screen in the virtual 800x600 screen, once a renderer
    // can project it; unused by the bottom-screen preset (no drag area), disabled by default.
    void setPlayerScreen(bool valid, float vx, float vy) { touch_.setPlayerScreen(valid, vx, vy); }

    const as3d::PadBindings& bindings() const { return bindings_; }
    const as3d::TouchMapper& touch() const { return touch_; }

    static constexpr int kTouchW = 320, kTouchH = 240;
    // The touch band's mapper as the input sets it up (the 320x240 preset, no auto-fire, no drag
    // area): the frame composer builds the band's first list with one during the loading screen.
    static void setUpTouch(as3d::TouchMapper& t);
    // A pad_script press is held this many displayed frames.
    static constexpr unsigned kScriptHoldFrames = 6;

private:
    std::vector<as3d::PadScriptPress> script_; // run.ini pad_script
    unsigned frames_ = 0;                       // readPad calls so far (the script's clock)
    static constexpr int kRepeatDelay = 24, kRepeatEvery = 6;
    int repeat_[4] = {0, 0, 0, 0};              // frames each D-pad direction is held (menus)
    as3d::PadBindings bindings_;
    as3d::TouchMapper touch_;
    as3d::PadState now_, before_;
    bool new3ds_;
    bool irrst_ = false;
    bool logInput_;
    bool firstStepOfFrame_ = true;
    uint32_t lastLogged_ = 0xFFFFFFFFu;
    int lastButton_ = -2;
};

} // namespace ctrapp
