// as3d_viewer: command-line tool exercising the platform/render foundation
// (engine/include/as3d/{platform,gfx,math}.h). Commands self-register into
// registry.h/.cpp; this file only dispatches.
//
// --game KEY (as3d, as2, gulf), --paks DIR, --data ROOT and --list-games are the viewer's
// own and may stand before or after the command name; they choose the data the commands
// mount (common.h, as3d/game_data.h).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "common.h"
#include "registry.h"

namespace {
void printUsageLine(const viewer::CommandInfo& c) { std::fprintf(stderr, "  %-10s %s\n", c.name, c.help); }

void printUsage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s [--game as3d|as2|gulf] [--paks DIR] [--data ROOT] <command> [options]\n"
                 "       %s --list-games\n\ncommands:\n",
                 argv0, argv0);
    viewer::forEachCommand(printUsageLine);
}
} // namespace

int main(int argc, char** argv) {
    std::string game, paks, data;
    bool listGames = false;
    std::vector<char*> args;
    args.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--game") && i + 1 < argc) game = argv[++i];
        else if (!std::strcmp(argv[i], "--paks") && i + 1 < argc) paks = argv[++i];
        else if (!std::strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
        else if (!std::strcmp(argv[i], "--list-games")) listGames = true;
        else args.push_back(argv[i]);
    }
    argc = static_cast<int>(args.size());
    argv = args.data();
    viewer::setGameSelection(game, paks, data);
    if (listGames) {
        std::fputs(as3d::describeGames(viewer::dataRoot()).c_str(), stdout);
        return 0;
    }
    // A game named on the command line must exist, whatever the command.
    if (!game.empty() && !viewer::selectedGame()) return 2;

    if (argc < 2 || std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0 ||
        std::strcmp(argv[1], "help") == 0) {
        printUsage(argv[0]);
        return argc < 2 ? 1 : 0;
    }

    const viewer::CommandInfo* cmd = viewer::findCommand(argv[1]);
    if (!cmd) {
        std::fprintf(stderr, "error: unknown command '%s'\n\n", argv[1]);
        printUsage(argv[0]);
        return 1;
    }
    if (!std::strcmp(argv[1], "info")) {
        // The data the other commands would read, for bug reports.
        if (const as3d::GameData* g = viewer::selectedGame())
            std::printf("game: %s (%s %s), %zu paks in %s, extracted: %s\n", g->game->key, g->game->title,
                        g->game->version, g->paks.size(), g->dataDir.c_str(),
                        g->hasExtracted ? g->extractedDir.c_str() : "none");
    }
    return cmd->run(argc - 2, argv + 2);
}
