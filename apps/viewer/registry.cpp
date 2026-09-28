#include "registry.h"

#include <cstring>
#include <vector>

namespace viewer {

namespace {
// Function-local static: guarantees the vector exists before the first static
// initializer in any command .cpp file runs, regardless of translation-unit order.
std::vector<CommandInfo>& table() {
    static std::vector<CommandInfo> instance;
    return instance;
}
} // namespace

void registerCommand(const CommandInfo& info) { table().push_back(info); }

const CommandInfo* findCommand(const char* name) {
    for (const CommandInfo& c : table()) {
        if (std::strcmp(c.name, name) == 0) return &c;
    }
    return nullptr;
}

void forEachCommand(void (*fn)(const CommandInfo&)) {
    for (const CommandInfo& c : table()) fn(c);
}

} // namespace viewer
