#include "doctest.h"

#include "as3d/core.h"

using namespace as3d;

TEST_CASE("ByteReader reads little-endian and flags overruns") {
    const u8 bytes[] = {0x01, 0x02, 0x03, 0x04, 0x00, 0x00, 0x80, 0x3f};
    ByteReader r(bytes, sizeof bytes);
    CHECK(r.readU32() == 0x04030201u);
    CHECK(r.readF32() == 1.0f);
    CHECK(!r.failed());
    CHECK(r.readU8() == 0);
    CHECK(r.failed());
}

TEST_CASE("normalizePath") {
    CHECK(normalizePath("Models/Tanks/T1.MDL") == "models\\tanks\\t1.mdl");
}

TEST_CASE("Rng is deterministic") {
    Rng a(42), b(42);
    for (int i = 0; i < 100; i++) {
        float v = a.uniform();
        CHECK(v == b.uniform());
        CHECK(v >= 0.0f);
        CHECK(v < 1.0f);
    }
}
