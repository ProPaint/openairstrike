// Shared plumbing for viewer commands: argument parsing, the headless-PNG /
// windowed-loop scene runner, game data mounting, and a synthetic checkerboard used
// as a background so alpha blending is visible without a second asset.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/game_data.h"
#include "as3d/image.h"
#include "as3d/vfs.h"

namespace viewer {

struct SceneArgs {
    int width = 640;
    int height = 480;
    std::string outPath;
    bool window = false;
};

// Parses --out <path>, --size WxH and --window out of argv[0..argc); everything else
// is appended to `positional` in order (e.g. the game path for `texture`). Returns
// false (after printing to stderr) on a malformed flag value.
bool parseSceneArgs(int argc, char** argv, SceneArgs& args, std::vector<std::string>& positional);

// Issues the GL draw calls for one frame into whatever framebuffer is currently bound.
using DrawFn = std::function<void(int width, int height, float timeSeconds)>;
// Runs once, after a context is current, to build shaders/buffers/textures.
using SetupFn = std::function<bool(std::string& error)>;

// Headless: creates an EGL context + 4x-MSAA RenderTarget, runs `setup` then one
// `draw`, and writes the result to args.outPath (required). Windowed (args.window):
// creates an SDL window instead and loops `draw` until it is closed, falling back to
// the headless path if no display is available. Returns a process exit code.
int runScene(const SceneArgs& args, const char* title, const SetupFn& setup, const DrawFn& draw);

// --data, else $AS3D_DATA_ROOT, or the repository root if unset -- same rule as
// apps/tests/test_data.h, reimplemented here so the viewer does not pull in a
// test-only header.
std::string dataRoot();

// The game the commands work on: --game KEY, --paks DIR and --data ROOT given before or after
// the command name (main.cpp strips them), else $AS3D_GAME, then as3d if present. Null after
// printing the reason to stderr when there is none.
void setGameSelection(const std::string& key, const std::string& paksDir, const std::string& dataRoot);
const as3d::GameData* selectedGame();

// Mounts the selected game's paks (in the profile's order, so later ones win on a name
// clash, per docs/spec/pak.md), or its extracted directory if it has no paks, into `vfs`.
// Returns false if nothing could be mounted.
bool mountGameData(as3d::Vfs& vfs);

// A cell x cell checkerboard of colorA/colorB, opaque, used as a background.
as3d::Image makeCheckerboard(int width, int height, int cell, const as3d::u8 colorA[4],
                              const as3d::u8 colorB[4]);

} // namespace viewer
