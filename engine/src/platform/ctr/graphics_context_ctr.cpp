// as3d::createGraphicsContext for the 3DS: gfx, the bottom screen format fix, citro3d and the
// three render targets (docs/3ds-port.md, M1.0: consoleInit leaves the bottom screen RGB565 and
// single-buffered, while the display transfer writes RGB8).
#include <3ds.h>

#include <cstdio>
#include <string>

#include "as3d/core.h"
#include "ctr_context.h"

namespace as3d {
namespace ctr {
namespace {

#define AS3D_DISPLAY_TRANSFER_FLAGS                                                                     \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |                    \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |       \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

CtrContext* g_live = nullptr;

class Impl final : public CtrContext {
public:
    ~Impl() override {
        if (inFrame_) C3D_FrameEnd(0);
        for (C3D_RenderTarget* t : {top_[0], top_[1], bottom_}) {
            if (t) C3D_RenderTargetDelete(t);
        }
        if (c3dInit_) C3D_Fini();
        if (gfxInit_) gfxExit();
        if (g_live == this) g_live = nullptr;
    }

    bool init() {
        APT_CheckNew3DS(&new3ds_);
        gfxInitDefault();
        gfxInit_ = true;
        gfxSet3D(true);
        // Before any target on the bottom screen exists.
        gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES);
        gfxSetDoubleBuffering(GFX_BOTTOM, true);
        if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
            AS3D_ERROR("ctr: C3D_Init failed");
            return false;
        }
        c3dInit_ = true;
        for (int eye = 0; eye < 2; ++eye) {
            top_[eye] = C3D_RenderTargetCreate(kTopHeight, kTopWidth, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
            if (!top_[eye]) {
                AS3D_ERROR("ctr: cannot create the top render target %d", eye);
                return false;
            }
            C3D_RenderTargetSetOutput(top_[eye], GFX_TOP, eye == 0 ? GFX_LEFT : GFX_RIGHT,
                                      AS3D_DISPLAY_TRANSFER_FLAGS);
        }
        bottom_ = C3D_RenderTargetCreate(kBottomHeight, kBottomWidth, GPU_RB_RGBA8, static_cast<GPU_DEPTHBUF>(-1));
        if (!bottom_) {
            AS3D_ERROR("ctr: cannot create the bottom render target");
            return false;
        }
        C3D_RenderTargetSetOutput(bottom_, GFX_BOTTOM, GFX_LEFT, AS3D_DISPLAY_TRANSFER_FLAGS);
        char buf[96];
        std::snprintf(buf, sizeof buf, "citro3d, New3DS %s", new3ds_ ? "yes" : "no");
        description_ = buf;
        return true;
    }

    bool makeCurrent() override { return true; }
    void swapBuffers() override { endFrame(); }
    int width() const override { return kTopWidth; }
    int height() const override { return kTopHeight; }
    std::string description() const override { return description_; }
    bool pumpEvents() override { return aptMainLoop(); }

    C3D_RenderTarget* topTarget(int eye) const override { return top_[eye ? 1 : 0]; }
    C3D_RenderTarget* bottomTarget() const override { return bottom_; }
    bool beginFrame(u8 flags) override {
        if (inFrame_) return false;
        inFrame_ = C3D_FrameBegin(flags);
        return inFrame_;
    }
    void endFrame() override {
        if (!inFrame_) return;
        C3D_FrameEnd(0);
        inFrame_ = false;
    }
    bool inFrame() const override { return inFrame_; }
    float sliderValue() const override { return osGet3DSliderState(); }
    int eyeCount() const override { return osGet3DSliderState() > 0.02f ? 2 : 1; }
    bool isNew3DS() const override { return new3ds_; }

private:
    C3D_RenderTarget* top_[2] = {nullptr, nullptr};
    C3D_RenderTarget* bottom_ = nullptr;
    bool gfxInit_ = false, c3dInit_ = false, inFrame_ = false, new3ds_ = false;
    std::string description_;
};

} // namespace

CtrContext* ctrContext() { return g_live; }

std::unique_ptr<CtrContext> makeCtrContext() {
    if (g_live) {
        AS3D_ERROR("createGraphicsContext: the 3DS has one context");
        return nullptr;
    }
    std::unique_ptr<Impl> ctx(new Impl());
    g_live = ctx.get(); // before init: the destructor of a failed one clears it
    if (!ctx->init()) return nullptr;
    return ctx;
}

} // namespace ctr

std::unique_ptr<GraphicsContext> createGraphicsContext(const GraphicsConfig&) {
    return ctr::makeCtrContext();
}

} // namespace as3d
