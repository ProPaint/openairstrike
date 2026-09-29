// Renders the exact ASCII trace format of tools/ref/rcsl_vm.py (docs/spec/rcsl-vm.md,
// "Trace format"): one line per event, fields space-separated, 32-bit values as 8
// lower-case hex digits.
#include "as3d/script.h"

#include <cstdio>

namespace as3d::script {

namespace {

void appendHex8(std::string& out, u32 v) {
    char buf[9];
    std::snprintf(buf, sizeof buf, "%08x", v);
    out += buf;
}

void appendHex2(std::string& out, u8 v) {
    char buf[3];
    std::snprintf(buf, sizeof buf, "%02x", v);
    out += buf;
}

void appendU(std::string& out, unsigned long v) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "%lu", v);
    out += buf;
}

void appendI(std::string& out, long v) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "%ld", v);
    out += buf;
}

} // namespace

void TextTraceSink::dispatchStart(u32 frame, EntryPoint entry) {
    buf_ += "E ";
    appendU(buf_, frame);
    buf_ += ' ';
    buf_ += entryPointName(entry);
    buf_ += '\n';
}

void TextTraceSink::instruction(u32 frame, EntryPoint entry, int depth, u32 pc, u8 op,
                                 bool hasValue, u32 value) {
    buf_ += "I ";
    appendU(buf_, frame);
    buf_ += ' ';
    buf_ += entryPointName(entry);
    buf_ += ' ';
    appendI(buf_, depth);
    buf_ += ' ';
    appendU(buf_, pc);
    buf_ += ' ';
    appendHex2(buf_, op);
    buf_ += ' ';
    if (hasValue) {
        appendHex8(buf_, value);
    } else {
        buf_ += '-';
    }
    buf_ += '\n';
}

void TextTraceSink::builtinCall(u32 frame, EntryPoint entry, u32 pc, const char* name,
                                 const u32* args, int argCount, u32 retBits) {
    buf_ += "B ";
    appendU(buf_, frame);
    buf_ += ' ';
    buf_ += entryPointName(entry);
    buf_ += ' ';
    appendU(buf_, pc);
    buf_ += ' ';
    buf_ += name;
    for (int i = 0; i < argCount; ++i) {
        buf_ += ' ';
        appendHex8(buf_, args[i]);
    }
    buf_ += " -> ";
    appendHex8(buf_, retBits);
    buf_ += '\n';
}

void TextTraceSink::dispatchEnd(u32 frame, EntryPoint entry, int status, int stackDelta) {
    buf_ += "R ";
    appendU(buf_, frame);
    buf_ += ' ';
    buf_ += entryPointName(entry);
    buf_ += ' ';
    appendI(buf_, status);
    buf_ += ' ';
    appendI(buf_, stackDelta);
    buf_ += '\n';
}

void TextTraceSink::damage(u32 frame, bool skipped, u32 amountBits) {
    buf_ += "D ";
    appendU(buf_, frame);
    buf_ += ' ';
    if (skipped) {
        buf_ += "skipped";
    } else {
        appendHex8(buf_, amountBits);
    }
    buf_ += '\n';
}

void TextTraceSink::vmError(u32 frame, EntryPoint entry, u32 pc, const char* message) {
    buf_ += "X ";
    appendU(buf_, frame);
    buf_ += ' ';
    buf_ += entryPointName(entry);
    buf_ += ' ';
    appendU(buf_, pc);
    buf_ += ' ';
    buf_ += message;
    buf_ += '\n';
}

} // namespace as3d::script
