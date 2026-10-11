#include "ctr_input.h"

#include "as3d/core.h"

namespace ctrapp {

using namespace as3d;

namespace {
constexpr int kCStickDead = 40;
} // namespace

CtrInput::CtrInput(const RunConfig& run, bool new3ds)
    : bindings_(run.bindings), new3ds_(new3ds), logInput_(run.logInput) {
    if (!run.padScript.empty()) {
        if (parsePadScript(run.padScript, script_))
            AS3D_INFO("pad_script: %d presses, last at frame %u", static_cast<int>(script_.size()),
                      script_.empty() ? 0u : script_.back().frame);
        else
            AS3D_WARN("pad_script: syntax error after %d presses (used as far as it goes)", static_cast<int>(script_.size()));
    }
    setUpTouch(touch_);
    if (new3ds_) irrst_ = R_SUCCEEDED(irrstInit());
}

void CtrInput::setUpTouch(TouchMapper& t) {
    t.setScreen(kTouchW, kTouchH, TouchLayoutOptions::forBottomScreen320x240());
    t.settings().autoFire = false; // A fires; the bottom screen is HUD and buttons only
    t.setPlayerScreen(false, 0, 0);
}

CtrInput::~CtrInput() {
    if (irrst_) irrstExit();
}

const PadState& CtrInput::readPad() {
    before_ = now_;
    hidScanInput();
    uint32_t held = hidKeysHeld();
    int16_t cx = 0, cy = 0;
    circlePosition cp;
    hidCircleRead(&cp);
    cx = cp.dx;
    cy = cp.dy;
    if (irrst_) {
        irrstScanInput();
        held |= irrstKeysHeld() & (pad::ZL | pad::ZR);
        circlePosition cs;
        irrstCstickRead(&cs);
        if (cs.dx > kCStickDead) held |= pad::CStickRight;
        if (cs.dx < -kCStickDead) held |= pad::CStickLeft;
        if (cs.dy > kCStickDead) held |= pad::CStickUp;
        if (cs.dy < -kCStickDead) held |= pad::CStickDown;
    }
    PadState p;
    p.held = held;
    p.cx = cx;
    p.cy = cy;
    p.touch = (held & pad::Touch) != 0;
    if (p.touch) {
        touchPosition tp;
        hidTouchRead(&tp);
        p.touchX = tp.px;
        p.touchY = tp.py;
    } else { // released: the last point, for the Up event
        p.touchX = before_.touchX;
        p.touchY = before_.touchY;
    }
    // run.ini pad_script: the scripted presses of this frame on top of the real pad.
    if (!script_.empty()) {
        applyPadScript(script_, frames_, kScriptHoldFrames, p);
        held = p.held;
    }
    // run.ini ui_taps: a scripted finger (queueTouch). A finger still down from another tap at
    // another point is lifted for one frame first.
    if (tapFrames_ > 0 && !(tapFrames_ == kScriptHoldFrames && before_.touch &&
                            (before_.touchX != tapX_ || before_.touchY != tapY_))) {
        p.touch = true;
        p.touchX = tapX_;
        p.touchY = tapY_;
        held |= pad::Touch;
        p.held = held;
        --tapFrames_;
    }
    ++frames_;
    p.down = held & ~before_.held; // own edges: the merged held (hid + irrst + script) is the truth
    p.up = before_.held & ~held;
    now_ = p;
    firstStepOfFrame_ = true;

    // The touch screen as one finger (id 0). A finger that belongs to a menu (uiInputs) reaches
    // the touch buttons neither while it is down nor at its release.
    const bool menuFinger = menuTouch_;
    if (!p.touch) menuTouch_ = false;
    const float nx = static_cast<float>(p.touchX) / kTouchW, ny = static_cast<float>(p.touchY) / kTouchH;
    int button = -1;
    if (menuFinger) {
        // the menu's finger: nothing for the touch buttons
    } else if (p.touch && !before_.touch) {
        touch_.touchEvent(0, TouchPhase::Down, nx, ny);
        button = touch_.layout().hitTest(nx, ny);
    } else if (p.touch) {
        if (p.touchX != before_.touchX || p.touchY != before_.touchY) touch_.touchEvent(0, TouchPhase::Move, nx, ny);
        button = touch_.layout().hitTest(nx, ny);
    } else if (before_.touch) {
        touch_.touchEvent(0, TouchPhase::Up, nx, ny);
    }

    if (logInput_) {
        const uint32_t shown = held & ~pad::Touch;
        if (shown != lastLogged_ || (button >= 0 && button != lastButton_)) {
            AS3D_INFO("AS3D_INPUT held=0x%08x touch=%s", static_cast<unsigned>(shown),
                      button >= 0 ? touchButtonName(static_cast<TouchButton>(button)) : "none");
            lastLogged_ = shown;
        }
    }
    lastButton_ = p.touch ? button : -1;
    return now_;
}

FrameInput CtrInput::frame(bool paused) {
    PadState p = now_;
    if (!firstStepOfFrame_) { // a catch-up step: edges were consumed by the first one
        p.down = 0;
        p.up = 0;
    }
    touch_.setPaused(paused);
    FrameInput t = touch_.takeFrame(); // empty after the first call of the frame
    firstStepOfFrame_ = false;
    return mergeFrameInput(padToFrameInput(p, bindings_), t);
}

ui::UiInput CtrInput::uiInputs(const ui::Mapping& bottom, ui::ConsoleMenuView* view, const ui::Frontend* frontend) {
    // The tap-through fix: a finger on the screen while a menu is up is the menu's until it is
    // lifted. The touch buttons forget it now (readPad gave them its down edge this frame: with
    // the in-game menu up that latched a pause, which the first step after Resume took), and
    // readPad keeps it from them until the release.
    if (now_.touch && !menuTouch_) {
        menuTouch_ = true;
        touch_.releaseAll();
        (void)touch_.takeFrame(); // drops the latched pause, confirm and buttons
    }
    const bool viewed = view && frontend && ui::ConsoleMenuView::handles(*frontend);
    ui::UiInput in = padToUiInputs(now_, before_, bottom, viewed ? 0 : kTouchW, viewed ? 0 : kTouchH);
    if (now_.down & pad::L) in.key(ui::keys::PageUp);
    if (now_.down & pad::R) in.key(ui::keys::PageDown);
    if (viewed) view->touchInput(*frontend, now_.touch, before_.touch, now_.touchX, now_.touchY, in);
    // A held D-pad direction repeats (sliders, lists): after 24 frames, every 6.
    static const struct { uint32_t bit; int key; } kDirs[4] = {
        {pad::DUp, ui::keys::Up}, {pad::DDown, ui::keys::Down}, {pad::DLeft, ui::keys::Left}, {pad::DRight, ui::keys::Right}};
    for (int i = 0; i < 4; ++i) {
        if (!(now_.held & kDirs[i].bit)) {
            repeat_[i] = 0;
            continue;
        }
        ++repeat_[i];
        if (repeat_[i] > kRepeatDelay && (repeat_[i] - kRepeatDelay) % kRepeatEvery == 0) in.key(kDirs[i].key);
    }
    if (viewed) view->filterKeys(*frontend, in);
    return in;
}

ui::UiInput CtrInput::playingUiInputs() const {
    // During play only the pause button reaches the front end (it opens the in-game menu); A
    // and B are fire and missile, the D-pad steers.
    ui::UiInput in;
    if (bindings_.pause && (now_.down & bindings_.pause)) in.key(ui::keys::Escape);
    return in;
}

} // namespace ctrapp
