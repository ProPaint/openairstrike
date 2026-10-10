// The game selector and the front end loop around it (ctr_selector.h).
#include "ctr_selector.h"

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/frontend.h"
#include "as3d/game_data.h"
#include "as3d/image.h"
#include "as3d/launcher.h"
#include "as3d/platform.h"
#include "as3d/render_state.h"
#include "as3d/ui.h"
#include "as3d/vfs.h"
#include "ctr_context.h"
#include "ctr_fatal.h"
#include "ctr_frame.h"
#include "ctr_input.h"
#include "ctr_install.h"
#include "ctr_loop.h"
#include "ctr_mem.h"
#include "ctr_perf.h"
#include "flat_fill.h"
#include "game_session.h"
#include "pica_frame.h"

namespace ctrapp {

using namespace as3d;
using as3d::ctr::CtrContext;

namespace {

constexpr int kTopW = as3d::ctr::kTopWidth, kTopH = as3d::ctr::kTopHeight;
constexpr int kBottomW = as3d::ctr::kBottomWidth, kBottomH = as3d::ctr::kBottomHeight;

std::string choicePath() {
    const std::string dir = userDataDir();
    return dir.empty() ? std::string() : dir + "launcher.bin";
}

bool loadPicture(Vfs& vfs, const char* path, Texture2D& out, bool repeat) {
    Blob blob;
    Image image;
    if (!vfs.read(path, blob) || !decodeTga(blob.data(), blob.size(), image)) return false;
    TextureOptions o;
    o.minFilter = o.magFilter = Filter::Linear;
    o.wrapS = o.wrapT = repeat ? Wrap::Repeat : Wrap::ClampToEdge;
    out.create(image, o);
    return out.valid();
}

std::unique_ptr<Vfs> mountPaks(const GameProfile& g, std::string* error) {
    as3d_game::GameOptions o;
    o.game = &g;
    const std::string dir = std::string(kDataDir) + "/" + g.key;
    o.extractedDir = dir;
    for (const char* const* p = g.paks; *p; ++p) o.paks.push_back(dir + "/" + *p);
    std::unique_ptr<Vfs> vfs(new Vfs());
    if (!as3d_game::mountGameFiles(o, *vfs, nullptr, error)) return nullptr;
    return vfs;
}

// The save line(s) of a card: the missing files of a part-installed game, else the save summary.
std::vector<std::string> cardLines(const GameInstall& gi) {
    if (!gi.complete()) {
        std::vector<std::string> l{"Missing: " + gi.missing.front()};
        if (gi.missing.size() > 1) l.push_back("and " + std::to_string(gi.missing.size() - 1) + " more");
        return l;
    }
    const std::string dir = userDataDir();
    const SaveSummary s = readSaveSummary(dir.empty() ? std::string() : dir + gi.game->key + "/profile.bin", "", *gi.game);
    if (!s.found || !s.readable) return {describeSave(s)};
    std::vector<std::string> lines{std::to_string(s.missionsUnlocked) + " of " + std::to_string(s.missionCount) +
                                   " missions open"};
    const std::string all = describeSave(s);
    const size_t best = all.find(", best ");
    if (best != std::string::npos) lines.push_back("Best score " + all.substr(best + 7));
    return lines;
}

ui::Marquee marqueeOf(GameId id) {
    switch (id) {
        case GameId::AirStrike3D: return ui::Marquee::Banner; // no banner mesh here: the title in text
        case GameId::AirStrike2: return ui::Marquee::TitleLogo;
        case GameId::GulfThunder: return ui::Marquee::Emblem;
        default: return ui::Marquee::None;
    }
}

const char* logoOf(GameId id) {
    switch (id) {
        case GameId::AirStrike2: return "gfx\\logo\\logo.tga";
        case GameId::GulfThunder: return "gfx\\logo\\logo_gulf.tga";
        default: return "";
    }
}

void emptyFrames(CtrContext& ctx) {
    for (int k = 0; k < 2; ++k) {
        if (!ctx.beginFrame()) break;
        pica::frameBegin();
        pica::frameEnd();
        ctx.endFrame();
    }
}

struct Selection {
    std::string key; // the game to start; "" = leave the app
};

// The selector screen. `offered`: the installs shown (any file present), in GameId order.
Selection runSelector(CtrContext& ctx, CtrInput& input, const RunConfig& run, const std::vector<const GameInstall*>& offered,
                      const std::string& last) {
    Selection result;
    if (!pica::initBackend()) {
        AS3D_ERROR("selector: pica::initBackend failed");
        return result;
    }
    // The font comes from the first game whose paks are all there.
    const GameInstall* fontGame = nullptr;
    for (const GameInstall* g : offered)
        if (!fontGame && g->paksPresent) fontGame = g;

    std::unique_ptr<ui::Renderer2D> top(new ui::Renderer2D()), bottom(new ui::Renderer2D());
    std::unique_ptr<Vfs> fontVfs;
    std::unique_ptr<ui::UiAssets> assets(new ui::UiAssets());
    std::vector<std::unique_ptr<Texture2D>> pictures;
    std::vector<ui::GameCard> cards;
    std::string err;
    bool ready = top->init(&err) && bottom->init(&err);
    if (ready && fontGame) {
        fontVfs = mountPaks(*fontGame->game, &err);
        ready = fontVfs && assets->load(*fontVfs, &err, fontGame->game->id);
    } else {
        ready = false;
    }
    if (!ready) {
        AS3D_ERROR("selector: no font (%s): the screen stays plain", err.empty() ? "no game with its paks" : err.c_str());
    } else {
        for (const GameInstall* gi : offered) {
            ui::GameCard card;
            card.key = gi->game->key;
            card.title = gi->game->title;
            card.version = gi->game->version;
            card.saveLines = cardLines(*gi);
            card.marquee = marqueeOf(gi->game->id);
            if ((card.marquee == ui::Marquee::TitleLogo || card.marquee == ui::Marquee::Emblem) && gi->paksPresent) {
                std::string e2;
                std::unique_ptr<Vfs> vfs = gi == fontGame ? nullptr : mountPaks(*gi->game, &e2);
                Vfs& v = gi == fontGame ? *fontVfs : *vfs;
                if (gi == fontGame || vfs) {
                    auto picture = [&](const char* path, bool repeat) -> const Texture2D* {
                        std::unique_ptr<Texture2D> t(new Texture2D());
                        if (!loadPicture(v, path, *t, repeat)) return nullptr;
                        pictures.push_back(std::move(t));
                        return pictures.back().get();
                    };
                    card.logo = picture(logoOf(gi->game->id), false);
                    card.clouds = picture("gfx\\logo\\clouds.tga", true);
                    if (card.marquee == ui::Marquee::TitleLogo) {
                        card.glow = picture("gfx\\logo\\glow.tga", false);
                        card.two = picture("gfx\\logo\\two3.tga", false);
                    }
                }
            }
            cards.push_back(std::move(card));
        }
    }

    const ui::Mapping map = ui::computeMapping(kBottomW, kBottomH);
    int pre = 0;
    for (size_t i = 0; i < offered.size(); ++i)
        if (last == offered[i]->game->key) pre = static_cast<int>(i);
    std::unique_ptr<ui::GameSelector> sel;
    auto makeSelector = [&](int focus) {
        sel.reset(new ui::GameSelector(cards, focus));
        sel->setTouchMode(true);
        sel->setView(map.left(), map.right(), true); // the 4:3 screen: everything inside the 800x600 field
    };
    if (ready) makeSelector(pre);

    unsigned frameNo = 0;
    int shot = 0, lastCurrent = -1;
    unsigned shotAt = 40;
    uint64_t lastT = 0;
    std::string shotName;
    while (aptMainLoop()) {
        const PadState& pad = input.readPad();
        if ((pad.held & (pad::Start | pad::Select)) == (pad::Start | pad::Select)) {
            AS3D_INFO("quit: START+SELECT");
            break;
        }
        const uint64_t now = nowTicks();
        const float dt = lastT ? static_cast<float>(std::min(0.1, ticksToMs(now - lastT) / 1000.0)) : 0.0f;
        lastT = now;
        if (sel) {
            sel->update(dt, input.uiInputs(map));
            if (sel->exitRequested()) {
                AS3D_INFO("selector: Exit");
                break;
            }
            if (sel->chosen() >= 0) {
                const int idx = sel->chosen();
                const GameInstall& gi = *offered[static_cast<size_t>(idx)];
                if (gi.complete()) {
                    AS3D_INFO("selector: chose %s after %u frames (pad reads so far %u)", gi.game->key, frameNo, frameNo);
                    result.key = gi.game->key;
                    break;
                }
                AS3D_WARN("selector: %s cannot start, missing: %s", gi.game->key, missingList(gi).c_str());
                makeSelector(idx);
            }
            if (sel->current() != lastCurrent) {
                lastCurrent = sel->current();
                shotAt = frameNo + 12;
                char b[64];
                std::snprintf(b, sizeof b, "%ssel_%02d_%s", run.dumpPrefix.c_str(), shot++, offered[static_cast<size_t>(lastCurrent)]->game->key);
                shotName = b;
                AS3D_INFO("AS3D_SELECTOR focus=%s frame=%u", offered[static_cast<size_t>(lastCurrent)]->game->key, frameNo);
            }
        }

        // The lists, outside the frame.
        const bool shown = sel != nullptr;
        if (shown) {
            bottom->begin(kBottomW, kBottomH);
            sel->draw(*bottom, *assets);
            top->begin(kTopW, kTopH);
            const GameInstall& cur = *offered[static_cast<size_t>(sel->current())];
            ui::TextStyle st;
            st.align = ui::Align::Center;
            st.scale = 3.0f;
            st.color = ui::orange();
            ui::drawTextShadowed(*top, assets->uiFont(), 400, 200, cur.game->title, st);
            st.scale = 1.5f;
            st.color = ui::Color{1, 1, 1, 0.85f};
            ui::drawText(*top, assets->uiFont(), 400, 290, std::string("Version ") + cur.game->version, st);
            st.scale = 1.0f;
            st.color = cur.complete() ? ui::Color{1, 1, 1, 0.6f} : ui::Color{1, 0.4f, 0.3f, 0.9f};
            ui::drawText(*top, assets->uiFont(), 400, 380,
                         cur.complete() ? "A: play    B: exit    START+SELECT: quit" : ("Missing: " + missingList(cur)).c_str(), st);
        }
        if (ctx.beginFrame()) {
            pica::frameBegin();
            const int eyes = ctx.eyeCount();
            for (int eye = 0; eye < eyes; ++eye) {
                pica::drawOn(pica::FrameTarget{ctx.topTarget(eye), kTopW, kTopH, true});
                clear(Vec4{0.0f, 0.0f, 0.0f, 1.0f}, true, 1.0f);
                if (shown) {
                    top->flush();
                } else {
                    pica::fillRect(0, 0, kTopW, kTopH, 80, 0, 0);
                }
            }
            pica::drawOn(pica::FrameTarget{ctx.bottomTarget(), kBottomW, kBottomH, false});
            clear(Vec4{0.0f, 0.0f, 0.0f, 1.0f}, false, 1.0f);
            if (shown) bottom->flush();
            else pica::fillRect(0, 0, kBottomW, kBottomH, 80, 0, 0);
            pica::frameEnd();
            ctx.endFrame();
        }
        // run.ini dump_screens / dump_every: PPMs of the settled selector.
        if (run.dumpScreens && !shotName.empty() && frameNo >= shotAt) {
            dumpRenderTarget(ctx.topTarget(0), kTopW, std::string(kDataDir) + "/" + shotName + "_top.ppm");
            dumpRenderTarget(ctx.bottomTarget(), kBottomW, std::string(kDataDir) + "/" + shotName + "_bottom.ppm");
            AS3D_INFO("AS3D_DUMP %s frame=%u", shotName.c_str(), frameNo);
            shotName.clear();
        }
        if (run.dumpEvery > 0 && frameNo > 0 && frameNo % static_cast<unsigned>(run.dumpEvery) == 0) {
            char b[96];
            std::snprintf(b, sizeof b, "%ssel_f%05u", run.dumpPrefix.c_str(), frameNo);
            dumpRenderTarget(ctx.topTarget(0), kTopW, std::string(kDataDir) + "/" + b + "_top.ppm");
            dumpRenderTarget(ctx.bottomTarget(), kBottomW, std::string(kDataDir) + "/" + b + "_bottom.ppm");
        }
        ++frameNo;
    }

    // Release: the GPU done with the textures first.
    emptyFrames(ctx);
    sel.reset();
    pictures.clear();
    assets.reset();
    top.reset();
    bottom.reset();
    fontVfs.reset();
    pica::shutdownBackend();
    return result;
}

} // namespace

int runCtrFrontEnd(const RunConfig& base) {
    RunConfig run = base;
    bool new3ds = false;
    APT_CheckNew3DS(&new3ds);
    if (new3ds) osSetSpeedupEnable(true);

    hidScanInput();
    const bool startHeld = (hidKeysHeld() & KEY_START) != 0;

    std::unique_ptr<GraphicsContext> gl = createGraphicsContext(GraphicsConfig());
    CtrContext* ctx = as3d::ctr::ctrContext();
    if (!gl || !ctx) {
        setFatal("cannot create the graphics context");
        return 1;
    }
    CtrInput input(run, new3ds);
    int code = 0;
    for (int phase = 0;; ++phase) {
        Selection pick;
        std::vector<GameInstall> installs;
        bool offerChange = false;
        {
            Perf log;
            log.open(false);
            installs = detectInstalls(kDataDir);
            std::vector<const GameInstall*> offered;
            int complete = 0;
            for (const GameInstall& gi : installs) {
                if (gi.complete()) {
                    ++complete;
                    AS3D_INFO("selector: %s installed (%d files)", gi.game->key, gi.present);
                } else if (gi.any()) {
                    AS3D_WARN("selector: %s incomplete, missing: %s", gi.game->key, missingList(gi).c_str());
                } else {
                    AS3D_INFO("selector: %s not installed", gi.game->key);
                }
                if (gi.any()) offered.push_back(&gi);
            }
            offerChange = complete > 1;
            std::string last;
            readLauncherChoice(choicePath(), &last);
            // Nothing to start and no font to draw the selector with (no game has all its paks): the
            // hard exit, with what is missing (ctr_fatal.h).
            bool anyPaks = false;
            for (const GameInstall* g : offered) anyPaks = anyPaks || g->paksPresent;
            if (complete == 0 && !anyPaks) {
                std::string detail;
                for (const GameInstall* g : offered)
                    detail += std::string(detail.empty() ? "" : "\n") + g->game->key + "/ is missing " + missingList(*g);
                setFatal(offered.empty() ? std::string("no game found under ") + kDataDir + "/" : "no game is complete",
                         detail);
                code = 1;
                break;
            }
            // One game and nothing else part-installed: it starts, unless START is held at boot
            // (the first time only; "Change game" is not offered with one game).
            if (phase == 0 && offered.size() == 1 && offered[0]->complete() && !startHeld) {
                AS3D_INFO("selector: one game installed (%s), skipped (hold START at boot to show it)", offered[0]->game->key);
                pick.key = offered[0]->game->key;
            } else {
                if (offered.empty()) AS3D_ERROR("selector: no game found under %s/{as3d,as2,gulf}/", kDataDir);
                pick = runSelector(*ctx, input, run, offered, last);
            }
            if (!pick.key.empty() && !writeLauncherChoice(choicePath(), pick.key))
                AS3D_WARN("cannot write launcher.bin");
            logMem("selector", pick.key.c_str(), 0);
        }
        if (pick.key.empty()) break; // Exit, or START+SELECT
        run.game = pick.key;
        run.dumpPrefix = base.dumpPrefix + "g" + std::to_string(phase) + "_";
        GameFrame frame(run);
        CtrLoopOptions options;
        options.run = run;
        options.hook = &frame;
        options.frame = &frame;
        options.context = gl.get();
        options.input = &input;
        options.changeGameOffered = offerChange;
        code = runCtrMenuLoop(options);
        if (!options.changeGameRequested) break;
    }
    gl.reset();
    return code;
}

} // namespace ctrapp
