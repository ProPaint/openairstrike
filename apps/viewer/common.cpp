#include "common.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "as3d/gfx.h"
#include "as3d/platform.h"

namespace viewer {

bool parseSceneArgs(int argc, char** argv, SceneArgs& args, std::vector<std::string>& positional) {
    for (int i = 0; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) {
            args.outPath = argv[++i];
        } else if (a == "--size" && i + 1 < argc) {
            std::string s = argv[++i];
            size_t x = s.find('x');
            if (x == std::string::npos) x = s.find('X');
            if (x == std::string::npos) {
                std::fprintf(stderr, "error: --size expects WxH, got '%s'\n", s.c_str());
                return false;
            }
            args.width = std::atoi(s.substr(0, x).c_str());
            args.height = std::atoi(s.substr(x + 1).c_str());
            if (args.width <= 0 || args.height <= 0) {
                std::fprintf(stderr, "error: invalid --size '%s'\n", s.c_str());
                return false;
            }
        } else if (a == "--window") {
            args.window = true;
        } else {
            positional.push_back(a);
        }
    }
    return true;
}

int runScene(const SceneArgs& args, const char* title, const SetupFn& setup, const DrawFn& draw) {
    if (args.window) {
        as3d::GraphicsConfig cfg;
        cfg.width = args.width;
        cfg.height = args.height;
        cfg.headless = false;
        cfg.vsync = true;
        cfg.title = title;
        auto ctx = as3d::createGraphicsContext(cfg);
        if (ctx) {
            ctx->makeCurrent();
            std::printf("window: %s\n", ctx->description().c_str());
            std::string error;
            if (!setup(error)) {
                std::fprintf(stderr, "error: scene setup failed: %s\n", error.c_str());
                return 1;
            }
            auto start = std::chrono::steady_clock::now();
            while (ctx->pumpEvents()) {
                float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
                as3d::setViewport(0, 0, ctx->width(), ctx->height());
                draw(ctx->width(), ctx->height(), t);
                ctx->swapBuffers();
            }
            return 0;
        }
        std::fprintf(stderr, "warning: --window requested but no display is available; "
                              "falling back to headless rendering\n");
    }

    if (args.outPath.empty()) {
        std::fprintf(stderr, "error: --out <path> is required (no --window, or no display)\n");
        return 1;
    }

    as3d::GraphicsConfig cfg;
    cfg.width = args.width;
    cfg.height = args.height;
    cfg.headless = true;
    cfg.title = title;
    auto ctx = as3d::createGraphicsContext(cfg);
    if (!ctx) {
        std::fprintf(stderr, "error: could not create a headless graphics context\n");
        return 1;
    }
    ctx->makeCurrent();
    std::printf("headless: %s\n", ctx->description().c_str());

    std::string error;
    if (!setup(error)) {
        std::fprintf(stderr, "error: scene setup failed: %s\n", error.c_str());
        return 1;
    }

    as3d::RenderTarget target;
    if (!target.create(args.width, args.height, 4)) {
        std::fprintf(stderr, "error: could not create a %dx%d render target\n", args.width, args.height);
        return 1;
    }
    target.bind();
    draw(args.width, args.height, 0.0f);

    as3d::Image image;
    if (!target.readPixels(image)) {
        std::fprintf(stderr, "error: RenderTarget::readPixels failed\n");
        return 1;
    }
    if (!as3d::writePng(args.outPath.c_str(), image)) {
        std::fprintf(stderr, "error: could not write '%s'\n", args.outPath.c_str());
        return 1;
    }
    std::printf("wrote %s (%dx%d)\n", args.outPath.c_str(), image.width, image.height);
    return 0;
}

std::string dataRoot() {
    const char* env = std::getenv("AS3D_DATA_ROOT");
    return env && *env ? env : AS3D_REPO_ROOT;
}

bool mountGameData(as3d::Vfs& vfs) {
    std::string dataDir = dataRoot() + "/third_party_local/original/data";
    bool any = false;
    for (const char* name : {"pak0.apk", "pak1.apk", "pak2.apk"}) {
        std::string path = dataDir + "/" + name;
        auto stream = as3d::openFileStream(path);
        if (!stream) {
            AS3D_WARN("mountGameData: cannot open %s", path.c_str());
            continue;
        }
        auto src = as3d::makePakSource(std::move(stream));
        if (!src) {
            AS3D_WARN("mountGameData: %s is not a valid pak", path.c_str());
            continue;
        }
        vfs.mount(std::move(src));
        any = true;
    }
    if (!any) {
        AS3D_ERROR("mountGameData: no paks could be mounted under %s (set AS3D_DATA_ROOT?)", dataDir.c_str());
    }
    return any;
}

as3d::Image makeCheckerboard(int width, int height, int cell, const as3d::u8 colorA[4],
                              const as3d::u8 colorB[4]) {
    as3d::Image img;
    img.width = width;
    img.height = height;
    img.hasAlpha = false;
    img.rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            bool a = ((x / cell) + (y / cell)) % 2 == 0;
            const as3d::u8* c = a ? colorA : colorB;
            size_t idx = (static_cast<size_t>(y) * width + x) * 4;
            img.rgba[idx + 0] = c[0];
            img.rgba[idx + 1] = c[1];
            img.rgba[idx + 2] = c[2];
            img.rgba[idx + 3] = c[3];
        }
    }
    return img;
}

} // namespace viewer
