// Headless EGL backend: a GLES 3.0 context bound to a pbuffer surface, no window
// system involved. See docs/graphics.md for the method that actually works on the
// development machine and which environment variables matter for CI.
//
// Desktop only. On Android this file is excluded from the build (see module.cmake);
// android_stub.cpp stands in for it there.
#include "backends.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <cstdio>
#include <string>
#include <vector>

#include "as3d/core.h"

namespace as3d {
namespace {

const char* eglErrorString(EGLint err) {
    switch (err) {
        case EGL_SUCCESS: return "EGL_SUCCESS";
        case EGL_NOT_INITIALIZED: return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ACCESS: return "EGL_BAD_ACCESS";
        case EGL_BAD_ALLOC: return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE: return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONFIG: return "EGL_BAD_CONFIG";
        case EGL_BAD_CONTEXT: return "EGL_BAD_CONTEXT";
        case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
        case EGL_BAD_DISPLAY: return "EGL_BAD_DISPLAY";
        case EGL_BAD_MATCH: return "EGL_BAD_MATCH";
        case EGL_BAD_NATIVE_PIXMAP: return "EGL_BAD_NATIVE_PIXMAP";
        case EGL_BAD_NATIVE_WINDOW: return "EGL_BAD_NATIVE_WINDOW";
        case EGL_BAD_PARAMETER: return "EGL_BAD_PARAMETER";
        case EGL_BAD_SURFACE: return "EGL_BAD_SURFACE";
        default: return "EGL_UNKNOWN_ERROR";
    }
}

// One candidate EGL display to try, in order. `describe` is filled in once we know
// which one actually worked (used in GraphicsContext::description()).
struct Candidate {
    std::string label;
    EGLDisplay display;
};

// Tries every EGL_EXT_platform_device device (real GPUs, enumerated without any
// window system), then EGL_PLATFORM_SURFACELESS_MESA, then plain
// eglGetDisplay(EGL_DEFAULT_DISPLAY) -- see docs/graphics.md for why this order and
// what each one needs on this machine.
void collectCandidates(std::vector<Candidate>& out) {
    auto eglGetPlatformDisplayEXT =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    auto eglQueryDevicesEXT =
        reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(eglGetProcAddress("eglQueryDevicesEXT"));
    auto eglQueryDeviceStringEXT =
        reinterpret_cast<PFNEGLQUERYDEVICESTRINGEXTPROC>(eglGetProcAddress("eglQueryDeviceStringEXT"));

    if (eglGetPlatformDisplayEXT && eglQueryDevicesEXT) {
        EGLDeviceEXT devices[16];
        EGLint numDevices = 0;
        if (eglQueryDevicesEXT(16, devices, &numDevices)) {
            for (EGLint i = 0; i < numDevices; i++) {
                EGLDisplay dpy = eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, devices[i], nullptr);
                if (dpy == EGL_NO_DISPLAY) continue;
                std::string label = "device[" + std::to_string(i) + "]";
#ifdef EGL_DRM_DEVICE_FILE_EXT
                if (eglQueryDeviceStringEXT) {
                    const char* name = eglQueryDeviceStringEXT(devices[i], EGL_DRM_DEVICE_FILE_EXT);
                    if (name) {
                        label += " (";
                        label += name;
                        label += ")";
                    }
                }
#else
                (void)eglQueryDeviceStringEXT;
#endif
                out.push_back({label, dpy});
            }
        }
    }

#ifdef EGL_PLATFORM_SURFACELESS_MESA
    if (eglGetPlatformDisplayEXT) {
        EGLDisplay dpy =
            eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, reinterpret_cast<void*>(EGL_DEFAULT_DISPLAY), nullptr);
        if (dpy != EGL_NO_DISPLAY) out.push_back({"surfaceless-mesa", dpy});
    }
#endif

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy != EGL_NO_DISPLAY) out.push_back({"default-display", dpy});
}

class HeadlessEglContext final : public GraphicsContext {
public:
    ~HeadlessEglContext() override {
        if (display_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
            if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
            eglTerminate(display_);
        }
    }

    bool makeCurrent() override {
        return eglMakeCurrent(display_, surface_, surface_, context_) == EGL_TRUE;
    }

    void swapBuffers() override {
        // Nothing external observes the pbuffer; render into an as3d::RenderTarget
        // and read it back instead. eglSwapBuffers on a pbuffer is a defined no-op on
        // every implementation this was tested against, so this is harmless either way.
    }

    int width() const override { return width_; }
    int height() const override { return height_; }

    std::string description() const override { return description_; }

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    int width_ = 0, height_ = 0;
    std::string description_;
};

} // namespace

std::unique_ptr<GraphicsContext> createHeadlessContext(const GraphicsConfig& config) {
    eglBindAPI(EGL_OPENGL_ES_API);

    std::vector<Candidate> candidates;
    collectCandidates(candidates);
    if (candidates.empty()) {
        AS3D_ERROR("headless EGL: no candidate display (no EGL_EXT_platform_device, no "
                    "EGL_PLATFORM_SURFACELESS_MESA, and eglGetDisplay(EGL_DEFAULT_DISPLAY) "
                    "returned EGL_NO_DISPLAY)");
        return nullptr;
    }

    for (const Candidate& c : candidates) {
        EGLDisplay dpy = c.display;
        EGLint major = 0, minor = 0;
        if (!eglInitialize(dpy, &major, &minor)) {
            AS3D_WARN("headless EGL: %s: eglInitialize failed (%s)", c.label.c_str(),
                       eglErrorString(eglGetError()));
            continue;
        }

        const EGLint configAttribs[] = {
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_NONE,
        };
        EGLConfig eglConfig;
        EGLint numConfigs = 0;
        if (!eglChooseConfig(dpy, configAttribs, &eglConfig, 1, &numConfigs) || numConfigs < 1) {
            AS3D_WARN("headless EGL: %s: eglChooseConfig failed (%s)", c.label.c_str(),
                       eglErrorString(eglGetError()));
            eglTerminate(dpy);
            continue;
        }

        const EGLint pbufferAttribs[] = {
            EGL_WIDTH, config.width,
            EGL_HEIGHT, config.height,
            EGL_NONE,
        };
        EGLSurface surface = eglCreatePbufferSurface(dpy, eglConfig, pbufferAttribs);
        if (surface == EGL_NO_SURFACE) {
            AS3D_WARN("headless EGL: %s: eglCreatePbufferSurface failed (%s)", c.label.c_str(),
                       eglErrorString(eglGetError()));
            eglTerminate(dpy);
            continue;
        }

        const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        EGLContext context = eglCreateContext(dpy, eglConfig, EGL_NO_CONTEXT, contextAttribs);
        if (context == EGL_NO_CONTEXT) {
            AS3D_WARN("headless EGL: %s: eglCreateContext failed (%s)", c.label.c_str(),
                       eglErrorString(eglGetError()));
            eglDestroySurface(dpy, surface);
            eglTerminate(dpy);
            continue;
        }

        if (!eglMakeCurrent(dpy, surface, surface, context)) {
            AS3D_WARN("headless EGL: %s: eglMakeCurrent failed (%s)", c.label.c_str(),
                       eglErrorString(eglGetError()));
            eglDestroyContext(dpy, context);
            eglDestroySurface(dpy, surface);
            eglTerminate(dpy);
            continue;
        }

        auto ctx = std::make_unique<HeadlessEglContext>();
        ctx->display_ = dpy;
        ctx->surface_ = surface;
        ctx->context_ = context;
        ctx->width_ = config.width;
        ctx->height_ = config.height;

        const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
        const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        char buf[512];
        std::snprintf(buf, sizeof buf, "headless-egl (platform=%s, EGL %d.%d): vendor=%s renderer=%s version=%s",
                      c.label.c_str(), major, minor, vendor ? vendor : "?", renderer ? renderer : "?",
                      version ? version : "?");
        ctx->description_ = buf;
        AS3D_INFO("%s", ctx->description_.c_str());

        // Terminate every candidate we are not using.
        for (const Candidate& other : candidates) {
            if (other.display != dpy) eglTerminate(other.display);
        }
        return ctx;
    }

    AS3D_ERROR("headless EGL: every candidate display failed, see warnings above");
    for (const Candidate& c : candidates) eglTerminate(c.display);
    return nullptr;
}

} // namespace as3d
