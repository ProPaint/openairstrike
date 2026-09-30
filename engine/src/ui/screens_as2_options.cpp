// The sequels' Options (S6) and Configure Controls (S7): the first game's items and logic in
// the sequels' layout (rows 40 px lower, the panel, text buttons). docs/spec/as2/frontend.md
// 3.6, 3.7; our own rows (Screen, Controls hand, Touch speed, Show FPS) as issue 140 places
// them for the first game, in the rows of the video options this port does not offer.
#include <algorithm>
#include <cmath>

#include "as2_screens.h"

namespace as3d::ui {

namespace {

using namespace as2;

enum OptionId { kBack = 1, kConfKeys = 2, kApply = 3, kResolution = 20, kRefresh, kDepth, kFullscreen, kBrightness,
                kSfx, kMusic, kSound3D, kCamera, kMouse, kScreenMode = 40, kHand, kTouchSpeed, kShowFps };

bool videoDiffers(const Settings& a, const Settings& b) {
    return a.videoMode != b.videoMode || a.refreshRate != b.refreshRate || a.colorDepth != b.colorDepth ||
           a.fullscreen != b.fullscreen || a.sound3D != b.sound3D;
}

// Controls rows (as2/frontend.md 3.7): the row table has "-" separators, hence the gaps.
constexpr float kRowY[kActionCount] = {210, 230, 270, 290, 330, 350, 390, 410, 430, 450};
constexpr int kRowBase = 100, kControlsSet = 10, kTouchClear = 51, kTouchCancel = 50;

} // namespace

Menu SequelScreens::options(Frontend& f) {
    Menu m;
    f.pending_ = f.profile_.settings;
    const Settings& s = f.profile_.settings;
    const std::string off = tr(f, "opt.off"), on = tr(f, "opt.on");
    auto spinner = [&m](int id, float y, std::string label, std::vector<std::string> values, int index) -> MenuItem& {
        MenuItem& it = m.addSpinner(id, 400, y, std::move(label), std::move(values), index);
        it.flags |= itemflag::NoHoverSound;
        return it;
    };
    auto slider = [&m](int id, float y, std::string label, int lo, int hi, float v) {
        m.addSlider(id, 400, y, std::move(label), lo, hi, v).flags |= itemflag::NoHoverSound;
    };
    std::vector<std::string> modes;
    for (int i = 0; i < std::clamp(f.content_.videoModeCount, 1, 9); i++) modes.push_back(kVideoModeNames[i]);
    spinner(kResolution, 200, tr(f, "opt.resolution"), modes, std::min(s.videoMode, static_cast<int>(modes.size()) - 1));
    std::vector<std::string> rates{tr(f, "opt.refresh.default")};
    int rateIndex = 0;
    for (size_t i = 0; i < f.content_.refreshRates.size(); i++) {
        rates.push_back(std::to_string(f.content_.refreshRates[i]) + tr(f, "opt.hz"));
        if (f.content_.refreshRates[i] == s.refreshRate) rateIndex = static_cast<int>(i) + 1;
    }
    spinner(kRefresh, 220, tr(f, "opt.refresh"), rates, rateIndex);
    spinner(kDepth, 240, tr(f, "opt.depth"), {tr(f, "opt.depth.0"), tr(f, "opt.depth.16"), tr(f, "opt.depth.32")},
            s.colorDepth == 16 ? 1 : s.colorDepth == 32 ? 2 : 0);
    spinner(kFullscreen, 260, tr(f, "opt.fullscreen"), {off, on}, s.fullscreen ? 1 : 0);
    slider(kBrightness, 280, tr(f, "opt.brightness"), 2, 10, std::round(s.brightness * 10));
    slider(kSfx, 320, tr(f, "opt.sfx"), 0, 10, std::round(s.sfxVolume * 10));
    slider(kMusic, 340, tr(f, "opt.music"), 0, 10, std::round(s.musicVolume * 10));
    spinner(kSound3D, 360, tr(f, "opt.sound3d"), {off, on}, s.sound3D ? 1 : 0);
    std::vector<std::string> cams;
    for (int i = 0; i < 4; i++) cams.push_back(tr(f, "camera." + std::to_string(i)));
    spinner(kCamera, 400, tr(f, "opt.camera"), cams, s.camera);
    spinner(kMouse, 440, tr(f, "opt.mouse"), {off, on}, s.mouseControl ? 1 : 0);
    // Ours (issue 140), in the rows of the video options when the port offers none.
    {
        float y = 200;
        if (f.content_.screenOption && !f.content_.videoOptions) {
            spinner(kScreenMode, y, tr(f, "opt.screen"), {tr(f, "opt.screen.wide"), tr(f, "opt.screen.4x3")},
                    s.screenMode == kScreen4x3 ? 1 : 0);
            y += 20;
        }
        if (f.content_.handOption && f.touch_ && !f.content_.videoOptions) {
            spinner(kHand, y, tr(f, "opt.controls"), {tr(f, "opt.controls.right"), tr(f, "opt.controls.left")},
                    s.leftHanded ? 1 : 0);
            y += 20;
            std::vector<std::string> speeds;
            for (int i = 0; i < kTouchSpeedSteps; i++) speeds.push_back(tr(f, "opt.touchspeed." + std::to_string(i)));
            spinner(kTouchSpeed, y, tr(f, "opt.touchspeed"), speeds, s.touchSpeed);
        }
        if (!f.content_.videoOptions) spinner(kShowFps, 360, tr(f, "opt.showfps"), {off, on}, s.showFps ? 1 : 0);
    }
    m.addSequelButton(kConfKeys, 400, 520, tr(f, "button.configure_controls"), itemflag::AlignCenter);
    m.addSequelButton(kBack, 50, 520, tr(f, "button.back_wide"));
    m.addSequelButton(kApply, 620, 520, tr(f, "button.apply"), itemflag::Disabled | itemflag::Hidden);

    if (!f.content_.mouseControlOption) m.find(kMouse)->setShown(false);
    for (int id : {kResolution, kRefresh, kDepth, kFullscreen, kSound3D}) {
        MenuItem* it = m.find(id);
        if (!f.content_.videoOptions) it->setShown(false);
        else if (f.optionsInGame_) it->setEnabled(false);
    }
    auto refresh = [&f](Menu& menu) {
        if (MenuItem* depth = menu.find(kDepth)) {
            if (f.content_.videoOptions && !f.optionsInGame_) depth->setEnabled(f.pending_.fullscreen);
            if (!f.pending_.fullscreen) { depth->index = 0; f.pending_.colorDepth = 0; }
        }
        if (MenuItem* apply = menu.find(kApply)) {
            const bool show = videoDiffers(f.pending_, f.profile_.settings);
            if (show != !apply->hidden()) apply->setShown(show); // it slides in and out (2.5)
        }
    };
    refresh(m);
    m.onItem = [&f, refresh](Menu& menu, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        Settings& live = f.profile_.settings;
        switch (it.id) {
            case kResolution: f.pending_.videoMode = it.index; f.pending_.refreshRate = 0;
                if (MenuItem* r = menu.find(kRefresh)) r->index = 0;
                break;
            case kRefresh:
                f.pending_.refreshRate = it.index == 0 ? 0 : f.content_.refreshRates[static_cast<size_t>(it.index - 1)];
                break;
            case kDepth: f.pending_.colorDepth = it.index == 1 ? 16 : it.index == 2 ? 32 : 0; break;
            case kFullscreen: f.pending_.fullscreen = it.index == 1; break;
            case kSound3D: f.pending_.sound3D = it.index == 1; break;
            case kBrightness: live.brightness = std::round(it.value) / 10.0f; f.settingsChanged(); break;
            case kSfx: live.sfxVolume = std::round(it.value) / 10.0f; f.settingsChanged(); break;
            case kMusic: live.musicVolume = std::round(it.value) / 10.0f; f.settingsChanged(); break;
            case kCamera: live.camera = it.index; f.settingsChanged(); break;
            case kScreenMode: live.screenMode = it.index == 1 ? kScreen4x3 : kScreenWide; f.settingsChanged(); break;
            case kHand: live.leftHanded = it.index == 1; f.settingsChanged(); break;
            case kTouchSpeed: live.touchSpeed = it.index; f.settingsChanged(); break;
            case kShowFps: live.showFps = it.index == 1; f.settingsChanged(); break;
            case kMouse:
                live.mouseControl = it.index == 1;
                live.applyMouseControlBindings();
                f.settingsChanged();
                break;
            case kConfKeys: f.open(Screen::Controls); return;
            case kBack:
                f.save(); // ours: written when leaving Options
                f.menus_.pop();
                return;
            case kApply: {
                live.videoMode = f.pending_.videoMode;
                live.refreshRate = f.pending_.refreshRate;
                live.colorDepth = f.pending_.colorDepth;
                live.fullscreen = f.pending_.fullscreen;
                live.sound3D = f.pending_.sound3D;
                f.save();
                f.host_.applyVideoSettings(live);
                f.settingsChanged();
                // The restart without the logo pages: a new main menu (as2/frontend.md 6.3).
                f.heli_[0] = f.heli_[1] = 0;
                f.campaign_ = Campaign(f.rules());
                f.paused_ = f.hudHidden_ = false;
                f.state_ = FrontendState::Attract;
                f.host_.loadAttract();
                f.showMainMenu();
                return;
            }
            default: break;
        }
        refresh(menu);
    };
    m.drawFront = [&f](MenuDrawContext& c) {
        panel(c.r, c.a, 210, 180, 380, 290, c.menu.open, tr(f, "title.options"));
        titleLogo(c.r, c.a, f.sq_->logoClock);
    };
    return m;
}

Menu SequelScreens::controls(Frontend& f) {
    Menu m;
    f.controlsPlayer_ = 0; // always Player 1 first
    f.captureRow_ = -1;
    m.addSpinner(kControlsSet, 400, 180, tr(f, "ctl.set"), {tr(f, "ctl.player.1"), tr(f, "ctl.player.2")}, 0).flags |=
        itemflag::NoHoverSound;
    for (int row = 0; row < kActionCount; row++) {
        const float y = kRowY[row];
        MenuItem& it = m.addCustom(kRowBase + row, {220, y - 2, 360, 20}, [&f, row, y](MenuDrawContext& c, MenuItem& self, bool focused) {
            const int (&k)[2] = f.profile_.settings.keys[f.controlsPlayer_][row];
            std::string keysText = k[0] > 0 ? keys::name(k[0]) : tr(f, "ctl.unbound");
            if (k[0] > 0 && k[1] > 0) keysText += tr(f, "ctl.or") + keys::name(k[1]);
            Color col = green();
            if (self.disabled()) {
                col = disabledGrey();
            } else if (focused) {
                col = orange();
                c.r.rect(self.hit.x, self.hit.y, self.hit.w, self.hit.h, darkGreenBox(), Blend::Alpha);
            }
            text(c.r, c.a, 392, y, tr(f, "ctl.row." + std::to_string(row)), col, Align::Right);
            if (f.captureRow_ == row) {
                if ((c.ms / 250) % 2 == 1) text(c.r, c.a, 403, y, "=", orange());
            } else {
                text(c.r, c.a, 408, y, keysText, col);
            }
        });
        it.flags |= itemflag::NoHoverSound;
    }
    m.addSequelButton(1, 50, 520, tr(f, "button.back"));
    // Touch: while a row waits for a key, Clear (Backspace) and Cancel (Esc), as text buttons.
    m.addSequelButton(kTouchClear, 330, 520, tr(f, "button.clear"), itemflag::AlignCenter | itemflag::Disabled | itemflag::Hidden);
    m.addSequelButton(kTouchCancel, 490, 520, tr(f, "button.cancel"), itemflag::AlignCenter | itemflag::Disabled | itemflag::Hidden);
    auto setCapture = [&f](Menu& menu, int row) {
        f.captureRow_ = row;
        for (MenuItem& it : menu.items) {
            if (it.id >= kRowBase && it.id < kRowBase + kActionCount) it.setEnabled(row < 0 || it.id == kRowBase + row);
            if (it.id == 1 || it.id == kControlsSet) it.setEnabled(row < 0);
            if (it.id == kTouchClear || it.id == kTouchCancel) {
                const bool show = row >= 0 && f.touch_;
                if (show != !it.hidden()) it.setShown(show);
            }
        }
    };
    m.onItem = [&f, setCapture](Menu& menu, MenuItem& it, int ev) {
        if (ev != kActivate) return;
        if (it.id == 1) {
            f.save();
            f.menus_.pop();
        } else if (it.id == kControlsSet) {
            f.controlsPlayer_ = it.index;
        } else if (it.id >= kRowBase && it.id < kRowBase + kActionCount && f.captureRow_ < 0) {
            setCapture(menu, it.id - kRowBase);
        }
    };
    m.onKey = [&f, setCapture](Menu& menu, int code) {
        if (f.captureRow_ >= 0) {
            const Action a = static_cast<Action>(f.captureRow_);
            if (code == keys::Escape) {
                setCapture(menu, -1);
                return true;
            }
            if (f.touch_ && code == keys::Mouse1) {
                const float px = f.menus_.pointerX(), py = f.menus_.pointerY();
                for (const MenuItem& it : menu.items) {
                    if ((it.id != kTouchClear && it.id != kTouchCancel) || !it.hit.contains(px, py)) continue;
                    f.menus_.playSound("sounds\\menu1.wav");
                    if (it.id == kTouchClear) {
                        f.profile_.settings.unbindKey(f.controlsPlayer_, a);
                        f.settingsChanged();
                    }
                    setCapture(menu, -1);
                    return true;
                }
            }
            f.profile_.settings.bindKey(f.controlsPlayer_, a, code);
            setCapture(menu, -1);
            f.settingsChanged();
            return true;
        }
        if ((code == keys::Backspace || code == keys::Delete) && menu.focused >= 0) {
            const int id = menu.items[static_cast<size_t>(menu.focused)].id;
            if (id >= kRowBase && id < kRowBase + kActionCount) {
                f.profile_.settings.unbindKey(f.controlsPlayer_, static_cast<Action>(id - kRowBase));
                f.settingsChanged();
                return true;
            }
        }
        return false;
    };
    // The viewer can show a capture in progress (--state capture=N).
    m.onUpdate = [&f, setCapture](Menu& menu, float, float) {
        for (const MenuItem& it : menu.items)
            if (it.id == 1 && it.disabled() != (f.captureRow_ >= 0)) {
                setCapture(menu, f.captureRow_);
                break;
            }
    };
    m.drawFront = [&f](MenuDrawContext& c) {
        panel(c.r, c.a, 190, 165, 420, 320, c.menu.open, tr(f, "title.controls"));
        c.r.outline(200, 205, 400, 270, greenOutline(), Blend::Alpha);
        titleLogo(c.r, c.a, f.sq_->logoClock);
    };
    return m;
}

} // namespace as3d::ui
