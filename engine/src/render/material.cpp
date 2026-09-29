// Implements as3d::Material::fromObjectDef (as3d/scene.h) from docs/spec/obj.md's
// blend/envmode/rflag/sort statement tables.
//
// This file is the one place in the engine that must see BOTH as3d/scene.h (for
// Material/ResourceCache/Texture2D, i.e. as3d/gfx.h) and as3d/defs.h (for the real
// ObjectDef) in the same translation unit. Both public headers declare an unrelated
// `enum class as3d::BlendMode` with different enumerators (scene.h: obj.md's own
// None/Alpha/Add/Filter; gfx.h: a GL blend-state helper's Off/AlphaBlend/Additive) --
// #including both normally is a straight redefinition error. Neither public header may
// be edited by this package (WP-30/33's file list), so this file works around the
// conflict locally and only here: `BlendMode` is #define'd to a private name for the
// duration of the as3d/defs.h #include, so the preprocessor transparently renames
// defs.h's own enum (and ObjectDef::blend's field type) before the compiler ever parses
// the real identifier. This is a narrowly-scoped, single-file, mechanical workaround --
// see the WP-30/33 final report for the proposed real fix (rename one of the two
// enums so a file like this one is never needed again).
#include "as3d/scene.h"

#define BlendMode AS3D_DEFS_BlendMode
#include "as3d/defs.h"
#undef BlendMode

namespace as3d {

namespace {

MaterialBlend toMaterialBlend(AS3D_DEFS_BlendMode b) {
    switch (b) {
        case AS3D_DEFS_BlendMode::None: return MaterialBlend::Opaque;
        case AS3D_DEFS_BlendMode::Alpha: return MaterialBlend::Alpha;
        case AS3D_DEFS_BlendMode::Add: return MaterialBlend::Add;
        case AS3D_DEFS_BlendMode::Filter: return MaterialBlend::Filter;
    }
    return MaterialBlend::Opaque;
}

SortBucket toSortBucket(SortMode s) {
    switch (s) {
        case SortMode::Opaque: return SortBucket::Opaque;
        case SortMode::Trans: return SortBucket::Trans;
        case SortMode::Effect: return SortBucket::Effect;
    }
    return SortBucket::Opaque;
}

} // namespace

Material Material::fromObjectDef(const ObjectDef& def, ResourceCache& cache) {
    Material mat;

    // "skin" overrides whatever texture path the .mdl itself carries in most cases
    // (docs/spec/mdl.md, "Texture path"); that embedded path is a local Windows path
    // from the artist's machine and is not a usable game path, so it is never used as a
    // fallback here. An object with no skin (light/mark/anchor objects, see obj.md
    // "type") renders with the placeholder texture, which is fine: those objects have
    // RF_NOLIGHTING/no mesh in practice, not a real missing-asset problem.
    ResourceCache::LoadedTexture tex = cache.texture(def.skin);
    mat.texture = tex.texture;
    mat.textureHasAlpha = tex.hasAlpha;

    mat.blend = toMaterialBlend(def.blend);
    // ENV_GLITTER/CHROME/QUAD (docs/spec/obj.md "envmode"): TODO, out of scope for
    // WP-30 per the work package -- dynamic lights/shadows/environment mapping are
    // left to a later package. The base texture is drawn unmodified regardless of
    // envmode; this raw value is kept only so a debug view can report what was asked
    // for.
    mat.envModeRaw = static_cast<int>(def.envmode);
    mat.rflag = def.rflag;
    mat.noLighting = (def.rflag & RF_NOLIGHTING) != 0;
    mat.noCulling = (def.rflag & RF_NOCULLING) != 0;
    mat.noDepthTest = (def.rflag & RF_NODEPTHTEST) != 0;
    mat.noDepthWrite = (def.rflag & RF_NODEPTHWRITE) != 0;
    mat.sortBucket = toSortBucket(def.sort);

    // Alpha-tested cutout, kept in the opaque (depth-writing) bucket: an object with no
    // `blend` statement (opaque) whose texture nonetheless carries a real alpha channel
    // (e.g. foliage/fences/chain-link -- cutout art with an alpha mask but no blend
    // mode) needs *some* handling of that channel, or fully transparent texels would
    // render as solid garbage. Alpha test (discard below a threshold, applied in the
    // shader) is the standard fix for exactly this case, and unlike real translucency
    // it does not need back-to-front sorting.
    mat.alphaTest = (mat.blend == MaterialBlend::Opaque) && mat.textureHasAlpha;

    return mat;
}

} // namespace as3d
