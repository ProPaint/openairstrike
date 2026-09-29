// Options (S6) and Configure controls (S7), frontend.md 3.6 and 3.7.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "as3d/frontend.h"

namespace as3d::ui {

namespace {

enum OptionId { kBack = 1, kConfKeys = 2, kApply = 3, kResolution = 20, kRefresh, kDepth, kFullscreen, kBrightness,
                kSfx, kMusic, kSound3D, kCamera, kMouse };

bool videoDiffers(const Settings& a, const Settings& b) {
    return a.videoMode != b.videoMode || a.refreshRate != b.refreshRate || a.colorDepth != b.colorDepth ||
           a.fullscreen != b.fullscreen || a.sound3D != b.sound3D;
}

constexpr float kRowY[kActionCount] = {180, 200, 240, 260, 300, 320, 360, 380, 400, 420};
constexpr int kRowBase = 100, kControlsSet = 10, kTouchClear = 51, kTouchCancel = 50;
constexpr RectF kClearButton{250, 452, 130, 26}, kCancelButton{420, 452, 130, 26};

} // namespace

Menu Frontend::buildOptions() {
    Menu m;
    pending_ = profile_.settings; // pending values reload from the live settings at every opening
    const Settings& s = profile_.settings;
    const std::string off = texts_.get("opt.off"), on = texts_.get("opt.on");

    std::vector<std::string> modes;
    for (int i = 0; i < std::clamp(content_.videoModeCount, 1, 9); i++) modes.push_back(kVideoModeNames[i]);
    m.addSpinner(kResolution, 400, 160, texts_.get("opt.resolution"), modes, std::min(s.videoMode, static_cast<int>(modes.size()) - 1))
        .tooltip = texts_.get("opt.tooltip.resolution");
    std::vector<std::string> rates{texts_.get("opt.refresh.default")};
    int rateIndex = 0;
    for (size_t i = 0; i < content_.refreshRates.size(); i++) {
        rates.push_back(std::to_string(content_.refreshRates[i]) + texts_.get("opt.hz"));
        if (content_.refreshRates[i] == s.refreshRate) rateIndex = static_cast<int>(i) + 1;
    }
    m.addSpinner(kRefresh, 400, 180, texts_.get("opt.refresh"), rates, rateIndex);
    m.addSpinner(kDepth, 400, 200, texts_.get("opt.depth"),
                 {texts_.get("opt.depth.0"), texts_.get("opt.depth.16"), texts_.get("opt.depth.32")},
                 s.colorDepth == 16 ? 1 : s.colorDepth == 32 ? 2 : 0);
    m.addSpinner(kFullscreen, 400, 220, texts_.get("opt.fullscreen"), {off, on}, s.fullscreen ? 1 : 0);
    m.addSlider(kBrightness, 400, 240, texts_.get("opt.brightness"), 2, 10, std::round(s.brightness * 10));
    m.addSlider(kSfx, 400, 280, texts_.get("opt.sfx"), 0, 10, std::round(s.sfxVolume * 10));
    m.addSlider(kMusic, 400, 300, texts_.get("opt.music"), 0, 10, std::round(s.musicVolume * 10));
    m.addSpinner(kSound3D, 400, 320, texts_.get("opt.sound3d"), {off, on}, s.sound3D ? 1 : 0);
    std::vector<std::string> cams;
    for (int i = 0; i < 4; i++) cams.push_back(texts_.get("camera." + std::to_string(i)));
    m.addSpinner(kCamera, 400, 360, texts_.get("opt.camera"), cams, s.camera);
    m.addSpinner(kMouse, 400, 400, texts_.get("opt.mouse"), {off, on}, s.mouseControl ? 1 : 0);
    m.addButton(kConfKeys, 230, 430, 340, 32, "menu\\confkeys_1.tga", "menu\\confkeys_2.tga");
    m.addButton(kBack, 50, 450, 128, 64, "menu\\back_1.tga", "menu\\back_2.tga");
    m.addButton(kApply, 600, 450, 160, 64, "menu\\apply_ok_1.tga", "menu\\apply_ok_2.tga", {0, 0, 0.625f, 1},
                itemflag::Disabled | itemflag::Hidden);

    // Video items: disabled during a mission, absent where there is no video mode to choose.
    for (int id : {kResolution, kRefresh, kDepth, kFullscreen, kSound3D}) {
        MenuItem* it = m.find(id);
        if (!content_.videoOptions) it->setShown(false);
        else if (optionsInGame_) it->setEnabled(false);
    }
    auto refresh = [this](Menu& menu) {
        if (MenuItem* depth = menu.find(kDepth)) {
            if (content_.videoOptions && !optionsInGame_) depth->setEnabled(pending_.fullscreen);
            if (!pending_.fullscreen) { depth->index = 0; pending_.colorDepth = 0; }
        }
        if (MenuItem* apply = menu.find(kApply)) apply->setShown(videoDiffers(pending_, profile_.settings));
    };
    refresh(m);

    m.onItem = [this, refresh](Menu& menu, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        Settings& live = profile_.settings;
        switch (it.id) {
            case kResolution: pending_.videoMode = it.index; pending_.refreshRate = 0;
                if (MenuItem* r = menu.find(kRefresh)) r->index = 0;
                break;
            case kRefresh:
                pending_.refreshRate = it.index == 0 ? 0 : content_.refreshRates[static_cast<size_t>(it.index - 1)];
                break;
            case kDepth: pending_.colorDepth = it.index == 1 ? 16 : it.index == 2 ? 32 : 0; break;
            case kFullscreen: pending_.fullscreen = it.index == 1; break;
            case kSound3D: pending_.sound3D = it.index == 1; break;
            case kBrightness: live.brightness = std::round(it.value) / 10.0f; settingsChanged(); break;
            case kSfx: live.sfxVolume = std::round(it.value) / 10.0f; settingsChanged(); break;
            case kMusic: live.musicVolume = std::round(it.value) / 10.0f; settingsChanged(); break;
            case kCamera: live.camera = it.index; settingsChanged(); break;
            case kMouse:
                live.mouseControl = it.index == 1;
                live.applyMouseControlBindings();
                settingsChanged();
                break;
            case kConfKeys: open(Screen::Controls); return;
            case kBack:
                save(); // ours: settings are written when leaving Options, not only at exit
                menus_.pop();
                return;
            case kApply: {
                live.videoMode = pending_.videoMode;
                live.refreshRate = pending_.refreshRate;
                live.colorDepth = pending_.colorDepth;
                live.fullscreen = pending_.fullscreen;
                live.sound3D = pending_.sound3D;
                save();
                host_.applyVideoSettings(live);
                settingsChanged();
                // Full restart without the logo pages (frontend.md 3.6): new main menu,
                // helicopter choices reset to 1 / 0.
                heli_[0] = 1;
                heli_[1] = 0;
                heliAlternator_ = 0;
                campaign_ = Campaign{};
                paused_ = hudHidden_ = false;
                state_ = FrontendState::Attract;
                host_.loadAttract();
                showMainMenu();
                return;
            }
            default: break;
        }
        refresh(menu);
    };
    m.drawBack = [](MenuDrawContext& c) {
        widgets::letterbox(c);
        widgets::header(c, "menu\\optionsh", 272, 63, 256, 64);
        widgets::panel(c, 210, 140, 380, 290);
    };
    return m;
}

// ---------------------------------------------------------------------------
// Configure controls
// ---------------------------------------------------------------------------
Menu Frontend::buildControls() {
    Menu m;
    controlsPlayer_ = 0; // always starts at Player 1
    captureRow_ = -1;
    m.addSpinner(kControlsSet, 400, 145, texts_.get("ctl.set"), {texts_.get("ctl.player.1"), texts_.get("ctl.player.2")}, 0);
    for (int row = 0; row < kActionCount; row++) {
        const float y = kRowY[row];
        m.addCustom(kRowBase + row, {220, y - 2, 360, 20}, [this, row, y](MenuDrawContext& c, MenuItem& it, bool focused) {
            const int (&k)[2] = profile_.settings.keys[controlsPlayer_][row];
            std::string keysText = k[0] > 0 ? keys::name(k[0]) : texts_.get("ctl.unbound");
            if (k[0] > 0 && k[1] > 0) keysText += texts_.get("ctl.or") + keys::name(k[1]);
            Color col = rust();
            if (it.disabled()) col = grey(0x80 / 255.0f);
            else if (focused) {
                col = orange();
                c.r.rect(it.hit.x, it.hit.y, it.hit.w, it.hit.h, packed(0x80000060u), Blend::Alpha);
            }
            widgets::text(c, 392, y, texts_.get("ctl.row." + std::to_string(row)), col, Align::Right);
            if (captureRow_ == row) {
                if ((c.ms / 250) % 2 == 1) widgets::text(c, 400, y, "=", orange(), Align::Center);
            } else {
                widgets::text(c, 408, y, keysText, col);
            }
        });
    }
    m.addButton(1, 50, 450, 128, 64, "menu\\back_1.tga", "menu\\back_2.tga");
    // Touch mode: while a row waits for a key, buttons for Backspace (Clear) and Esc (Cancel).
    m.addTextButton(kTouchClear, kClearButton, texts_.get("touch.clear"), itemflag::Disabled | itemflag::Hidden);
    m.addTextButton(kTouchCancel, kCancelButton, texts_.get("touch.cancel"), itemflag::Disabled | itemflag::Hidden);

    auto setCapture = [this](Menu& menu, int row) {
        captureRow_ = row;
        for (MenuItem& it : menu.items) {
            if (it.id >= kRowBase && it.id < kRowBase + kActionCount) it.setEnabled(row < 0 || it.id == kRowBase + row);
            if (it.id == 1 || it.id == kControlsSet) it.setEnabled(row < 0);
            if (it.id == kTouchClear || it.id == kTouchCancel) it.setShown(row >= 0 && touch_);
        }
    };
    m.onItem = [this, setCapture](Menu& menu, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        if (it.id == 1) {
            save();
            menus_.pop();
        } else if (it.id == kControlsSet) {
            controlsPlayer_ = it.index;
        } else if (it.id >= kRowBase && it.id < kRowBase + kActionCount && captureRow_ < 0) {
            setCapture(menu, it.id - kRowBase);
        }
    };
    m.onKey = [this, setCapture](Menu& menu, int code) {
        if (captureRow_ >= 0) {
            const Action a = static_cast<Action>(captureRow_);
            if (code == keys::Escape) {
                setCapture(menu, -1);
                return true;
            }
            if (touch_ && code == keys::Mouse1) {
                const float px = menus_.pointerX(), py = menus_.pointerY();
                if (kCancelButton.contains(px, py)) {
                    menus_.playSound("sounds\\menu1.wav");
                    setCapture(menu, -1);
                    return true;
                }
                if (kClearButton.contains(px, py)) {
                    menus_.playSound("sounds\\menu1.wav");
                    profile_.settings.unbindKey(controlsPlayer_, a);
                    setCapture(menu, -1);
                    settingsChanged();
                    return true;
                }
            }
            profile_.settings.bindKey(controlsPlayer_, a, code);
            setCapture(menu, -1);
            settingsChanged();
            return true;
        }
        if (code == keys::Backspace || code == keys::Delete) {
            if (menu.focused >= 0) {
                const int id = menu.items[static_cast<size_t>(menu.focused)].id;
                if (id >= kRowBase && id < kRowBase + kActionCount) {
                    profile_.settings.unbindKey(controlsPlayer_, static_cast<Action>(id - kRowBase));
                    settingsChanged();
                    return true;
                }
            }
        }
        return false;
    };
    // The viewer can show a capture in progress (--state capture=N).
    m.onUpdate = [this, setCapture](Menu& menu, float, float) {
        bool rowsMatch = true;
        for (const MenuItem& it : menu.items)
            if (it.id == 1 && it.disabled() != (captureRow_ >= 0)) rowsMatch = false;
        if (!rowsMatch) setCapture(menu, captureRow_);
    };
    m.drawBack = [](MenuDrawContext& c) {
        widgets::letterbox(c);
        widgets::header(c, "menu\\controlsh", 242, 63, 316, 64);
        widgets::panel(c, 210, 165, 380, 280);
    };
    return m;
}

} // namespace as3d::ui
