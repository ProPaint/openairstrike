// Fog on the PICA (pica_frame.h): the linear fog of the GL shaders through the fog unit's
// lookup table. The table comes from as3d/fog_lut.h buildLinearFogLut for the current depth
// mapping; its index convention (entry i = depth i/128, depth 1 at the near plane, zFlip off)
// is citro3d's own (fog.c FogLut_Exp, fog.h FogLut_CalcZ), checked in apps/tests/fog_lut_test.cpp
// and on Azahar by apps/3ds/pica_test.cpp (fog ramp).
#include <citro3d.h>

#include <algorithm>

#include "as3d/fog_lut.h"
#include "pica_frame.h"

namespace as3d::pica {
namespace {

struct FogState {
    bool enabled = false;
    bool lutValid = false;
    float nearPlane = 0, farPlane = 0, start = 0, end = 0;
    FogDepthMap map = FogDepthMap::ZBuffer;
    C3D_FogLut lut{};
    Vec3 levelColor{0.0f, 0.0f, 0.0f};
};

FogState& fog() {
    static FogState s;
    return s;
}

} // namespace

bool g_fogForcedOff = false; // run.ini fog=0 (hardware A/B)

void setFogForcedOff(bool off) { g_fogForcedOff = off; }

void setFog(float nearPlane, float farPlane, float fogStart, float fogEnd) {
    if (g_fogForcedOff) {
        disableFog();
        return;
    }
    FogState& s = fog();
    const FogDepthMap map = depthMapping();
    if (!s.lutValid || s.nearPlane != nearPlane || s.farPlane != farPlane || s.start != fogStart ||
        s.end != fogEnd || s.map != map) {
        float data[256];
        buildLinearFogLut(nearPlane, farPlane, fogStart, fogEnd, data, map);
        FogLut_FromArray(&s.lut, data);
        s.nearPlane = nearPlane;
        s.farPlane = farPlane;
        s.start = fogStart;
        s.end = fogEnd;
        s.map = map;
        s.lutValid = true;
    }
    C3D_FogLutBind(&s.lut);
    C3D_FogGasMode(GPU_FOG, GPU_PLAIN_DENSITY, false);
    s.enabled = true;
}

void disableFog() {
    fog().enabled = false;
    C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);
}

bool fogEnabled() { return fog().enabled; }

void setLevelFogColor(const Vec3& rgb) { fog().levelColor = rgb; }

Vec3 levelFogColor() { return fog().levelColor; }

void applyFogColor(const Vec3& rgb) {
    auto b = [](float v) { return static_cast<u32>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    // GPUREG_FOG_COLOR: R in bits 0-7, G 8-15, B 16-23.
    detail::c3dFogColor(b(rgb.x) | b(rgb.y) << 8 | b(rgb.z) << 16);
}

} // namespace as3d::pica
