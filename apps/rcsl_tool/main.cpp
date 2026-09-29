// rcsl_tool: command line front end for the RCSL VM (as3d::script) and the mock host in
// this directory. See docs/script-vm.md for usage and how to use `trace` to debug a
// mismatch against tools/ref/rcsl_vm.py.
//
//   rcsl_tool trace <file.scr> [--frames N] [--dt D] [--events SPEC] [--no-init] [--out FILE]
//   rcsl_tool check [<dir>]      (no dir: the scripts of the chosen game)
//   rcsl_tool info <file.scr>
//   rcsl_tool --list-games
// --game KEY (as3d, as2, gulf; default $AS3D_GAME, then as3d), --paks DIR and --data ROOT
// (default $AS3D_DATA_ROOT) choose the game whose files `check` reads (as3d/game_data.h).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "as3d/game_data.h"
#include "as3d/script.h"
#include "mock_host.h"
#include "runner.h"
#include "sha1.h"

namespace fs = std::filesystem;
using namespace as3d::script;
using namespace rcsl_tool;

namespace {

bool readFile(const std::string& path, std::vector<as3d::u8>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    if (n < 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(n));
    if (n > 0) f.read(reinterpret_cast<char*>(out.data()), n);
    return static_cast<bool>(f) || n == 0;
}

bool loadProgram(const std::string& path, ScriptProgram& out, std::string& error) {
    std::vector<as3d::u8> blob;
    if (!readFile(path, blob)) {
        error = "could not read file";
        return false;
    }
    return ScriptProgram::load(blob.data(), blob.size(), out, &error);
}

int cmdTrace(int argc, char** argv) {
    if (argc < 1) {
        std::cerr << "usage: rcsl_tool trace <file.scr> [--frames N] [--dt D] [--events SPEC] "
                     "[--no-init] [--out FILE]\n";
        return 2;
    }
    std::string file = argv[0];
    int frames = 600;
    float dt = 1.0f / 60.0f;
    std::string events; // default: none (matches tools/ref/rcsl_vm.py's `trace` default)
    bool init = true;
    std::string outPath;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string(); };
        if (a == "--frames") frames = std::atoi(next().c_str());
        else if (a == "--dt") dt = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--events") events = next();
        else if (a == "--no-init") init = false;
        else if (a == "--out") outPath = next();
    }

    ScriptProgram program;
    std::string error;
    if (!loadProgram(file, program, error)) {
        std::cerr << "rcsl_tool: " << file << ": " << error << "\n";
        return 1;
    }
    TextTraceSink sink;
    RunOutcome outcome = runStandard(program, frames, dt, events, init, &sink, nullptr, file.c_str());

    std::ostream* out = &std::cout;
    std::ofstream outFile;
    if (!outPath.empty()) {
        outFile.open(outPath, std::ios::binary);
        out = &outFile;
    }
    *out << sink.text();
    if (!outcome.ok) {
        std::cerr << "error: " << outcome.errorMessage << "\n";
        return 1;
    }
    return 0;
}

std::string g_game, g_paks, g_data;

std::string dataRoot() {
    if (!g_data.empty()) return g_data;
    const char* env = std::getenv("AS3D_DATA_ROOT");
    return env && *env ? env : ".";
}

int cmdCheck(int argc, char** argv) {
    fs::path root;
    if (argc >= 1) {
        root = argv[0];
    } else {
        as3d::GameData game;
        std::string error;
        if (!as3d::chooseGameData(dataRoot(), g_game, g_paks, &game, &error) || !game.hasExtracted) {
            std::cerr << "usage: rcsl_tool check [<dir>]\n";
            if (!error.empty()) std::cerr << "rcsl_tool: " << error << "\n";
            else std::cerr << "rcsl_tool: no extracted files for " << game.game->key << " (" << game.extractedDir << ")\n";
            return 2;
        }
        root = game.extractedDir + "/scripts";
    }
    std::vector<fs::path> files;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (it->is_regular_file()) {
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
            if (ext == ".scr") files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());

    int failures = 0;
    as3d::script::u64 totalInstr = 0, totalBuiltins = 0;
    for (const fs::path& p : files) {
        ScriptProgram program;
        std::string error;
        std::string rel = fs::relative(p, root).string();
        if (!loadProgram(p.string(), program, error)) {
            std::cout << rel << " LOAD-ERROR " << error << "\n";
            failures++;
            continue;
        }
        TextTraceSink sink;
        RunOutcome outcome =
            runStandard(program, 600, 1.0f / 60.0f, kStandardEvents, true, &sink, nullptr, rel.c_str());
        std::string hash = sha1Hex(sink.text());
        std::cout << rel << " sha1=" << hash << " instructions=" << outcome.instructions
                   << " builtins=" << outcome.builtinCalls;
        if (!outcome.ok) {
            std::cout << " ERROR " << outcome.errorMessage;
            failures++;
        }
        std::cout << "\n";
        totalInstr += outcome.instructions;
        totalBuiltins += outcome.builtinCalls;
    }
    std::cerr << "rcsl_tool check: " << files.size() << " scripts, " << totalInstr << " instructions, "
               << totalBuiltins << " builtin calls, " << failures << " failures\n";
    return failures ? 1 : 0;
}

int cmdInfo(int argc, char** argv) {
    if (argc < 1) {
        std::cerr << "usage: rcsl_tool info <file.scr>\n";
        return 2;
    }
    ScriptProgram program;
    std::string error;
    if (!loadProgram(argv[0], program, error)) {
        std::cerr << "rcsl_tool: " << argv[0] << ": " << error << "\n";
        return 1;
    }
    std::cout << "file: " << argv[0] << "\n";
    std::cout << "header1=" << program.header1() << " frameSlots=" << program.frameSlots()
               << " instructions=" << program.code().size() << "\n";
    std::cout << "cash=" << program.cash().size() << " defs=" << program.defs().size()
               << " funcs=" << program.funcs().size() << " data=" << program.data().size()
               << " strgBytes=" << program.strg().size() << "\n";
    static const EntryPoint kEps[] = {EntryPoint::Init, EntryPoint::Main, EntryPoint::Damage,
                                       EntryPoint::Touch, EntryPoint::Callback};
    for (EntryPoint ep : kEps) {
        std::cout << "  entry " << entryPointName(ep) << " = ";
        if (program.hasEntry(ep)) std::cout << program.entry(ep);
        else std::cout << "-";
        std::cout << "\n";
    }
    if (!program.defs().empty()) {
        std::cout << "DEFS:";
        for (const std::string& n : program.defs()) std::cout << " " << n;
        std::cout << "\n";
    }
    if (!program.funcs().empty()) {
        std::cout << "FUNC:";
        for (const std::string& n : program.funcs()) std::cout << " " << n;
        std::cout << "\n";
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<char*> args;
    args.push_back(argv[0]);
    bool listGames = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--game") && i + 1 < argc) g_game = argv[++i];
        else if (!std::strcmp(argv[i], "--paks") && i + 1 < argc) g_paks = argv[++i];
        else if (!std::strcmp(argv[i], "--data") && i + 1 < argc) g_data = argv[++i];
        else if (!std::strcmp(argv[i], "--list-games")) listGames = true;
        else args.push_back(argv[i]);
    }
    argc = static_cast<int>(args.size());
    argv = args.data();
    if (listGames) {
        std::fputs(as3d::describeGames(dataRoot()).c_str(), stdout);
        return 0;
    }
    if (!g_game.empty() || !g_paks.empty()) {
        as3d::GameData game;
        std::string error;
        if (!as3d::chooseGameData(dataRoot(), g_game, g_paks, &game, &error)) {
            std::cerr << "rcsl_tool: " << error << "\n";
            return 2;
        }
    }
    if (argc < 2) {
        std::cerr << "usage: rcsl_tool <trace|check|info> ...\n";
        return 2;
    }
    std::string cmd = argv[1];
    if (cmd == "trace") return cmdTrace(argc - 2, argv + 2);
    if (cmd == "check") return cmdCheck(argc - 2, argv + 2);
    if (cmd == "info") return cmdInfo(argc - 2, argv + 2);
    std::cerr << "unknown command '" << cmd << "'\n";
    return 2;
}
