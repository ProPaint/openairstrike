// RCSL container loader: parses and statically validates a compiled script. Follows
// docs/spec/rcsl-container.md exactly; ported from tools/rcsl_disasm.py's `parse` and
// `check_bounds`. Never crashes or reads out of bounds on malformed input: every access
// goes through as3d::ByteReader or an explicit bounds check.
#include "as3d/script.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "as3d/core.h"
#include "script_internal.h"

namespace as3d::script {

namespace {

using detail::Role;
using detail::RoleSet;
using detail::roleSetFor;

constexpr u32 kMagic = 0x4C534352u; // "RCSL"
constexpr size_t kHeaderSize = 0x38;
constexpr size_t kInstrSize = 14;

struct Payload {
    const u8* data = nullptr;
    size_t len = 0;
    bool present = false;
};

// Reads `count` NUL-terminated names (with an optional leading kind byte) from a
// payload, matching rcsl_disasm.py's `_read_names`. Fails if any name is malformed or if
// the payload has leftover bytes after `count` names.
template <typename Emit>
bool readNames(const Payload& p, u32 count, bool withKind, Emit emit, std::string* error) {
    size_t o = 0;
    for (u32 i = 0; i < count; ++i) {
        u8 kind = 0;
        if (withKind) {
            if (o >= p.len) { if (error) *error = "name table: truncated"; return false; }
            kind = p.data[o++];
        }
        if (o >= p.len) { if (error) *error = "name table: truncated"; return false; }
        u8 n = p.data[o++];
        if (n < 1 || o + n > p.len) { if (error) *error = "name table: bad length"; return false; }
        const u8* s = p.data + o;
        if (s[n - 1] != 0) { if (error) *error = "name table: not NUL-terminated"; return false; }
        for (u8 k = 0; k + 1 < n; ++k) {
            if (s[k] == 0) { if (error) *error = "name table: embedded NUL"; return false; }
        }
        emit(kind, std::string(reinterpret_cast<const char*>(s), n - 1));
        o += n;
    }
    if (o != p.len) { if (error) *error = "name table: trailing bytes"; return false; }
    return true;
}

} // namespace

const char* entryPointName(EntryPoint ep) {
    switch (ep) {
        case EntryPoint::Init: return "init";
        case EntryPoint::Main: return "main";
        case EntryPoint::Damage: return "damage";
        case EntryPoint::Touch: return "touch";
        case EntryPoint::Callback: return "callback";
    }
    return "?";
}

bool ScriptProgram::isStringStart(u32 offset) const {
    return std::binary_search(stringStarts_.begin(), stringStarts_.end(), offset);
}

const char* ScriptProgram::stringAt(u32 offset) const {
    if (!isStringStart(offset)) return nullptr;
    return reinterpret_cast<const char*>(strg_.data()) + offset;
}

void ScriptProgram::notifyPrecache(IScriptHost& host) const {
    for (const CashEntry& e : cash_) host.precache(e.kind, e.name.c_str());
}

bool ScriptProgram::load(const u8* data, size_t size, ScriptProgram& out, std::string* error) {
    out = ScriptProgram{};
    auto fail = [&](const char* msg) {
        if (error) *error = msg;
        return false;
    };
    if (!data || size < kHeaderSize) return fail("file shorter than header");

    ByteReader r(data, size);
    u32 header[14];
    for (u32& h : header) h = r.readU32();
    if (r.failed()) return fail("truncated header");
    if (header[0] != kMagic) return fail("bad magic");

    const u32 cashCount = header[2], defsCount = header[3], funcCount = header[4];
    const u32 dataCount = header[5], frameSlots = header[6], strgSize = header[7];
    const u32 codeCount = header[8];

    Payload cash, defs, func, dataSec, strg, code;
    while (r.pos() < r.size()) {
        if (r.remaining() < 8) return fail("truncated section header");
        u8 tagBytes[4];
        r.readBytes(tagBytes, 4);
        u32 len = r.readU32();
        if (r.failed() || len > r.remaining()) return fail("section overruns file");
        size_t off = r.pos();
        r.skip(len);
        Payload p{data + off, len, true};
        if (std::memcmp(tagBytes, "CASH", 4) == 0) {
            if (cash.present) return fail("duplicate CASH section");
            cash = p;
        } else if (std::memcmp(tagBytes, "DEFS", 4) == 0) {
            if (defs.present) return fail("duplicate DEFS section");
            defs = p;
        } else if (std::memcmp(tagBytes, "FUNC", 4) == 0) {
            if (func.present) return fail("duplicate FUNC section");
            func = p;
        } else if (std::memcmp(tagBytes, "DATA", 4) == 0) {
            if (dataSec.present) return fail("duplicate DATA section");
            dataSec = p;
        } else if (std::memcmp(tagBytes, "STRG", 4) == 0) {
            if (strg.present) return fail("duplicate STRG section");
            strg = p;
        } else if (std::memcmp(tagBytes, "CODE", 4) == 0) {
            if (code.present) return fail("duplicate CODE section");
            code = p;
        }
        // Unknown tags are skipped (already advanced past by r.skip(len)).
    }

    if (cashCount > 0 && !cash.present) return fail("CASH count without CASH section");
    if (defsCount > 0 && !defs.present) return fail("DEFS count without DEFS section");
    if (funcCount > 0 && !func.present) return fail("FUNC count without FUNC section");
    if (!code.present) return fail("missing CODE section");
    if (frameSlots < 16) return fail("frame size below 16");
    if (frameSlots > kMaxFrameSlots) return fail("frame size too large");

    if (cash.present) {
        if (!readNames(
                cash, cashCount, true,
                [&](u8 kind, std::string name) { out.cash_.push_back({kind, std::move(name)}); },
                error))
            return false;
    }
    if (defs.present) {
        if (!readNames(
                defs, defsCount, false,
                [&](u8, std::string name) { out.defs_.push_back(std::move(name)); }, error))
            return false;
    }
    if (func.present) {
        if (!readNames(
                func, funcCount, false,
                [&](u8, std::string name) { out.funcs_.push_back(std::move(name)); }, error))
            return false;
    }

    if (dataSec.present) {
        if (dataSec.len != 8u * dataCount) return fail("DATA length != 8 * count");
        ByteReader dr(dataSec.data, dataSec.len);
        for (u32 i = 0; i < dataCount; ++i) {
            u16 kind = dr.readU16();
            i16 slot = static_cast<i16>(dr.readU16());
            u32 value = dr.readU32();
            if (kind == 2 || kind == 3) {
                if (slot < 0 || static_cast<u32>(slot) >= frameSlots)
                    return fail("DATA slot out of range");
                if (kind == 3 && (value >= frameSlots)) return fail("DATA ref target out of range");
            }
            out.data_.push_back({kind, slot, value});
        }
    } else if (dataCount > 0) {
        return fail("DATA count without DATA section");
    }

    if (strg.present) {
        if (strg.len != strgSize) return fail("STRG length != header size");
        out.strg_.assign(strg.data, strg.data + strg.len);
        if (!out.strg_.empty() && out.strg_.back() != 0) return fail("STRG not NUL-terminated");
    } else if (strgSize > 0) {
        return fail("STRG size without STRG section");
    }
    if (!out.strg_.empty()) {
        out.stringStarts_.push_back(0);
        for (size_t i = 0; i + 1 < out.strg_.size(); ++i) {
            if (out.strg_[i] == 0) out.stringStarts_.push_back(static_cast<u32>(i + 1));
        }
    }

    if (code.len != kInstrSize * static_cast<size_t>(codeCount))
        return fail("CODE length != 14 * instruction count");
    {
        ByteReader cr(code.data, code.len);
        out.code_.reserve(codeCount);
        for (u32 i = 0; i < codeCount; ++i) {
            Instruction ins;
            ins.op = cr.readU8();
            ins.mode = cr.readU8();
            ins.a = cr.readI32();
            ins.b = cr.readI32();
            ins.c = cr.readI32();
            out.code_.push_back(ins);
        }
    }

    for (int k = 0; k < kNumEntryPoints; ++k) {
        u32 e = header[9 + k];
        if (e != kNoEntry && e >= codeCount) return fail("entry point out of range");
        out.entries_[k] = e;
    }
    out.header1_ = header[1];
    out.frameSlots_ = frameSlots;

    // Static operand validation (rcsl-opcodes-v0.md, opcode table + container spec's
    // operand encoding exceptions), matching rcsl_disasm.py's check_bounds: only
    // operands actually used by the handler (role != None) are checked.
    const u32 n = codeCount;
    for (u32 i = 0; i < n; ++i) {
        const Instruction& ins = out.code_[i];
        RoleSet roles;
        if (!roleSetFor(ins.op, roles)) return fail("unknown opcode");
        auto checkGeneric = [&](Role role, i32 v, bool allowImmediate, u8 immBit) -> bool {
            if (role == Role::None) return true;
            if (allowImmediate && (ins.mode & immBit)) return true; // immediate: no bound to check
            if (v < 0) {
                if (static_cast<u32>(-v - 1) >= out.defs_.size()) return false;
            } else {
                if (static_cast<u32>(v) >= frameSlots) return false;
            }
            return true;
        };
        // Operand A.
        if (roles.a == Role::Rel) {
            std::int64_t target = static_cast<std::int64_t>(i) + ins.a;
            if (target < 0 || target >= n) return fail("jump target out of range");
        } else if (roles.a == Role::Call) {
            if (ins.a < 0) {
                if (static_cast<u32>(-ins.a - 1) >= out.funcs_.size())
                    return fail("FUNC index out of range");
            } else if (static_cast<u32>(ins.a) >= n) {
                return fail("call target out of range");
            }
        } else if (roles.a == Role::Read || roles.a == Role::Write || roles.a == Role::Ptr) {
            if (!checkGeneric(roles.a, ins.a, true, M_IMM1)) return fail("operand A out of range");
        }
        // Operand B.
        if (roles.b == Role::Rel) {
            std::int64_t target = static_cast<std::int64_t>(i) + ins.b;
            if (target < 0 || target >= n) return fail("jump target out of range");
        } else if (roles.b == Role::Read || roles.b == Role::Write) {
            if (!checkGeneric(roles.b, ins.b, true, M_IMM2)) return fail("operand B out of range");
        }
        // Operand C (never immediate, never rel/call/field/raw).
        if (roles.c == Role::Read || roles.c == Role::Write) {
            if (!checkGeneric(roles.c, ins.c, false, 0)) return fail("operand C out of range");
        }
        if (ins.op == OP_LEA && ins.b < 0) return fail("LEA with negative field index");
    }

    return true;
}

} // namespace as3d::script
