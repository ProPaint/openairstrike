#include "ctr_input.h"

#include "as3d/core.h"

namespace ctrapp {

using namespace as3d;

namespace {
constexpr int kCStickDead = 40;
} // namespace

CtrInput::CtrInput(const RunConfig& run, bool new3ds)
    : bindings_(run.bindings), new3ds_(new3ds), logInput_(run.logInput) {
    touch_.setScreen(kTouchW, kTouchH, TouchLayoutOptions::forBottomScreen320x240());
    touch_.settings().autoFire = false; // A fires; the bottom screen is HUD and buttons only
    touch_.setPlayerScreen(false, 0, 0);
    if (new3ds_) irrst_ = R_SUCCEEDED(irrstInit());
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
    p.down = held & ~before_.held; // own edges: the merged held (hid + irrst) is the truth
    p.up = before_.held & ~held;
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
    now_ = p;
    firstStepOfFrame_ = true;

    // The touch screen as one finger (id 0).
    const float nx = static_cast<float>(p.touchX) / kTouchW, ny = static_cast<float>(p.touchY) / kTouchH;
    int button = -1;
    if (p.touch && !before_.touch) {
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

ui::UiInput CtrInput::uiInputs(const ui::Mapping& bottom) const {
    return padToUiInputs(now_, before_, bottom, kTouchW, kTouchH);
}

} // namespace ctrapp
