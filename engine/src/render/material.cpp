// Implements as3d::Material::fromObjectDef (as3d/scene.h) from docs/spec/obj.md's
// blend/envmode/rflag/sort statement tables.
#include "as3d/scene.h"

#include "as3d/defs.h"

namespace as3d {

namespace {

MaterialBlend toMaterialBlend(BlendMode b) {
    switch (b) {
        case BlendMode::None: return MaterialBlend::Opaque;
        case BlendMode::Alpha: return MaterialBlend::Alpha;
        case BlendMode::Add: return MaterialBlend::Add;
        case BlendMode::Filter: return MaterialBlend::Filter;
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

    // "skin" overrides the model's own texture path (docs/spec/mdl.md "Texture path",
    // render-pipeline.md 3.2 step 4: the record skin, else the model's default texture).
    // For an object without a skin the model's stored path is the texture; the shipped
    // models keep a game path there (e.g. models\mapobjects\palms\palm.tga). An object
    // with neither (light/mark/anchor objects) gets the placeholder, which is fine: those
    // draw nothing.
    std::string skinPath = def.skin;
    if (skinPath.empty() && !def.model.empty()) {
        const GpuMesh& mesh = cache.mesh(def.model);
        if (mesh.valid) skinPath = mesh.model.sourceTexturePath;
    }
    ResourceCache::LoadedTexture tex = cache.texture(skinPath);
    mat.texture = tex.texture;
    mat.textureHasAlpha = tex.hasAlpha;

    mat.blend = toMaterialBlend(def.blend);
    // ENV_GLITTER/CHROME/QUAD (docs/spec/obj.md "envmode"): drawn by MeshRenderer from
    // envModeRaw and envTexture (render-pipeline.md 4.3, as3d/envmap.h).
    mat.envModeRaw = static_cast<int>(def.envmode);
    if (!def.envmap.empty()) mat.envTexture = cache.texture(def.envmap).texture;
    mat.rflag = def.rflag;
    mat.noLighting = (def.rflag & RF_NOLIGHTING) != 0;
    mat.noCulling = (def.rflag & RF_NOCULLING) != 0;
    mat.noDepthTest = (def.rflag & RF_NODEPTHTEST) != 0;
    mat.noDepthWrite = (def.rflag & RF_NODEPTHWRITE) != 0;
    mat.sortBucket = toSortBucket(def.sort);

    // No alpha test (render-pipeline.md 4.1, 1.1): the original never enables it, so an
    // opaque-blend model with a 32-bit skin simply draws its RGB. alphaTest stays false.
    mat.alphaTest = false;

    return mat;
}

} // namespace as3d
