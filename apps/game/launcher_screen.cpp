#include "launcher_screen.h"

#include <cmath>
#include <cstdio>

#include "as3d/image.h"

namespace as3d_game {

using namespace as3d;

namespace {

bool loadTexture(Vfs& vfs, const char* path, Texture2D& out, bool repeat = false) {
    Blob blob;
    Image image;
    if (!vfs.read(path, blob) || !decodeTga(blob.data(), blob.size(), image)) return false;
    TextureOptions o;
    o.minFilter = o.magFilter = Filter::Linear;
    o.wrapS = o.wrapT = repeat ? Wrap::Repeat : Wrap::ClampToEdge;
    out.create(image, o);
    return out.valid();
}

// "3 of 20 missions open, best 12 345" as the card's lines.
std::vector<std::string> saveLines(const LauncherEntry& e) {
    if (e.savePath.empty() && e.legacySavePath.empty()) return {};
    const SaveSummary s = readSaveSummary(e.savePath, e.legacySavePath, *e.game);
    if (!s.found || !s.readable) return {describeSave(s)};
    std::vector<std::string> lines{std::to_string(s.missionsUnlocked) + " of " + std::to_string(s.missionCount) +
                                   " missions open"};
    const std::string all = describeSave(s);
    const size_t best = all.find(", best ");
    if (best != std::string::npos) lines.push_back("Best score " + all.substr(best + 7));
    return lines;
}

} // namespace

const char* gameLogoPath(GameId id) {
    switch (id) {
        case GameId::AirStrike2: return "gfx\\logo\\logo.tga";
        case GameId::GulfThunder: return "gfx\\logo\\logo_gulf.tga";
        default: return "";
    }
}

ui::Marquee gameMarquee(GameId id) {
    switch (id) {
        case GameId::AirStrike3D: return ui::Marquee::Banner;
        case GameId::AirStrike2: return ui::Marquee::TitleLogo;
        case GameId::GulfThunder: return ui::Marquee::Emblem;
        default: return ui::Marquee::None;
    }
}

bool LauncherScreen::init(const std::vector<LauncherEntry>& games, int preselected, bool touch, std::string* error) {
    games_ = games;
    logos_.clear();
    std::vector<ui::GameCard> cards;
    for (const LauncherEntry& e : games_) {
        ui::GameCard card;
        card.key = e.game->key;
        card.title = e.game->title;
        card.version = e.game->version;
        card.saveLines = saveLines(e);
        card.marquee = gameMarquee(e.game->id);
        // The game's files, mounted for as long as its pictures take to load (the banner's
        // stay).
        std::unique_ptr<Vfs> vfs(new Vfs());
        std::string err;
        if (!mountGameFiles(e.files, *vfs, nullptr, &err)) {
            AS3D_WARN("selector: %s: %s", e.game->key, err.c_str());
        } else {
            if (!font_.fontLoaded && loadTexture(*vfs, "gfx\\ui\\font.tga", font_.font)) {
                font_.fontLoaded = true;
                loadTexture(*vfs, "gfx\\ui\\font_alpha.tga", font_.fontAlpha);
                fontGame_ = e.game->key;
            }
            auto picture = [&](const char* path, bool repeat) -> const Texture2D* {
                std::unique_ptr<Texture2D> t(new Texture2D());
                if (!loadTexture(*vfs, path, *t, repeat)) return nullptr;
                logos_.push_back(std::move(t));
                return logos_.back().get();
            };
            if (card.marquee == ui::Marquee::TitleLogo || card.marquee == ui::Marquee::Emblem) {
                card.logo = picture(gameLogoPath(e.game->id), false);
                card.clouds = picture("gfx\\logo\\clouds.tga", true);
                if (card.marquee == ui::Marquee::TitleLogo) {
                    card.glow = picture("gfx\\logo\\glow.tga", false);
                    card.two = picture("gfx\\logo\\two3.tga", false);
                }
            } else if (card.marquee == ui::Marquee::Banner && !banner_.valid()) {
                std::unique_ptr<DefDatabase> defs(new DefDatabase());
                std::string bannerErr;
                if (defs->load(*vfs) && banner_.init(*vfs, *defs, &bannerErr)) {
                    bannerDefs_ = std::move(defs);
                    bannerVfs_ = std::move(vfs);
                } else {
                    AS3D_WARN("selector: %s: no banner: %s", e.game->key, bannerErr.c_str());
                }
            }
        }
        cards.push_back(std::move(card));
    }
    if (!font_.fontLoaded) {
        if (error) *error = "no game provides gfx\\ui\\font.tga for the game selector";
        return false;
    }
    sel_.reset(new ui::GameSelector(std::move(cards), preselected));
    sel_->setTouchMode(touch);
    if (banner_.valid()) {
        sel_->setBannerDrawer([this](const ui::RectF& box, float clock) {
            // The mesh fills the marquee box with the part of the 800x200 banner viewport that
            // the title covers (ui::kBannerContent), the viewport scaled and placed to do so,
            // scissored to the box.
            const ui::BannerContent& k = ui::kBannerContent;
            float vw = box.w / (k.x1 - k.x0);
            vw = std::min(vw, box.h / (k.y1 - k.y0) * 4.0f);
            const float vh = vw * 0.25f;
            const float vx = box.x + box.w * 0.5f - (k.x0 + k.x1) * 0.5f * vw;
            const float vy = box.y + box.h * 0.5f - (k.y0 + k.y1) * 0.5f * vh;
            const ui::Mapping m = ui::computeMapping(fbW_, fbH_);
            auto px = [](float v) { return static_cast<int>(std::lround(v)); };
            const int x0 = px(m.toFbX(vx)), y0 = px(m.toFbY(vy)), x1 = px(m.toFbX(vx + vw)), y1 = px(m.toFbY(vy + vh));
            const int cx0 = px(m.toFbX(box.x)), cy0 = px(m.toFbY(box.y)), cx1 = px(m.toFbX(box.x + box.w)),
                      cy1 = px(m.toFbY(box.y + box.h));
            const int clip[4] = {cx0, cy0, cx1 - cx0, cy1 - cy0};
            banner_.draw(clock, fbW_, fbH_, x0, y0, x1 - x0, y1 - y0, clip, loopFit_);
        });
    }
    return true;
}

void LauncherScreen::setTouchMode(bool on) {
    if (sel_) sel_->setTouchMode(on);
}

void LauncherScreen::setLoopFit(bool on) {
    loopFit_ = on;
    if (sel_) sel_->setLoopFit(on);
}

void LauncherScreen::setScreen(int fbWidth, int fbHeight, const SafeInsets& in, int screenMode) {
    if (!sel_) return;
    fbW_ = fbWidth;
    fbH_ = fbHeight;
    const ui::Mapping m = ui::computeMapping(fbWidth, fbHeight);
    sel_->setView(m.left(), m.right(), screenMode == kScreen4x3);
    sel_->setSafeArea(m.toVirtX(static_cast<float>(in.left)), m.toVirtY(static_cast<float>(in.top)),
                      m.toVirtX(static_cast<float>(fbWidth - in.right)), m.toVirtY(static_cast<float>(fbHeight - in.bottom)));
}

void LauncherScreen::update(float dt, const ui::UiInput& input) {
    if (sel_) sel_->update(dt, input);
}

void LauncherScreen::draw(ui::Renderer2D& r, int fbWidth, int fbHeight) {
    fbW_ = fbWidth;
    fbH_ = fbHeight;
    r.begin(fbWidth, fbHeight);
    if (sel_) sel_->draw(r, font_);
    r.flush();
}

const GameProfile* LauncherScreen::chosen() const {
    if (!sel_ || sel_->chosen() < 0 || sel_->chosen() >= static_cast<int>(games_.size())) return nullptr;
    return games_[static_cast<size_t>(sel_->chosen())].game;
}

std::string LauncherScreen::layoutMarker() const {
    if (!sel_) return std::string();
    std::string s = "cards=";
    char buf[96];
    auto rect = [&](const ui::RectF& b) {
        std::snprintf(buf, sizeof buf, "%.0f,%.0f,%.0f,%.0f", b.x, b.y, b.w, b.h);
        return std::string(buf);
    };
    for (size_t i = 0; i < games_.size(); ++i)
        s += (i ? ";" : "") + std::string(games_[i].game->key) + "@" + rect(sel_->cardRect(static_cast<int>(i)));
    s += " play=" + rect(sel_->playRect()) + " exit=" + rect(sel_->exitRect());
    s += " current=" + std::string(games_.empty() ? "" : games_[static_cast<size_t>(sel_->current())].game->key);
    return s;
}

} // namespace as3d_game
