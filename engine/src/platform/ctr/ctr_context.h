// Private to the platform module and the 3DS app: the citro3d graphics context of the ctr
// backend (as3d::createGraphicsContext returns a CtrContext on the 3DS). Not part of the public
// API: platform.h stays neutral. The frame composer (apps/3ds, M1.8) and the PICA render backend
// (M1.3) reach the render targets and the frame brackets through ctrContext().
//
// Frame protocol (citro3d): beginFrame(); for each target C3D_FrameDrawOn(target) and draw;
// endFrame(). swapBuffers() of the GraphicsContext interface is endFrame().
#pragma once

#include <citro3d.h>

#include <memory>

#include "as3d/platform.h"

namespace as3d {
namespace ctr {

constexpr int kTopWidth = 400, kTopHeight = 240;
constexpr int kBottomWidth = 320, kBottomHeight = 240;

class CtrContext : public GraphicsContext {
public:
    // GraphicsContext: makeCurrent() is true (citro3d has one global context), swapBuffers() ends
    // the frame (C3D_FrameEnd), width()/height() are the top screen's 400x240, pumpEvents() is
    // aptMainLoop() (false once the system asks the app to close).

    // The render targets: top screen left/right eye (400x240 RGBA8 + D24S8; the GPU target is
    // 240 wide and 400 high, the screen is rotated) and the bottom screen (320x240 RGBA8, no
    // depth).
    virtual C3D_RenderTarget* topTarget(int eye) const = 0; // eye 0 = left, 1 = right
    virtual C3D_RenderTarget* bottomTarget() const = 0;

    // Opens the frame (C3D_FrameBegin; SYNCDRAW waits for the vblank before the CPU records) /
    // ends it (C3D_FrameEnd: submits the command buffer and the display transfers of the targets
    // drawn on). beginFrame returns false while a frame is open. endFrame's flags go to
    // C3D_FrameEnd: 0 flushes the whole linear heap before the queue starts, GX_CMDLIST_FLUSH
    // only the command list (every other buffer flushed when written: pica_frame.h kickGpu).
    virtual bool beginFrame(u8 flags = C3D_FRAME_SYNCDRAW) = 0;
    virtual void endFrame(u8 flags = 0) = 0;
    virtual bool inFrame() const = 0;

    // The 3D slider, 0..1 (osGet3DSliderState).
    virtual float sliderValue() const = 0;
    // Top-screen eyes to draw this frame: 2 when the slider is above a small dead zone, else 1
    // (the composer draws the left eye only).
    virtual int eyeCount() const = 0;
    virtual bool isNew3DS() const = 0;
};

// The live context, or null (before createGraphicsContext and after it was destroyed).
CtrContext* ctrContext();

// The reads of the platform streams (the game's paks, openPlatformStream: FSFILE_Read at an
// offset), since start: the load profile's read time (AS3D_LOAD).
struct StreamReadStats {
    u64 ticks = 0;
    u64 bytes = 0;
    u32 reads = 0;     // FSFILE_Read calls (cache misses and large reads)
    u32 cacheHits = 0; // reads served from the block cache
};
const StreamReadStats& streamReadStats();
// The block cache of each platform stream opened from now on (bytes, 64 KB blocks; 0 = none).
// run.ini pak_cache_kb=; default 0.
void setStreamBlockCache(size_t bytes);

} // namespace ctr
} // namespace as3d
