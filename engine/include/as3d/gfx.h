// Thin RAII wrappers over OpenGL ES 3.0 objects. No hidden global state: every object
// here owns exactly the GL name(s) it created, and none of them reach for a current
// context implicitly except through the GL calls themselves (the caller is
// responsible for having one current, via as3d::GraphicsContext).
//
// This header includes no SDL/EGL/desktop-GL headers, and no platform.h: `render` is
// usable unchanged on Android as long as *something* (the `platform` module) has made
// a GLES 3.0 context current first.
//
// Texture orientation convention (read this before touching model/level UVs):
//   as3d::Image (see as3d/image.h) always stores row 0 as the TOP row of the image.
//   Texture2D uploads those rows to GL exactly as they are, in the same order,
//   performing no flip. GL's first uploaded row becomes texture row 0, which this
//   engine DEFINES to be sampled at v = 0. The consequence: in this engine, v = 0 is
//   the TOP of the texture and v = 1 is the BOTTOM -- the mirror of the "traditional"
//   OpenGL convention (bottom-left origin) that many tutorials assume. Every piece of
//   UV data in this engine (quads, model meshes, sprite atlases) must be authored
//   against this convention: UV (0,0) is the visual top-left texel. Do not flip V
//   anywhere else in the pipeline to compensate; there is exactly one convention and
//   it is this one.
//
// Framebuffer readback is a different story: OpenGL's window/framebuffer coordinate
// system is fixed by the spec with y=0 at the bottom, so glReadPixels always returns
// its first row as the bottom of what was rendered. RenderTarget::readPixels flips
// that back into an as3d::Image (row 0 = top), so a vertex placed at the top of clip
// space (NDC y = +1) ends up in the low-numbered rows of the returned Image, matching
// the Image convention above.
#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "as3d/core.h"
#include "as3d/image.h"
#include "as3d/math.h"

namespace as3d {

// ---------------------------------------------------------------------------
// ShaderProgram
// ---------------------------------------------------------------------------
class ShaderProgram {
public:
    ShaderProgram() = default;
    ~ShaderProgram();
    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;
    ShaderProgram(ShaderProgram&& other) noexcept;
    ShaderProgram& operator=(ShaderProgram&& other) noexcept;

    // Compiles and links a `#version 300 es` vertex+fragment pair. On failure returns
    // false, leaves the object invalid (valid() == false, never partially built), and
    // if `error` is non-null fills it with the compiler/linker log instead of
    // crashing or asserting.
    bool compile(const char* vertexSrc, const char* fragmentSrc, std::string* error = nullptr);

    bool valid() const { return program_ != 0; }
    void use() const;
    unsigned id() const { return program_; }

    // -1 if the uniform does not exist (or was optimized out); cached after the first
    // lookup per name.
    int uniformLocation(const char* name);

    void setInt(const char* name, int v);
    void setFloat(const char* name, float v);
    void setVec2(const char* name, const Vec2& v);
    void setVec3(const char* name, const Vec3& v);
    void setVec4(const char* name, const Vec4& v);
    void setMat3(const char* name, const Mat3& v);
    void setMat4(const char* name, const Mat4& v);

private:
    unsigned program_ = 0;
    std::unordered_map<std::string, int> uniformCache_;
    void destroy();
};

// ---------------------------------------------------------------------------
// Texture2D
// ---------------------------------------------------------------------------
enum class Filter { Nearest, Linear };
enum class Wrap { Repeat, ClampToEdge, MirroredRepeat };

struct TextureOptions {
    bool mipmaps = false;
    Filter minFilter = Filter::Linear;
    Filter magFilter = Filter::Linear;
    Wrap wrapS = Wrap::Repeat;
    Wrap wrapT = Wrap::Repeat;
};

class Texture2D {
public:
    Texture2D() = default;
    ~Texture2D();
    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;
    Texture2D(Texture2D&& other) noexcept;
    Texture2D& operator=(Texture2D&& other) noexcept;

    // Uploads `image` as-is (see the file header for the orientation convention). RGBA8
    // always; `image.hasAlpha` does not change the internal format, only how a caller
    // may choose to blend.
    void create(const Image& image, const TextureOptions& options = {});
    void bind(unsigned unit = 0) const;

    bool valid() const { return id_ != 0; }
    int width() const { return width_; }
    int height() const { return height_; }
    unsigned id() const { return id_; }

private:
    unsigned id_ = 0;
    int width_ = 0;
    int height_ = 0;
    void destroy();
};

// ---------------------------------------------------------------------------
// Buffers and vertex layout
// ---------------------------------------------------------------------------
class VertexBuffer {
public:
    VertexBuffer() = default;
    ~VertexBuffer();
    VertexBuffer(const VertexBuffer&) = delete;
    VertexBuffer& operator=(const VertexBuffer&) = delete;
    VertexBuffer(VertexBuffer&& other) noexcept;
    VertexBuffer& operator=(VertexBuffer&& other) noexcept;

    void upload(const void* data, size_t bytes, bool dynamic = false);
    unsigned id() const { return id_; }
    bool valid() const { return id_ != 0; }

private:
    unsigned id_ = 0;
    void destroy();
};

enum class IndexType { U16, U32 };

class IndexBuffer {
public:
    IndexBuffer() = default;
    ~IndexBuffer();
    IndexBuffer(const IndexBuffer&) = delete;
    IndexBuffer& operator=(const IndexBuffer&) = delete;
    IndexBuffer(IndexBuffer&& other) noexcept;
    IndexBuffer& operator=(IndexBuffer&& other) noexcept;

    void upload(const void* data, size_t bytes, IndexType type, bool dynamic = false);
    unsigned id() const { return id_; }
    IndexType type() const { return type_; }
    size_t count() const { return count_; }
    bool valid() const { return id_ != 0; }

private:
    unsigned id_ = 0;
    IndexType type_ = IndexType::U16;
    size_t count_ = 0;
    void destroy();
};

// One vertex attribute: `location` matches the shader's `layout(location = N)`.
// Attributes are always tightly-typed floats (normals/positions/UVs/colours); that
// covers every vertex format this milestone needs.
struct VertexAttrib {
    int location = 0;
    int components = 3;    // 1-4
    size_t offsetBytes = 0;
    bool normalized = false;
};

struct VertexLayout {
    std::vector<VertexAttrib> attribs;
    int strideBytes = 0;
};

class VertexArray {
public:
    VertexArray() = default;
    ~VertexArray();
    VertexArray(const VertexArray&) = delete;
    VertexArray& operator=(const VertexArray&) = delete;
    VertexArray(VertexArray&& other) noexcept;
    VertexArray& operator=(VertexArray&& other) noexcept;

    // `ibo` may be null for non-indexed draws.
    void create(const VertexBuffer& vbo, const VertexLayout& layout, const IndexBuffer* ibo = nullptr);
    void bind() const;
    bool valid() const { return id_ != 0; }
    unsigned id() const { return id_; }

private:
    unsigned id_ = 0;
    void destroy();
};

// ---------------------------------------------------------------------------
// Render target (offscreen FBO)
// ---------------------------------------------------------------------------
// RGBA8 colour + 24-bit depth. With msaaSamples > 0, renders into a multisampled
// renderbuffer pair and resolves into a plain texture on demand (readPixels(), or
// resolve() to sample the result as a texture); falls back to no MSAA if the GLES
// implementation does not support glRenderbufferStorageMultisample at that sample
// count.
class RenderTarget {
public:
    RenderTarget() = default;
    ~RenderTarget();
    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;
    RenderTarget(RenderTarget&& other) noexcept;
    RenderTarget& operator=(RenderTarget&& other) noexcept;

    bool create(int width, int height, int msaaSamples = 0);
    bool valid() const { return fbo_ != 0; }

    // Binds this target's FBO and sets the viewport to its full size.
    void bind() const;

    int width() const { return width_; }
    int height() const { return height_; }

    // Resolves MSAA (if any) and reads the colour buffer back into `out` as a
    // top-down RGBA8 as3d::Image (see the file header for why this flips rows).
    bool readPixels(Image& out) const;

    // GL name of the resolved colour texture (0 if MSAA and not yet resolved by a
    // prior readPixels() call, or if create() failed).
    unsigned colorTextureId() const { return resolveColorTexture_; }

private:
    int width_ = 0, height_ = 0;
    int samples_ = 0;
    unsigned fbo_ = 0;              // render target (multisampled if samples_ > 0)
    unsigned colorRbo_ = 0;         // multisample colour renderbuffer (samples_ > 0 only)
    unsigned depthRbo_ = 0;         // depth renderbuffer, always a renderbuffer
    unsigned resolveFbo_ = 0;       // single-sample resolve target (samples_ > 0 only)
    unsigned resolveColorTexture_ = 0; // single-sample colour texture, always present
    void destroy();
};

// ---------------------------------------------------------------------------
// Small state helpers (no hidden state beyond what GL itself tracks)
// ---------------------------------------------------------------------------
enum class BlendMode { Off, AlphaBlend, Additive };
enum class CullMode { Off, Back, Front };

void setBlend(BlendMode mode);
void setDepth(bool test, bool write);
void setCull(CullMode mode);
void setViewport(int x, int y, int w, int h);
void clear(const Vec4& color, bool depth = true, float depthValue = 1.0f);

// Writes `image` (top-down rows, as produced by RenderTarget::readPixels or
// decodeTga) as an RGBA PNG. Returns false on I/O or encode failure.
bool writePng(const char* path, const Image& image);

} // namespace as3d
