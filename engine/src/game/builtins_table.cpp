// The game's single builtin table: the concatenation of every family's table. Names not
// listed bind to the VM's counted auto-stub (arity from docs/spec/rcsl-builtins-table.md).
#include <cstring>
#include <vector>

#include "builtins_common.h"

namespace as3d {

namespace {
const std::vector<script::BuiltinDesc>& table() {
    static const std::vector<script::BuiltinDesc> t = [] {
        std::vector<script::BuiltinDesc> v;
        const builtins::Family families[] = {
            builtins::mathBuiltins(),   builtins::vectorBuiltins(), builtins::entityBuiltins(),
            builtins::movementBuiltins(), builtins::combatBuiltins(), builtins::playerBuiltins(),
            builtins::effectsBuiltins(),
        };
        for (const builtins::Family& f : families) {
            for (size_t i = 0; i < f.count; ++i) v.push_back(f.table[i]);
        }
        return v;
    }();
    return t;
}
} // namespace

const script::BuiltinDesc* findGameBuiltin(const char* name) {
    if (!name) return nullptr;
    for (const script::BuiltinDesc& d : table()) {
        if (std::strcmp(d.name, name) == 0) return &d;
    }
    return nullptr;
}

size_t gameBuiltinCount() { return table().size(); }
const script::BuiltinDesc& gameBuiltinAt(size_t index) { return table()[index]; }

} // namespace as3d
