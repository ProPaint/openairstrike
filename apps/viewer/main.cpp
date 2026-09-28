// as3d_viewer: command-line tool exercising the platform/render foundation
// (engine/include/as3d/{platform,gfx,math}.h). Commands self-register into
// registry.h/.cpp; this file only dispatches.
#include <cstdio>
#include <cstring>

#include "registry.h"

namespace {
void printUsageLine(const viewer::CommandInfo& c) { std::fprintf(stderr, "  %-10s %s\n", c.name, c.help); }

void printUsage(const char* argv0) {
    std::fprintf(stderr, "usage: %s <command> [options]\n\ncommands:\n", argv0);
    viewer::forEachCommand(printUsageLine);
}
} // namespace

int main(int argc, char** argv) {
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
    return cmd->run(argc - 2, argv + 2);
}
