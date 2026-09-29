// `as3d_viewer object <name> --out file.png [view options] [--night] [--list] [--markers]`:
// an object definition with its whole attachment hierarchy.
#include <cstdio>

#include "as3d/gfx.h"
#include "registry.h"
#include "viewer_scene.h"

namespace {
void printTree(const as3d::ObjectTree& tree) {
    const auto& nodes = tree.nodes();
    for (size_t i = 0; i < nodes.size(); i++) {
        const as3d::ObjectNode& n = nodes[i];
        int depth = 0;
        for (int p = n.parent; p >= 0; p = nodes[static_cast<size_t>(p)].parent) depth++;
        std::string mods;
        if (n.absolute) mods += " abs";
        if (n.nightOnly) mods += " night";
        if (!n.attachId.empty()) mods += " id=" + n.attachId;
        std::printf("%*s%s%s model='%s' tag='%s'%s  world=(%.2f %.2f %.2f)\n", depth * 2, "",
                    n.kind == as3d::NodeKind::ParticleSystem ? "[PS] " : "", n.name.c_str(),
                    n.def ? n.def->model.c_str() : "", n.tagName.c_str(), mods.c_str(), n.worldPos.x, n.worldPos.y,
                    n.worldPos.z);
    }
    for (const std::string& w : tree.warnings()) std::printf("warning: %s\n", w.c_str());
}
int run(int argc, char** argv) {
    viewer::ViewOpts opts;
    std::vector<std::string> pos;
    if (!viewer::parseViewOpts(argc, argv, opts, pos)) return 1;
    if (pos.empty() || (opts.out.empty() && !opts.list)) {
        std::fprintf(stderr, "usage: as3d_viewer object <object name> --out file.png [--size WxH] [--yaw d] [--pitch d] "
                             "[--dist f] [--wire] [--tags] [--night] [--list] [--markers]\n");
        return 1;
    }
    viewer::ViewerContext ctx;
    std::string err;
    if (!ctx.init(opts.width, opts.height, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    viewer::SceneDesc scene;
    as3d::ObjectTree tree;
    bool ok = viewer::buildObjectScene(ctx, pos[0], opts.night, scene, &tree, err);
    if (opts.list && ok) printTree(tree);
    if (!ok) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    if (opts.out.empty()) return 0;
    as3d::Image img;
    if (!ctx.renderToImage(opts.width, opts.height, img, scene, opts, false, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    if (!as3d::writePng(opts.out.c_str(), img)) { std::fprintf(stderr, "error: cannot write %s\n", opts.out.c_str()); return 1; }
    std::printf("wrote %s (%dx%d)\n", opts.out.c_str(), img.width, img.height);
    return 0;
}
} // namespace

AS3D_VIEWER_COMMAND("object", "render an object with its attachment hierarchy (--list prints the tree)", run);
