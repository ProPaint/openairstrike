// Shared float/bit helpers for the RCSL VM, private to engine/src/script. These
// implement the exact bit-for-bit semantics of docs/spec/rcsl-vm.md, "Values and
// arithmetic", ported from tools/ref/rcsl_vm.py so the C++ VM matches the reference
// interpreter exactly. Built with -ffp-contract=off and no -ffast-math (see module.cmake)
// so the compiler cannot change these results.
#pragma once

#include <cmath>
#include <cstring>

#include "as3d/core.h"
#include "as3d/script.h"

namespace as3d::script::detail {

// Operand roles per opcode, mirroring rcsl_disasm.py's OPCODES[op][1] (docs/spec/
// rcsl-opcodes-v0.md, opcode table). Used both by the loader (to decide which operands
// need a static bounds check) and by the interpreter (to decide whether an indirection
// on an operand the handler does not use should be a soft, error-free read).
enum class Role : u8 { None, Read, Write, Ptr, Field, Rel, Call, Raw };
struct RoleSet {
    Role a, b, c;
};

// Returns false for an opcode outside 0x00..0x1E (never valid; the loader rejects such
// programs, so the interpreter never actually sees one).
inline bool roleSetFor(u8 op, RoleSet& out) {
    switch (op) {
        case OP_END:
            out = {Role::None, Role::None, Role::None};
            return true;
        case OP_MUL:
        case OP_DIV:
        case OP_ADD:
        case OP_SUB:
        case OP_BAND:
        case OP_BOR:
        case OP_LOR:
        case OP_LAND:
        case OP_EQ:
        case OP_NE:
        case OP_GT:
        case OP_LT:
        case OP_GE:
        case OP_LE:
            out = {Role::Read, Role::Read, Role::Write};
            return true;
        case OP_NOT:
        case OP_NEG:
            out = {Role::Read, Role::None, Role::Write};
            return true;
        case OP_MOV:
            out = {Role::Write, Role::Read, Role::None};
            return true;
        case OP_LEA:
            out = {Role::Ptr, Role::Field, Role::Write};
            return true;
        case OP_PUSH:
            out = {Role::Read, Role::None, Role::None};
            return true;
        case OP_POP:
            out = {Role::None, Role::Write, Role::None};
            return true;
        case OP_ALLOC:
            out = {Role::Raw, Role::None, Role::Write};
            return true;
        case OP_NOP16:
        case OP_NOP17:
            out = {Role::None, Role::None, Role::None};
            return true;
        case OP_RET:
            out = {Role::Read, Role::None, Role::None};
            return true;
        case OP_JNZ:
        case OP_JZ:
            out = {Role::Read, Role::Rel, Role::None};
            return true;
        case OP_JMP:
            out = {Role::Rel, Role::None, Role::None};
            return true;
        case OP_CALL:
        case OP_LCALL:
            out = {Role::Call, Role::Write, Role::None};
            return true;
        case OP_TMO:
            out = {Role::Read, Role::None, Role::None};
            return true;
        default:
            return false;
    }
}

inline u32 floatToBits(float f) {
    u32 b;
    std::memcpy(&b, &f, 4);
    return b;
}

inline float bitsToFloat(u32 b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

// x87 fld/fstp of a single: a signalling NaN becomes quiet; everything else is copied
// unchanged (rcsl-vm.md: "Every copy made through the FPU ... turns a signalling NaN
// into a quiet one (bit 22 set) and preserves every other pattern").
inline u32 quiet(u32 b) {
    bool isNanBits = (b & 0x7F800000u) == 0x7F800000u && (b & 0x007FFFFFu) != 0;
    bool isSignalling = isNanBits && !(b & 0x00400000u);
    return isSignalling ? (b | 0x00400000u) : b;
}

inline bool isNanBits(u32 b) { return (b & 0x7F800000u) == 0x7F800000u && (b & 0x007FFFFFu) != 0; }

// Float bits -> int32 with truncation toward zero; NaN, infinities and out-of-range
// values give INT32_MIN (0x80000000), matching _ftol2 (0x440870).
inline i32 ftol(u32 bits) {
    float f = bitsToFloat(bits);
    double x = static_cast<double>(f);
    if (std::isnan(x) || std::isinf(x) || !(x > -2147483649.0 && x < 2147483648.0)) {
        return static_cast<i32>(0x80000000u);
    }
    return static_cast<i32>(x);
}

inline u32 itof(i32 i) { return floatToBits(static_cast<float>(i)); }

constexpr u32 kIndefinite = 0xFFC00000u; // x87 default NaN for invalid operations

// MUL/DIV/ADD/SUB on float bits the way the x87 code produces them (rcsl-vm.md). `op` is
// one of the Opcode constants OP_MUL/OP_DIV/OP_ADD/OP_SUB.
inline u32 farith(u8 op, u32 ab, u32 bb) {
    bool na = isNanBits(ab), nb = isNanBits(bb);
    if (na || nb) {
        if (na && nb) {
            u32 sigA = ab & 0x7FFFFFu, sigB = bb & 0x7FFFFFu;
            return quiet(sigA >= sigB ? ab : bb);
        }
        return quiet(na ? ab : bb);
    }
    double x = static_cast<double>(bitsToFloat(ab));
    double y = static_cast<double>(bitsToFloat(bb));
    double r;
    switch (op) {
        case 0x01: r = x * y; break;
        case 0x02: r = x / y; break;
        case 0x03: r = x + y; break;
        default: r = x - y; break; // 0x04
    }
    if (std::isnan(r)) return kIndefinite;
    return floatToBits(static_cast<float>(r));
}

} // namespace as3d::script::detail
