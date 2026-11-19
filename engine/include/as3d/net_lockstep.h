// Delay-based lockstep (docs/spec/issues/180-online-coop.md 4): both peers run the whole
// simulation and only per-frame input crosses the wire. Frames are numbered from 0 at every level
// start (`token` names the start); with input delay d the local input sampled at frame f is the
// record for frame f + d, frames 0..d-1 of both sides are empty records. Frame f is simulated
// when both records for f are present (canStep); otherwise the loop stalls.
//
// Per simulation step the game side (apps/game/net_play.cpp) does:
//     lockstep.setLocal(record);                 // for frame() + delay(); a stall keeps the first
//     if (lockstep.canStep()) { lockstep.takeInputs(host, guest); step the world; hash; }
//     lockstep.buildInputsPacket(packet); transport.sendInputs(packet)
// and feeds every received Inputs packet of the current token to addRemote. No dependency beyond
// as3d_core (the merge into a FrameInput is the game side's, 4.2).
//
// The public members are the contract (T0); the private section is T1's to change.
#pragma once

#include <vector>

#include "as3d/core.h"
#include "as3d/net_protocol.h"

namespace as3d::net {

// Input delay bounds and the host's choice per level from the lobby's ping (median of the last
// five round trips): clamp(ceil((rtt / 2) / 16.67 ms) + 1, 2, 10) (4.1).
constexpr int kMinInputDelay = 2, kMaxInputDelay = 10;
int inputDelayForRtt(double rttMs);

// The sequels' relative mouse steering online (4.2): a direction byte, 0..255 = angle * 256 /
// 2 pi with the angle of atan2(dy, dx) (x right, y up, as FrameInput::mouseDx / mouseDy).
// steerByte is the sending side's only trigonometry and never reaches the simulation; both peers
// turn the byte back into a vector through kSteerTable, so they see the same bits.
u8 steerByte(float dx, float dy);
struct SteerVec {
    float x, y;
};
// (cos, sin) of i * 2 pi / 256, generated once by Python (float32, the shortest literal that
// round-trips; the three axis values below 1e-15 written as 0) and committed: no trigonometry
// in the simulation path.
constexpr SteerVec kSteerTable[256] = {
    {1.0f, 0.0f}, {0.9996988f, 0.024541229f}, {0.99879545f, 0.049067676f}, {0.99729043f, 0.07356457f},
    {0.9951847f, 0.09801714f}, {0.99247956f, 0.12241068f}, {0.9891765f, 0.14673047f}, {0.98527765f, 0.17096189f},
    {0.98078525f, 0.19509032f}, {0.9757021f, 0.21910124f}, {0.97003126f, 0.24298018f}, {0.96377605f, 0.26671275f},
    {0.95694035f, 0.29028466f}, {0.94952816f, 0.31368175f}, {0.94154406f, 0.33688986f}, {0.9329928f, 0.35989505f},
    {0.9238795f, 0.38268343f}, {0.9142098f, 0.4052413f}, {0.9039893f, 0.42755508f}, {0.8932243f, 0.44961134f},
    {0.8819213f, 0.47139674f}, {0.87008697f, 0.4928982f}, {0.8577286f, 0.51410276f}, {0.8448536f, 0.53499764f},
    {0.8314696f, 0.55557024f}, {0.8175848f, 0.57580817f}, {0.8032075f, 0.5956993f}, {0.7883464f, 0.6152316f},
    {0.77301043f, 0.6343933f}, {0.7572088f, 0.65317285f}, {0.7409511f, 0.671559f}, {0.7242471f, 0.68954057f},
    {0.70710677f, 0.70710677f}, {0.68954057f, 0.7242471f}, {0.671559f, 0.7409511f}, {0.65317285f, 0.7572088f},
    {0.6343933f, 0.77301043f}, {0.6152316f, 0.7883464f}, {0.5956993f, 0.8032075f}, {0.57580817f, 0.8175848f},
    {0.55557024f, 0.8314696f}, {0.53499764f, 0.8448536f}, {0.51410276f, 0.8577286f}, {0.4928982f, 0.87008697f},
    {0.47139674f, 0.8819213f}, {0.44961134f, 0.8932243f}, {0.42755508f, 0.9039893f}, {0.4052413f, 0.9142098f},
    {0.38268343f, 0.9238795f}, {0.35989505f, 0.9329928f}, {0.33688986f, 0.94154406f}, {0.31368175f, 0.94952816f},
    {0.29028466f, 0.95694035f}, {0.26671275f, 0.96377605f}, {0.24298018f, 0.97003126f}, {0.21910124f, 0.9757021f},
    {0.19509032f, 0.98078525f}, {0.17096189f, 0.98527765f}, {0.14673047f, 0.9891765f}, {0.12241068f, 0.99247956f},
    {0.09801714f, 0.9951847f}, {0.07356457f, 0.99729043f}, {0.049067676f, 0.99879545f}, {0.024541229f, 0.9996988f},
    {0.0f, 1.0f}, {-0.024541229f, 0.9996988f}, {-0.049067676f, 0.99879545f}, {-0.07356457f, 0.99729043f},
    {-0.09801714f, 0.9951847f}, {-0.12241068f, 0.99247956f}, {-0.14673047f, 0.9891765f}, {-0.17096189f, 0.98527765f},
    {-0.19509032f, 0.98078525f}, {-0.21910124f, 0.9757021f}, {-0.24298018f, 0.97003126f}, {-0.26671275f, 0.96377605f},
    {-0.29028466f, 0.95694035f}, {-0.31368175f, 0.94952816f}, {-0.33688986f, 0.94154406f}, {-0.35989505f, 0.9329928f},
    {-0.38268343f, 0.9238795f}, {-0.4052413f, 0.9142098f}, {-0.42755508f, 0.9039893f}, {-0.44961134f, 0.8932243f},
    {-0.47139674f, 0.8819213f}, {-0.4928982f, 0.87008697f}, {-0.51410276f, 0.8577286f}, {-0.53499764f, 0.8448536f},
    {-0.55557024f, 0.8314696f}, {-0.57580817f, 0.8175848f}, {-0.5956993f, 0.8032075f}, {-0.6152316f, 0.7883464f},
    {-0.6343933f, 0.77301043f}, {-0.65317285f, 0.7572088f}, {-0.671559f, 0.7409511f}, {-0.68954057f, 0.7242471f},
    {-0.70710677f, 0.70710677f}, {-0.7242471f, 0.68954057f}, {-0.7409511f, 0.671559f}, {-0.7572088f, 0.65317285f},
    {-0.77301043f, 0.6343933f}, {-0.7883464f, 0.6152316f}, {-0.8032075f, 0.5956993f}, {-0.8175848f, 0.57580817f},
    {-0.8314696f, 0.55557024f}, {-0.8448536f, 0.53499764f}, {-0.8577286f, 0.51410276f}, {-0.87008697f, 0.4928982f},
    {-0.8819213f, 0.47139674f}, {-0.8932243f, 0.44961134f}, {-0.9039893f, 0.42755508f}, {-0.9142098f, 0.4052413f},
    {-0.9238795f, 0.38268343f}, {-0.9329928f, 0.35989505f}, {-0.94154406f, 0.33688986f}, {-0.94952816f, 0.31368175f},
    {-0.95694035f, 0.29028466f}, {-0.96377605f, 0.26671275f}, {-0.97003126f, 0.24298018f}, {-0.9757021f, 0.21910124f},
    {-0.98078525f, 0.19509032f}, {-0.98527765f, 0.17096189f}, {-0.9891765f, 0.14673047f}, {-0.99247956f, 0.12241068f},
    {-0.9951847f, 0.09801714f}, {-0.99729043f, 0.07356457f}, {-0.99879545f, 0.049067676f}, {-0.9996988f, 0.024541229f},
    {-1.0f, 0.0f}, {-0.9996988f, -0.024541229f}, {-0.99879545f, -0.049067676f}, {-0.99729043f, -0.07356457f},
    {-0.9951847f, -0.09801714f}, {-0.99247956f, -0.12241068f}, {-0.9891765f, -0.14673047f}, {-0.98527765f, -0.17096189f},
    {-0.98078525f, -0.19509032f}, {-0.9757021f, -0.21910124f}, {-0.97003126f, -0.24298018f}, {-0.96377605f, -0.26671275f},
    {-0.95694035f, -0.29028466f}, {-0.94952816f, -0.31368175f}, {-0.94154406f, -0.33688986f}, {-0.9329928f, -0.35989505f},
    {-0.9238795f, -0.38268343f}, {-0.9142098f, -0.4052413f}, {-0.9039893f, -0.42755508f}, {-0.8932243f, -0.44961134f},
    {-0.8819213f, -0.47139674f}, {-0.87008697f, -0.4928982f}, {-0.8577286f, -0.51410276f}, {-0.8448536f, -0.53499764f},
    {-0.8314696f, -0.55557024f}, {-0.8175848f, -0.57580817f}, {-0.8032075f, -0.5956993f}, {-0.7883464f, -0.6152316f},
    {-0.77301043f, -0.6343933f}, {-0.7572088f, -0.65317285f}, {-0.7409511f, -0.671559f}, {-0.7242471f, -0.68954057f},
    {-0.70710677f, -0.70710677f}, {-0.68954057f, -0.7242471f}, {-0.671559f, -0.7409511f}, {-0.65317285f, -0.7572088f},
    {-0.6343933f, -0.77301043f}, {-0.6152316f, -0.7883464f}, {-0.5956993f, -0.8032075f}, {-0.57580817f, -0.8175848f},
    {-0.55557024f, -0.8314696f}, {-0.53499764f, -0.8448536f}, {-0.51410276f, -0.8577286f}, {-0.4928982f, -0.87008697f},
    {-0.47139674f, -0.8819213f}, {-0.44961134f, -0.8932243f}, {-0.42755508f, -0.9039893f}, {-0.4052413f, -0.9142098f},
    {-0.38268343f, -0.9238795f}, {-0.35989505f, -0.9329928f}, {-0.33688986f, -0.94154406f}, {-0.31368175f, -0.94952816f},
    {-0.29028466f, -0.95694035f}, {-0.26671275f, -0.96377605f}, {-0.24298018f, -0.97003126f}, {-0.21910124f, -0.9757021f},
    {-0.19509032f, -0.98078525f}, {-0.17096189f, -0.98527765f}, {-0.14673047f, -0.9891765f}, {-0.12241068f, -0.99247956f},
    {-0.09801714f, -0.9951847f}, {-0.07356457f, -0.99729043f}, {-0.049067676f, -0.99879545f}, {-0.024541229f, -0.9996988f},
    {0.0f, -1.0f}, {0.024541229f, -0.9996988f}, {0.049067676f, -0.99879545f}, {0.07356457f, -0.99729043f},
    {0.09801714f, -0.9951847f}, {0.12241068f, -0.99247956f}, {0.14673047f, -0.9891765f}, {0.17096189f, -0.98527765f},
    {0.19509032f, -0.98078525f}, {0.21910124f, -0.9757021f}, {0.24298018f, -0.97003126f}, {0.26671275f, -0.96377605f},
    {0.29028466f, -0.95694035f}, {0.31368175f, -0.94952816f}, {0.33688986f, -0.94154406f}, {0.35989505f, -0.9329928f},
    {0.38268343f, -0.9238795f}, {0.4052413f, -0.9142098f}, {0.42755508f, -0.9039893f}, {0.44961134f, -0.8932243f},
    {0.47139674f, -0.8819213f}, {0.4928982f, -0.87008697f}, {0.51410276f, -0.8577286f}, {0.53499764f, -0.8448536f},
    {0.55557024f, -0.8314696f}, {0.57580817f, -0.8175848f}, {0.5956993f, -0.8032075f}, {0.6152316f, -0.7883464f},
    {0.6343933f, -0.77301043f}, {0.65317285f, -0.7572088f}, {0.671559f, -0.7409511f}, {0.68954057f, -0.7242471f},
    {0.70710677f, -0.70710677f}, {0.7242471f, -0.68954057f}, {0.7409511f, -0.671559f}, {0.7572088f, -0.65317285f},
    {0.77301043f, -0.6343933f}, {0.7883464f, -0.6152316f}, {0.8032075f, -0.5956993f}, {0.8175848f, -0.57580817f},
    {0.8314696f, -0.55557024f}, {0.8448536f, -0.53499764f}, {0.8577286f, -0.51410276f}, {0.87008697f, -0.4928982f},
    {0.8819213f, -0.47139674f}, {0.8932243f, -0.44961134f}, {0.9039893f, -0.42755508f}, {0.9142098f, -0.4052413f},
    {0.9238795f, -0.38268343f}, {0.9329928f, -0.35989505f}, {0.94154406f, -0.33688986f}, {0.94952816f, -0.31368175f},
    {0.95694035f, -0.29028466f}, {0.96377605f, -0.26671275f}, {0.97003126f, -0.24298018f}, {0.9757021f, -0.21910124f},
    {0.98078525f, -0.19509032f}, {0.98527765f, -0.17096189f}, {0.9891765f, -0.14673047f}, {0.99247956f, -0.12241068f},
    {0.9951847f, -0.09801714f}, {0.99729043f, -0.07356457f}, {0.99879545f, -0.049067676f}, {0.9996988f, -0.024541229f},
};

class Lockstep {
public:
    // `host`: this side flies player 1 (takeInputs's `host` record is then the local one).
    explicit Lockstep(bool host = true) : host_(host) {}
    void setHost(bool host) { host_ = host; }
    bool host() const { return host_; }

    // A level start: frame 0, the delay for this level (kMinInputDelay..kMaxInputDelay), every
    // record and hash of the previous token forgotten, frames 0..delay-1 pre-filled empty.
    void start(u32 token, int delay);
    u32 token() const { return token_; }
    int delay() const { return delay_; }
    // The next frame to simulate.
    u32 frame() const { return frame_; }

    // The local record for frame() + delay(). The first call for that frame wins (the record may
    // already have been sent, so a stall must not change it); false when it was already set.
    bool setLocal(const InputRecord& r);
    // Remote records for firstFrame .. firstFrame + n - 1 from an Inputs packet of the current
    // token (duplicates and frames already taken are ignored), and the packet's ack.
    void addRemote(u32 firstFrame, const InputRecord* records, int n, u32 ack);
    void addRemote(const InputsPacket& p) { addRemote(p.firstFrame, p.records, p.count, p.ack); }
    // Both records of frame() are present.
    bool canStep() const;
    // Frame()'s two records (player 1's, player 2's), then frame() + 1. Only after canStep().
    void takeInputs(InputRecord& host, InputRecord& guest);
    // The Inputs packet of the last kRedundancy local records with this side's ack, encoded
    // (encodeInputs) into `out`; empty when there is nothing to send yet.
    void buildInputsPacket(std::vector<u8>& out) const;
    // The highest frame of ours the remote acknowledged holding (kNoFrame: none), and of its ours.
    u32 remoteAck() const { return remoteAck_; }
    u32 localAck() const;
    // Pacing (4.1): true when this side runs ahead (frame() - remoteAck() > delay() + 2); the
    // loop then skips one step this display frame. True at most once every 10 frames; call it
    // once per display frame.
    bool shouldThrottle();
    // Display frames that could not step for want of the remote record (AS3D_NET stall).
    u32 stalls() const { return stalls_; }
    void noteStall() { ++stalls_; }

    // Desync detection (4.4): this side's hash of `frame` (kept for the last 64 frames) and the
    // remote's (kept until this side has its own). The first mismatch is remembered: desynced().
    void noteLocalHash(u32 frame, u64 hash);
    void checkRemoteHash(u32 frame, u64 hash);
    bool desynced(u32* frame = nullptr, u64* mine = nullptr, u64* yours = nullptr) const;

private:
    bool host_ = true;
    u32 token_ = 0;
    int delay_ = kMinInputDelay;
    u32 frame_ = 0;
    u32 remoteAck_ = kNoFrame;
    u32 stalls_ = 0;
    u32 lastThrottle_ = 0;
    struct Slot {
        u32 frame = kNoFrame;
        InputRecord rec;
    };
    static constexpr int kWindow = 256; // frames held per side (a power of two above any delay + redundancy)
    Slot local_[kWindow];
    Slot remote_[kWindow];
    struct HashSlot {
        u32 frame = kNoFrame;
        u64 hash = 0;
    };
    HashSlot localHash_[64];
    std::vector<HashSlot> remoteHashes_; // received before this side had its own
    bool desync_ = false;
    u32 desyncFrame_ = 0;
    u64 desyncMine_ = 0, desyncYours_ = 0;
};

} // namespace as3d::net
