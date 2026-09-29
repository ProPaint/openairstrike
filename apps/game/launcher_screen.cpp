#include "launcher_screen.h"

#include <cstdio>

#include "as3d/image.h"

namespace as3d_game {

using namespace as3d;

namespace {

bool loadTexture(Vfs& vfs, const char* path, Texture2D& out) {
    Blob blob;
    Image image;
    if (!vfs.read(path, blob) || !decodeTga(blob.data(), blob.size(), image)) return false;
    TextureOptions o;
    o.minFilter = o.magFilter = Filter::Linear;
    o.wrapS = o.wrapT = Wrap::ClampToEdge;
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
        // The game's files, mounted for as long as its pictures take to load.
        Vfs vfs;
        std::string err;
        if (!mountGameFiles(e.files, vfs, nullptr, &err)) {
            AS3D_WARN("selector: %s: %s", e.game->key, err.c_str());
        } else {
            if (!font_.fontLoaded && loadTexture(vfs, "gfx\\ui\\font.tga", font_.font)) {
                font_.fontLoaded = true;
                loadTexture(vfs, "gfx\\ui\\font_alpha.tga", font_.fontAlpha);
                fontGame_ = e.game->key;
            }
            const char* logo = gameLogoPath(e.game->id);
            if (*logo) {
                std::unique_ptr<Texture2D> t(new Texture2D());
                if (loadTexture(vfs, logo, *t)) {
                    card.logo = t.get();
                    logos_.push_back(std::move(t));
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
    return true;
}

void LauncherScreen::setTouchMode(bool on) {
    if (sel_) sel_->setTouchMode(on);
}

void LauncherScreen::setScreen(int fbWidth, int fbHeight, const SafeInsets& in) {
    if (!sel_) return;
    const ui::Mapping m = ui::computeMapping(fbWidth, fbHeight);
    sel_->setSafeArea(m.toVirtX(static_cast<float>(in.left)), m.toVirtY(static_cast<float>(in.top)),
                      m.toVirtX(static_cast<float>(fbWidth - in.right)), m.toVirtY(static_cast<float>(fbHeight - in.bottom)));
}

void LauncherScreen::update(float dt, const ui::UiInput& input) {
    if (sel_) sel_->update(dt, input);
}

void LauncherScreen::draw(ui::Renderer2D& r, int fbWidth, int fbHeight) {
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
