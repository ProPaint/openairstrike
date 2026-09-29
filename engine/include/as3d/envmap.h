// Environment mapping (docs/spec/render-pipeline.md 4.3): the CPU reference of the sphere-map
// coordinate generation and of the three combine modes that the model shader implements on
// the GPU. MeshRenderer draws Materials with `envTexture` set through these formulas; the
// functions here are the tested definition. No GL.
//
// Named EnvMapMode, not EnvMode, because as3d/defs.h already declares an EnvMode enum and
// the two headers are often included together.
#pragma once

#include "as3d/math.h"

namespace as3d {

enum class EnvMapMode { None = 0, Glitter = 1, Chrome = 2, Quad = 3 };

// obj `envmode` value (as3d::EnvMode's underlying value) to the mode; anything else is None.
EnvMapMode envMapModeFromInt(int value);

// The original sends each model normal folded to its absolute value (render-pipeline.md
// 2.3, "side effect to reproduce"); the environment map sees only these.
Vec3 foldEnvNormal(const Vec3& modelNormal);

// Matrix that takes a model-space normal to eye space: the inverse transpose of the upper
// 3x3 of view * model. Not renormalised, so a scaled model gets normals of length 1/scale.
Mat3 envNormalMatrix(const Mat4& view, const Mat4& model);

// GL_SPHERE_MAP texture coordinates (s, t) of a vertex at `eyePosition` with eye-space
// normal `eyeNormal` (the eye is at the origin).
Vec2 sphereMapUv(const Vec3& eyePosition, const Vec3& eyeNormal);

// The sequels' coordinates (as2/render-pipeline.delta.md 4.3): the folded normal through the
// model's world matrix (scale included, not renormalised) and the view, then
// (0.5 + 0.5·n.x, 0.5 + 0.5·n.y), used as a v-down coordinate (this engine's convention).
Vec2 viewNormalEnvUv(const Mat4& view, const Mat4& model, const Vec3& modelNormal);

// ENV_QUAD: the texture matrix rotates the sphere-map coordinates by time * 30 degrees about
// the texture origin (0, 0).
constexpr float kEnvQuadDegreesPerSecond = 30.0f;
Vec2 rotateQuadEnvUv(const Vec2& st, float timeSeconds);

// Fragment colour of one texel pair. `skin`/`env` are texel RGBA, `vertexColour` the lit
// vertex colour (alpha 1 for lit models, the entity colour for unlit ones).
//   Glitter: rgb = skin.rgb * colour.rgb + env.rgb,  a = skin.a * colour.a * env.a
//   Chrome:  rgb = skin.rgb * skin.a + env.rgb * colour.rgb * (1 - skin.a)
//   Quad:    rgb = env.rgb * colour.rgb,             a = env.a * colour.a  (skin not drawn)
// Results are not clamped (the framebuffer does that).
Vec4 combineEnvMap(EnvMapMode mode, const Vec4& skin, const Vec4& env, const Vec4& vertexColour);

} // namespace as3d
