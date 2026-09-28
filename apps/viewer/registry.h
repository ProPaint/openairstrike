// Self-registering command table for as3d_viewer. Adding a new command needs a new
// .cpp file that defines a run function and ends with an AS3D_VIEWER_COMMAND(...)
// line; nothing else in the viewer (not even main.cpp) needs to change.
#pragma once

namespace viewer {

using CommandFn = int (*)(int argc, char** argv);

struct CommandInfo {
    const char* name;
    const char* help; // one line, shown by `as3d_viewer help` / on a bad command name
    CommandFn run;
};

void registerCommand(const CommandInfo& info);
const CommandInfo* findCommand(const char* name);
// Calls `fn` once per registered command, in registration order.
void forEachCommand(void (*fn)(const CommandInfo&));

namespace detail {
struct AutoRegister {
    AutoRegister(const char* name, const char* help, CommandFn run) {
        registerCommand({name, help, run});
    }
};
} // namespace detail

} // namespace viewer

#define AS3D_VIEWER_CONCAT_INNER(a, b) a##b
#define AS3D_VIEWER_CONCAT(a, b) AS3D_VIEWER_CONCAT_INNER(a, b)

// Place at namespace scope in a command's .cpp file:
//   AS3D_VIEWER_COMMAND("triangle", "renders a triangle to a PNG", cmdTriangle);
#define AS3D_VIEWER_COMMAND(NAME, HELP, FN) \
    static ::viewer::detail::AutoRegister AS3D_VIEWER_CONCAT(as3d_viewer_autoreg_, __LINE__)(NAME, HELP, FN)
