// GLES 3.0 implementation of engine/include/as3d/gfx.h. See that header for the
// texture-orientation and readback-flip conventions; this file only implements them.
#include "as3d/gfx.h"

#include <GLES3/gl3.h>

#include <cstring>

namespace as3d {

namespace {

GLenum toGlWrap(Wrap w) {
    switch (w) {
        case Wrap::Repeat: return GL_REPEAT;
        case Wrap::ClampToEdge: return GL_CLAMP_TO_EDGE;
        case Wrap::MirroredRepeat: return GL_MIRRORED_REPEAT;
    }
    return GL_REPEAT;
}

GLenum toGlMagFilter(Filter f) { return f == Filter::Nearest ? GL_NEAREST : GL_LINEAR; }

GLenum toGlMinFilter(Filter f, bool mipmaps) {
    if (!mipmaps) return toGlMagFilter(f);
    return f == Filter::Nearest ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR;
}

GLuint compileStage(GLenum stage, const char* src, std::string* error) {
    GLuint shader = glCreateShader(stage);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        if (error) {
            GLint len = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
            std::string log(static_cast<size_t>(len > 0 ? len : 0), '\0');
            if (len > 0) glGetShaderInfoLog(shader, len, nullptr, log.data());
            *error += (stage == GL_VERTEX_SHADER ? "vertex shader: " : "fragment shader: ");
            *error += log;
            *error += "\n";
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

// ---------------------------------------------------------------------------
// ShaderProgram
// ---------------------------------------------------------------------------
ShaderProgram::~ShaderProgram() { destroy(); }

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept
    : program_(other.program_), uniformCache_(std::move(other.uniformCache_)) {
    other.program_ = 0;
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept {
    if (this != &other) {
        destroy();
        program_ = other.program_;
        uniformCache_ = std::move(other.uniformCache_);
        other.program_ = 0;
    }
    return *this;
}

void ShaderProgram::destroy() {
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    uniformCache_.clear();
}

bool ShaderProgram::compile(const char* vertexSrc, const char* fragmentSrc, std::string* error) {
    destroy();
    std::string localError;
    std::string* err = error ? error : &localError;
    err->clear();

    GLuint vs = compileStage(GL_VERTEX_SHADER, vertexSrc, err);
    GLuint fs = vs ? compileStage(GL_FRAGMENT_SHADER, fragmentSrc, err) : 0;
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        if (!error) AS3D_ERROR("shader compile failed: %s", err->c_str());
        return false;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::string log(static_cast<size_t>(len > 0 ? len : 0), '\0');
        if (len > 0) glGetProgramInfoLog(prog, len, nullptr, log.data());
        *err += "link: ";
        *err += log;
        glDeleteProgram(prog);
        if (!error) AS3D_ERROR("shader link failed: %s", err->c_str());
        return false;
    }

    program_ = prog;
    return true;
}

void ShaderProgram::use() const { glUseProgram(program_); }

int ShaderProgram::uniformLocation(const char* name) {
    auto it = uniformCache_.find(name);
    if (it != uniformCache_.end()) return it->second;
    int loc = program_ ? glGetUniformLocation(program_, name) : -1;
    uniformCache_.emplace(name, loc);
    return loc;
}

void ShaderProgram::setInt(const char* name, int v) {
    int loc = uniformLocation(name);
    if (loc < 0) return;
    glUseProgram(program_);
    glUniform1i(loc, v);
}

void ShaderProgram::setFloat(const char* name, float v) {
    int loc = uniformLocation(name);
    if (loc < 0) return;
    glUseProgram(program_);
    glUniform1f(loc, v);
}

void ShaderProgram::setVec2(const char* name, const Vec2& v) {
    int loc = uniformLocation(name);
    if (loc < 0) return;
    glUseProgram(program_);
    glUniform2f(loc, v.x, v.y);
}

void ShaderProgram::setVec3(const char* name, const Vec3& v) {
    int loc = uniformLocation(name);
    if (loc < 0) return;
    glUseProgram(program_);
    glUniform3f(loc, v.x, v.y, v.z);
}

void ShaderProgram::setVec4(const char* name, const Vec4& v) {
    int loc = uniformLocation(name);
    if (loc < 0) return;
    glUseProgram(program_);
    glUniform4f(loc, v.x, v.y, v.z, v.w);
}

void ShaderProgram::setMat3(const char* name, const Mat3& v) {
    int loc = uniformLocation(name);
    if (loc < 0) return;
    glUseProgram(program_);
    glUniformMatrix3fv(loc, 1, GL_FALSE, v.m);
}

void ShaderProgram::setMat4(const char* name, const Mat4& v) {
    int loc = uniformLocation(name);
    if (loc < 0) return;
    glUseProgram(program_);
    glUniformMatrix4fv(loc, 1, GL_FALSE, v.m);
}

// ---------------------------------------------------------------------------
// Texture2D
// ---------------------------------------------------------------------------
Texture2D::~Texture2D() { destroy(); }

Texture2D::Texture2D(Texture2D&& other) noexcept
    : id_(other.id_), width_(other.width_), height_(other.height_) {
    other.id_ = 0;
    other.width_ = other.height_ = 0;
}

Texture2D& Texture2D::operator=(Texture2D&& other) noexcept {
    if (this != &other) {
        destroy();
        id_ = other.id_;
        width_ = other.width_;
        height_ = other.height_;
        other.id_ = 0;
        other.width_ = other.height_ = 0;
    }
    return *this;
}

void Texture2D::destroy() {
    if (id_) {
        glDeleteTextures(1, &id_);
        id_ = 0;
    }
    width_ = height_ = 0;
}

void Texture2D::create(const Image& image, const TextureOptions& options) {
    destroy();
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    // Rows are uploaded exactly as as3d::Image stores them (row 0 first); see the
    // header for why that makes v=0 the TOP of the texture in this engine.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, image.rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, toGlWrap(options.wrapS));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, toGlWrap(options.wrapT));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, toGlMagFilter(options.magFilter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                     toGlMinFilter(options.minFilter, options.mipmaps));
    if (options.mipmaps) glGenerateMipmap(GL_TEXTURE_2D);
    width_ = image.width;
    height_ = image.height;
}

void Texture2D::bind(unsigned unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, id_);
}

// ---------------------------------------------------------------------------
// VertexBuffer / IndexBuffer / VertexArray
// ---------------------------------------------------------------------------
VertexBuffer::~VertexBuffer() { destroy(); }

VertexBuffer::VertexBuffer(VertexBuffer&& other) noexcept : id_(other.id_) { other.id_ = 0; }

VertexBuffer& VertexBuffer::operator=(VertexBuffer&& other) noexcept {
    if (this != &other) {
        destroy();
        id_ = other.id_;
        other.id_ = 0;
    }
    return *this;
}

void VertexBuffer::destroy() {
    if (id_) {
        glDeleteBuffers(1, &id_);
        id_ = 0;
    }
}

void VertexBuffer::upload(const void* data, size_t bytes, bool dynamic) {
    if (!id_) glGenBuffers(1, &id_);
    glBindBuffer(GL_ARRAY_BUFFER, id_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), data,
                 dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
}

IndexBuffer::~IndexBuffer() { destroy(); }

IndexBuffer::IndexBuffer(IndexBuffer&& other) noexcept
    : id_(other.id_), type_(other.type_), count_(other.count_) {
    other.id_ = 0;
    other.count_ = 0;
}

IndexBuffer& IndexBuffer::operator=(IndexBuffer&& other) noexcept {
    if (this != &other) {
        destroy();
        id_ = other.id_;
        type_ = other.type_;
        count_ = other.count_;
        other.id_ = 0;
        other.count_ = 0;
    }
    return *this;
}

void IndexBuffer::destroy() {
    if (id_) {
        glDeleteBuffers(1, &id_);
        id_ = 0;
    }
    count_ = 0;
}

void IndexBuffer::upload(const void* data, size_t bytes, IndexType type, bool dynamic) {
    if (!id_) glGenBuffers(1, &id_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, id_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), data,
                 dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    type_ = type;
    count_ = bytes / (type == IndexType::U16 ? sizeof(u16) : sizeof(u32));
}

VertexArray::~VertexArray() { destroy(); }

VertexArray::VertexArray(VertexArray&& other) noexcept : id_(other.id_) { other.id_ = 0; }

VertexArray& VertexArray::operator=(VertexArray&& other) noexcept {
    if (this != &other) {
        destroy();
        id_ = other.id_;
        other.id_ = 0;
    }
    return *this;
}

void VertexArray::destroy() {
    if (id_) {
        glDeleteVertexArrays(1, &id_);
        id_ = 0;
    }
}

void VertexArray::create(const VertexBuffer& vbo, const VertexLayout& layout, const IndexBuffer* ibo) {
    destroy();
    glGenVertexArrays(1, &id_);
    glBindVertexArray(id_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo.id());
    for (const VertexAttrib& a : layout.attribs) {
        glEnableVertexAttribArray(static_cast<GLuint>(a.location));
        glVertexAttribPointer(static_cast<GLuint>(a.location), a.components, GL_FLOAT,
                               a.normalized ? GL_TRUE : GL_FALSE, layout.strideBytes,
                               reinterpret_cast<const void*>(a.offsetBytes));
    }
    if (ibo) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo->id());
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (ibo) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void VertexArray::bind() const { glBindVertexArray(id_); }

// ---------------------------------------------------------------------------
// RenderTarget
// ---------------------------------------------------------------------------
RenderTarget::~RenderTarget() { destroy(); }

RenderTarget::RenderTarget(RenderTarget&& other) noexcept
    : width_(other.width_), height_(other.height_), samples_(other.samples_), fbo_(other.fbo_),
      colorRbo_(other.colorRbo_), depthRbo_(other.depthRbo_), resolveFbo_(other.resolveFbo_),
      resolveColorTexture_(other.resolveColorTexture_) {
    other.width_ = other.height_ = other.samples_ = 0;
    other.fbo_ = other.colorRbo_ = other.depthRbo_ = other.resolveFbo_ = other.resolveColorTexture_ = 0;
}

RenderTarget& RenderTarget::operator=(RenderTarget&& other) noexcept {
    if (this != &other) {
        destroy();
        width_ = other.width_;
        height_ = other.height_;
        samples_ = other.samples_;
        fbo_ = other.fbo_;
        colorRbo_ = other.colorRbo_;
        depthRbo_ = other.depthRbo_;
        resolveFbo_ = other.resolveFbo_;
        resolveColorTexture_ = other.resolveColorTexture_;
        other.width_ = other.height_ = other.samples_ = 0;
        other.fbo_ = other.colorRbo_ = other.depthRbo_ = other.resolveFbo_ =
            other.resolveColorTexture_ = 0;
    }
    return *this;
}

void RenderTarget::destroy() {
    if (colorRbo_) glDeleteRenderbuffers(1, &colorRbo_);
    if (depthRbo_) glDeleteRenderbuffers(1, &depthRbo_);
    if (resolveColorTexture_) glDeleteTextures(1, &resolveColorTexture_);
    if (resolveFbo_ && resolveFbo_ != fbo_) glDeleteFramebuffers(1, &resolveFbo_);
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    width_ = height_ = samples_ = 0;
    fbo_ = colorRbo_ = depthRbo_ = resolveFbo_ = resolveColorTexture_ = 0;
}

namespace {
bool makeResolveTarget(int width, int height, GLuint& fbo, GLuint& colorTex) {
    glGenTextures(1, &colorTex);
    glBindTexture(GL_TEXTURE_2D, colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}
} // namespace

bool RenderTarget::create(int width, int height, int msaaSamples) {
    destroy();
    width_ = width;
    height_ = height;

    if (msaaSamples > 0) {
        GLint maxSamples = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        int samples = msaaSamples < maxSamples ? msaaSamples : maxSamples;
        if (samples > 0) {
            glGenFramebuffers(1, &fbo_);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

            glGenRenderbuffers(1, &colorRbo_);
            glBindRenderbuffer(GL_RENDERBUFFER, colorRbo_);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorRbo_);

            glGenRenderbuffers(1, &depthRbo_);
            glBindRenderbuffer(GL_RENDERBUFFER, depthRbo_);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, width, height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRbo_);

            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
                samples_ = samples;
                if (!makeResolveTarget(width, height, resolveFbo_, resolveColorTexture_)) {
                    AS3D_WARN("RenderTarget: MSAA resolve FBO incomplete, disabling MSAA");
                    destroy();
                } else {
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    return true;
                }
            } else {
                AS3D_WARN("RenderTarget: %dx MSAA framebuffer incomplete, falling back to no MSAA", samples);
                destroy();
            }
        }
    }

    // Non-MSAA path: a single FBO doubles as both the render target and the
    // "resolve" target (there is nothing to resolve).
    if (!makeResolveTarget(width, height, resolveFbo_, resolveColorTexture_)) {
        AS3D_ERROR("RenderTarget: colour framebuffer incomplete (%dx%d)", width, height);
        destroy();
        return false;
    }
    fbo_ = resolveFbo_;

    glGenRenderbuffers(1, &depthRbo_);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRbo_);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        AS3D_ERROR("RenderTarget: framebuffer incomplete after adding depth (%dx%d)", width, height);
        destroy();
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void RenderTarget::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);
}

bool RenderTarget::readPixels(Image& out) const {
    if (!fbo_) return false;
    if (samples_ > 0) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo_);
        glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, resolveFbo_);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    std::vector<u8> raw(static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // glReadPixels' row 0 is the bottom of the rendered image (GL window-space y=0 is
    // the bottom); as3d::Image wants row 0 = top, so flip here.
    out.width = width_;
    out.height = height_;
    out.hasAlpha = true;
    out.rgba.resize(raw.size());
    size_t rowBytes = static_cast<size_t>(width_) * 4;
    for (int row = 0; row < height_; row++) {
        const u8* src = raw.data() + static_cast<size_t>(height_ - 1 - row) * rowBytes;
        u8* dst = out.rgba.data() + static_cast<size_t>(row) * rowBytes;
        std::memcpy(dst, src, rowBytes);
    }
    return true;
}

// ---------------------------------------------------------------------------
// State helpers
// ---------------------------------------------------------------------------
void setBlend(BlendMode mode) {
    if (mode == BlendMode::Off) {
        glDisable(GL_BLEND);
        return;
    }
    glEnable(GL_BLEND);
    if (mode == BlendMode::AlphaBlend) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else { // Additive
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    }
}

void setDepth(bool test, bool write) {
    if (test) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(write ? GL_TRUE : GL_FALSE);
}

void setCull(CullMode mode) {
    if (mode == CullMode::Off) {
        glDisable(GL_CULL_FACE);
        return;
    }
    glEnable(GL_CULL_FACE);
    glCullFace(mode == CullMode::Back ? GL_BACK : GL_FRONT);
    glFrontFace(GL_CCW);
}

void setViewport(int x, int y, int w, int h) { glViewport(x, y, w, h); }

void clear(const Vec4& color, bool depth, float depthValue) {
    glClearColor(color.x, color.y, color.z, color.w);
    GLbitfield mask = GL_COLOR_BUFFER_BIT;
    if (depth) {
        glClearDepthf(depthValue);
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    glClear(mask);
}

} // namespace as3d
