// Shared helpers for world_test.cpp, builtins_test.cpp and collision_test.cpp: an
// in-memory file source, a tiny RCSL assembler (adapted from script_test.cpp's
// ScrBuilder) and a rig that builds a World over synthetic definitions and scripts.
#pragma once

#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "as3d/core.h"
#include "as3d/defs.h"
#include "as3d/script.h"
#include "as3d/vfs.h"
#include "as3d/world.h"

namespace worldtest {

using namespace as3d;
using namespace as3d::script;

inline u32 fb(float f) {
    u32 b;
    std::memcpy(&b, &f, 4);
    return b;
}
inline float bf(u32 b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}
inline i32 imm(float f) { return static_cast<i32>(fb(f)); }

// ---------------------------------------------------------------------------------------
// In-memory file source.
// ---------------------------------------------------------------------------------------
class MemSource final : public IFileSource {
public:
    std::map<std::string, Blob> files;
    void add(const std::string& path, const std::string& text) { files[normalizePath(path)] = Blob(text.begin(), text.end()); }
    void add(const std::string& path, const Blob& b) { files[normalizePath(path)] = b; }
    bool exists(const std::string& path) override { return files.count(path) != 0; }
    bool read(const std::string& path, Blob& out) override {
        auto it = files.find(path);
        if (it == files.end()) return false;
        out = it->second;
        return true;
    }
    void list(std::vector<std::string>& out) override {
        for (auto& kv : files) out.push_back(kv.first);
    }
};

// ---------------------------------------------------------------------------------------
// Assembler (container layout of rcsl-container.md; same as script_test.cpp's).
// ---------------------------------------------------------------------------------------
class Asm {
public:
    u32 frameSlots = 32;

    // Globals and builtins are added to DEFS/FUNC on first use.
    i32 g(const std::string& name) { return -(index(defs_, name) + 1); }
    i32 fn(const std::string& name) { return -(index(funcs_, name) + 1); }
    u32 str(const std::string& s) {
        u32 off = static_cast<u32>(strg_.size());
        strg_.insert(strg_.end(), s.begin(), s.end());
        strg_.push_back(0);
        return off;
    }
    void data(u16 kind, i16 slot, u32 value) { data_.push_back({kind, slot, value}); }
    int emit(u8 op, u8 mode = 0, i32 a = 0, i32 b = 0, i32 c = 0) {
        code_.push_back({op, mode, a, b, c});
        return static_cast<int>(code_.size()) - 1;
    }
    int here() const { return static_cast<int>(code_.size()); }
    void entry(EntryPoint ep) { entries_[static_cast<int>(ep)] = static_cast<u32>(here()); }
    void patchB(int at, i32 b) { code_[static_cast<size_t>(at)].b = b; }
    void patchA(int at, i32 a) { code_[static_cast<size_t>(at)].a = a; }

    // --- macros ---------------------------------------------------------------------
    void movImm(int slot, float v) { emit(OP_MOV, M_IMM2, slot, imm(v)); }
    void movRaw(int slot, u32 bits) { emit(OP_MOV, M_IMM2, slot, static_cast<i32>(bits)); }
    void movStr(int slot, const std::string& s) { movRaw(slot, str(s)); }
    void movGlobal(int slot, const std::string& name) { emit(OP_MOV, 0, slot, g(name)); }
    void movSlot(int dst, int src) { emit(OP_MOV, 0, dst, src); }
    // dst = &global[k] (global holds a pointer: self, player, other, camera)
    void leaGlobal(int dst, const std::string& global, int k) { emit(OP_LEA, 0, g(global), k, dst); }
    void leaSlot(int dst, int src, int k) { emit(OP_LEA, 0, src, k, dst); }
    void storeImm(int ptrSlot, float v) { emit(OP_MOV, M_IND1 | M_IMM2, ptrSlot, imm(v)); }
    void storeSlot(int ptrSlot, int src) { emit(OP_MOV, M_IND1, ptrSlot, src); }
    void load(int dst, int ptrSlot) { emit(OP_MOV, M_IND2, dst, ptrSlot); }
    // self[k] = v / self[k] = slot / slot = self[k], using t15 as scratch.
    void setSelf(int k, float v) { leaGlobal(15, "self", k); storeImm(15, v); }
    void setSelfSlot(int k, int src) { leaGlobal(15, "self", k); storeSlot(15, src); }
    void getSelf(int dst, int k) { leaGlobal(15, "self", k); load(dst, 15); }
    // self[k] += v
    void addSelf(int k, float v) {
        leaGlobal(15, "self", k);
        emit(OP_ADD, M_IND1 | M_IMM2, 15, imm(v), 14);
        storeSlot(15, 14);
    }
    void call(const std::string& name, int dst = 14) { emit(OP_CALL, 0, fn(name), dst); }
    void lcall(const std::string& name, int dst = 14) { emit(OP_LCALL, 0, fn(name), dst); }
    void tmo(float s) { emit(OP_TMO, M_IMM1, imm(s)); }
    void end() { emit(OP_END); }

    std::vector<u8> build() const {
        std::vector<u8> out;
        auto u32le = [](std::vector<u8>& v, u32 x) {
            for (int i = 0; i < 4; ++i) v.push_back(static_cast<u8>(x >> (8 * i)));
        };
        u32le(out, 0x4C534352u);
        u32le(out, 16);
        u32le(out, 0);
        u32le(out, static_cast<u32>(defs_.size()));
        u32le(out, static_cast<u32>(funcs_.size()));
        u32le(out, static_cast<u32>(data_.size()));
        u32le(out, frameSlots);
        u32le(out, static_cast<u32>(strg_.size()));
        u32le(out, static_cast<u32>(code_.size()));
        for (int k = 0; k < 5; ++k) u32le(out, entries_[k]);
        auto section = [&](const char* tag, const std::vector<u8>& payload) {
            out.insert(out.end(), tag, tag + 4);
            u32le(out, static_cast<u32>(payload.size()));
            out.insert(out.end(), payload.begin(), payload.end());
        };
        auto names = [](const std::vector<std::string>& v) {
            std::vector<u8> p;
            for (const auto& n : v) {
                p.push_back(static_cast<u8>(n.size() + 1));
                p.insert(p.end(), n.begin(), n.end());
                p.push_back(0);
            }
            return p;
        };
        if (!defs_.empty()) section("DEFS", names(defs_));
        if (!funcs_.empty()) section("FUNC", names(funcs_));
        std::vector<u8> d;
        for (const auto& e : data_) {
            d.push_back(static_cast<u8>(e.kind));
            d.push_back(static_cast<u8>(e.kind >> 8));
            u16 s = static_cast<u16>(e.slot);
            d.push_back(static_cast<u8>(s));
            d.push_back(static_cast<u8>(s >> 8));
            u32le(d, e.value);
        }
        section("DATA", d);
        if (!strg_.empty()) section("STRG", strg_);
        std::vector<u8> c;
        for (const auto& i : code_) {
            c.push_back(i.op);
            c.push_back(i.mode);
            u32le(c, static_cast<u32>(i.a));
            u32le(c, static_cast<u32>(i.b));
            u32le(c, static_cast<u32>(i.c));
        }
        section("CODE", c);
        return out;
    }

private:
    static int index(std::vector<std::string>& v, const std::string& n) {
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i] == n) return static_cast<int>(i);
        }
        v.push_back(n);
        return static_cast<int>(v.size()) - 1;
    }
    struct D { u16 kind; i16 slot; u32 value; };
    struct I { u8 op, mode; i32 a, b, c; };
    u32 entries_[5] = {kNoEntry, kNoEntry, kNoEntry, kNoEntry, kNoEntry};
    std::vector<std::string> defs_, funcs_;
    std::vector<D> data_;
    std::vector<u8> strg_;
    std::vector<I> code_;
};

// ---------------------------------------------------------------------------------------
// World rig over synthetic data.
// ---------------------------------------------------------------------------------------

// Objects always available in the rig (the player helicopters come first so their
// definitions win; Rig::playerScript gives p_comanche a script).
inline const char* kBaseObjects = R"(
p_apache {
	player
	flag	FL_TEMPORARY
	health	400
}
score_num {
	flag	FL_TEMPORARY
}
)";

// A minimal valid .mdl (docs/spec/mdl.md): one vertex/uv/face/normal, the given box
// and tags.
struct TestTag {
    std::string name;
    Vec3 pos;
};
inline Blob makeMdl(Vec3 mn, Vec3 mx, const std::vector<TestTag>& tags = {}) {
    Blob v;
    auto u32le = [&](u32 x) {
        for (int i = 0; i < 4; ++i) v.push_back(static_cast<u8>(x >> (8 * i)));
    };
    auto f32 = [&](float f) { u32le(fb(f)); };
    auto fixed = [&](const std::string& s, size_t n) {
        for (size_t i = 0; i < n; ++i) v.push_back(i < s.size() ? static_cast<u8>(s[i]) : 0);
    };
    v.push_back('M'); v.push_back('D'); v.push_back('L'); v.push_back('!');
    u32le(2);
    u32le(0);
    fixed("t.tga", 0x40);
    u32le(1); u32le(1); u32le(1); u32le(1);
    u32le(static_cast<u32>(tags.size()));
    f32(mn.x); f32(mn.y); f32(mn.z); f32(mx.x); f32(mx.y); f32(mx.z);
    f32(mn.x); f32(mn.y); f32(mn.z);        // vertex
    f32(0); f32(0);                         // uv
    for (int i = 0; i < 6; ++i) { v.push_back(0); v.push_back(0); } // face
    f32(0); f32(0); f32(1);                 // normal
    for (const TestTag& t : tags) {
        fixed(t.name, 32);
        f32(t.pos.x); f32(t.pos.y); f32(t.pos.z);
        f32(0); f32(0); f32(1);
    }
    return v;
}

struct Rig {
    MemSource* src = nullptr; // owned by vfs
    Vfs vfs;
    DefDatabase db;
    World world;
    std::string objects = kBaseObjects;
    std::string weapons;
    std::string playerScript; // optional script path for p_comanche
    WorldConfig config;

    Rig() {
        std::unique_ptr<MemSource> s(new MemSource());
        src = s.get();
        vfs.mount(std::move(s));
    }
    void obj(const std::string& text) { objects += text; }
    void script(const std::string& path, const Asm& a) { src->add(path, a.build()); }
    void model(const std::string& path, Vec3 mn, Vec3 mx, const std::vector<TestTag>& tags = {}) {
        src->add(path, makeMdl(mn, mx, tags));
    }
    // Loads the definitions and starts an empty level (flat ground at 0).
    void start(bool players = false) {
        std::string heli = "p_comanche {\n player\n flag FL_TEMPORARY\n health 400\n";
        if (!playerScript.empty()) heli += " script \"" + playerScript + "\"\n";
        heli += "}\n";
        src->add("objects\\test.obj", heli + objects);
        if (!weapons.empty()) src->add("weapons\\test.wpn", weapons);
        db.load(vfs);
        world.init(vfs, db, config);
        world.startEmptyLevel(players);
    }
    int create(const std::string& name, Vec3 pos = {640.0f, 300.0f, 0.0f}) {
        return world.createEntity(name, pos, -1);
    }
    Entity& e(int idx) { return world.entity(idx); }
    void step(int n = 1) {
        PlayerInput in;
        for (int i = 0; i < n; ++i) world.step(in);
    }
};

} // namespace worldtest
