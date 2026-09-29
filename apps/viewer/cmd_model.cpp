// `as3d_viewer model <game path of .mdl> [--skin <tga>] --out file.png [--size WxH] [--yaw d]
// [--pitch d] [--dist f] [--wire] [--tags]`: one model, auto-framed, with a ground grid.
#include <cstdio>

#include "as3d/gfx.h"
#include "registry.h"
#include "viewer_scene.h"

namespace {
int run(int argc, char** argv) {
    viewer::ViewOpts opts;
    std::vector<std::string> pos;
    if (!viewer::parseViewOpts(argc, argv, opts, pos)) return 1;
    if (pos.empty() || opts.out.empty()) {
        std::fprintf(stderr, "usage: as3d_viewer model <game path .mdl> [--skin <tga>] --out file.png [--size WxH] "
                             "[--yaw deg] [--pitch deg] [--dist factor] [--wire] [--tags]\n");
        return 1;
    }
    viewer::ViewerContext ctx;
    std::string err;
    if (!ctx.init(opts.width, opts.height, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    viewer::SceneDesc scene;
    if (!viewer::buildModelScene(ctx, pos[0], opts.skin, scene, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    as3d::Image img;
    if (!ctx.renderToImage(opts.width, opts.height, img, scene, opts, false, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    if (!as3d::writePng(opts.out.c_str(), img)) { std::fprintf(stderr, "error: cannot write %s\n", opts.out.c_str()); return 1; }
    std::printf("wrote %s (%dx%d)\n", opts.out.c_str(), img.width, img.height);
    return 0;
}
} // namespace

AS3D_VIEWER_COMMAND("model", "render one .mdl model, auto-framed, to a PNG", run);
