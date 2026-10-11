// The second eye's world from the first eye's command words (round 3, R3.2; docs/3ds-port.md
// "Stereo CPU path (round 3, R3.2)"). 3DS only.
//
// The two eyes of a stereo frame draw the same world: the same records, buffers, textures and
// state, the central view; only the projection differs, and of it only row 1 (the tilted screen's
// horizontal axis), columns 2 and 3 (pica_space.h: A iod / (2 screen) and iod / 2, the sign of iod
// per eye). With w = -z_view (row 3), the right eye's clip y is the left eye's plus
//     k1 * w + k2,   k1 = -(P_R - P_L)[1][2],  k2 = (P_R - P_L)[1][3],
// for every vertex of every world draw. The world shaders add `eye.x * w + eye.y` to their clip y
// (shaders/*.v.pica; eye = (0, 0) otherwise, which leaves y exactly as it was), so:
//   * the left eye records its world passes as usual, with the left projection, between a
//     priming draw (a degenerate triangle that emits the target's frame buffer and everything
//     dirty) and the end of the sprites; at the start of that region every citro3d state flag
//     but the frame buffer and every uniform but `eye` is marked dirty, so the region's first
//     draw emits the whole state: the region depends on nothing recorded before it;
//   * the right eye clears its target, sets eye = (k1, k2), primes (its frame buffer, the eye
//     uniform), and appends a copy of the region's command words to the command list: no culling,
//     no uniform math, no citro3d draw calls. Then everything is marked dirty again (the GPU now
//     holds the region's last state, citro3d its own) and eye = (0, 0).
// What the left eye records must therefore serve both eyes: the terrain culls its chunks against
// both eyes' frusta and the water's tiles are culled for both (eyeReplayShift); every other pass
// already draws the same for both eyes (records culled in prepare with the widened stereo frustum,
// particles and sprites built once per frame). The left eye's picture is unchanged (extra chunks
// and tiles lie outside its frustum); the right eye's vertices are the left eye's transform plus
// the shift, equal to its own projection's up to float rounding.
//
// Not used (both eyes recorded as before) in mono, when the composer does not ask for it
// (run.ini eye_replay=0), in a frame with a state_check GPU read-back (it splits the command list
// inside the region), when the projections differ elsewhere than the two entries, when the
// programs' `eye` uniforms are not one register, or when the command buffer has no room.
#pragma once

#include <cstddef>
#include <cstdint>

#include "as3d/math.h"
#include "as3d/world_render.h"

namespace as3d::pica {

// run.ini eye_replay (default on).
void setEyeReplayEnabled(bool on);
bool eyeReplayEnabled();

// The shift (k1, k2) between two eye projections that differ only in row 1, columns 2 and 3
// (within a relative 1e-6 elsewhere). False otherwise.
bool eyeShiftOf(const Mat4& left, const Mat4& right, float* k1, float* k2);

// The composer, after pica::frameBegin and before the left eye's world: the right eye this frame
// will draw (WorldRenderer::draw arms the replay when the projections allow it).
void eyeReplayRequest(const EyeView& right);
bool eyeReplayRequested(EyeView* right);

// The frame's state (WorldRenderer::draw): recording (the left eye's draw), replaying.
void eyeReplayFrameBegin();               // every frame (pica::frameBegin): nothing recorded
void eyeReplayArm(float k1, float k2);    // the left eye will record for a right eye with this shift
bool eyeReplayArmed();                    // this frame's left eye records for both eyes
// The shift of the armed frame (terrain and water cull for both eyes); false when not armed.
bool eyeReplayShift(float* k1, float* k2);
// The left eye, after its clear: the priming draw and the region's start. False: not recorded.
bool eyeReplayRecordBegin();
void eyeReplayRecordEnd();
// The right eye, after its clear: the replay. False: nothing replayed (draw the eye as usual).
bool eyeReplayRun();
bool eyeReplayRecording();  // between RecordBegin and RecordEnd
bool eyeReplayRecorded();   // a region waits for the right eye

struct EyeReplayStats {
    unsigned armed = 0, recorded = 0, replayed = 0, failed = 0; // since start
    size_t words = 0;                                            // command words copied, since start
    size_t lastWords = 0;                                        // the last region's
    uint64_t replayTicks = 0;                                    // the right eyes' replays (prime, copy, dirty marks)
    const char* lastFailure = "";
};
const EyeReplayStats& eyeReplayStats();

} // namespace as3d::pica
