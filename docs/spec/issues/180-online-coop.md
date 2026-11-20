# 180: Online two-player co-op (peer to peer)

Status: specified (2026-11-19); T0 to T5 landed, the game integration (T4) on 2026-11-20. Ours; the originals had no network play. Affects
a new engine module `engine/src/net/` (`as3d/net_*.h`), `as3d/sim_math.h` and the simulation
sources it replaces libm calls in (`engine/src/game`, `engine/src/script`, `engine/src/core`,
`as3d/math.h`), `as3d/world.h` (`stateHash`), `as3d/frontend.h` + `engine/src/ui/screens_online.cpp`
and the Start Game / in-game screens, `apps/game/{net_play,game_flow,game_session,game_loop,main}.*`,
`apps/web/{web_main.cpp,site/app.js}`, `android/app/src/main/AndroidManifest.xml`,
`tools/fetch_third_party.sh`, the CMake files, tests and docs. The 3DS (`apps/3ds`,
`tools/3ds_build.sh`) is out of scope: it gets the simulation changes for free (`sim_math`,
`stateHash`) and must keep compiling with the `null` net backend.

Two players, each on their own machine (Linux desktop, the web page, or the Android app, any
pair), fly the same co-op mission as if they sat at one screen: the host is player 1, the guest
player 2, with the two-player HUD and rules the games already have (issue 130, as2 7.5). The
owner's hard constraints, all of which this design meets:

* **No infrastructure the owner hosts or pays for, ever.** Peer to peer only. Connection setup by
  copy-paste codes passed through any chat: the host gets an *invite code*, the guest pastes it
  and gets a *reply code* to send back. Free public STUN servers only; **no TURN relay**: a pair
  that cannot connect directly gets a clear "can't connect" message with the port-forward and
  virtual-LAN tips. The web stays plain static files.
* Cross-platform: desktop, web and Android peers play together in any combination.
* The implementers build everything first and test at the very end (section "Tasks": one fast
  smoke test per task, one integration test plan run once at the end).

Sections 1 to 12 are the specification, 13 the tasks, 14 the final test plan, 15 the decisions
taken here and the open questions for the owner.

## 1. Architecture

```
            local devices (keys, every pad, touch, mouse)
                        |
   GameWindow (apps/game/game_loop.cpp) -- fixed 60 Hz loop, bounded catch-up
                        |  local FrameInput of this peer, resolved (mouse-control bits applied)
                        v
   NetPlay (apps/game/net_play.cpp) -- lobby, barrier, flow choices, hashes, status for the UI
        |                                  ^
        | InputRecord for frame f+delay    | ctl messages (Hello, Lobby, StartMission, Ready,
        v                                  |   FlowChoice, Hash, Bye, Ping)
   as3d::net::Lockstep (engine/src/net/lockstep.cpp) -- delay-based lockstep, redundancy, acks
        |                                  |
        v                                  v
   as3d::net::Transport ------ "in" channel (unordered, unreliable) + "ctl" channel (reliable)
        |
        +-- rtc backend: libdatachannel (desktop, Android) / datachannel-wasm (web) = WebRTC
        |      data channels over DTLS/SCTP/ICE with public STUN
        +-- loopback pair (tests, in process, simulated latency and loss)
        +-- null backend (3DS, or AS3D_WITH_NET=OFF): "online play is not in this build"
```

**Deterministic lockstep** (verified against the code): the simulation is a fixed 60 Hz step
(`WorldConfig::dt = 1/60`, `World::step(const PlayerInput&)` in `engine/src/game/level_runtime.cpp`),
seeded by `WorldConfig::seed` into one xorshift32 `Rng` (`as3d/core.h`), and the only input of
a step is `PlayerInput` (`as3d/world.h`): the held `p_action` bits of both players, the hint box's
`confirm`, and the sequels' mouse-steer vector. `as3d::FrameInput` (`as3d/input.h`) is the
game-shell form of the same thing plus a pause edge. Whole missions replay from input scripts
(`as3d_sim --input-script`, `as3d_game --input-script`), and `World::dumpStateJson()`
(`engine/src/game/world_dump.cpp`) writes the state with float bits, used by the determinism
tests (`apps/tests/world_test.cpp` "two runs give the same dump"). So **only per-frame input
crosses the wire**; both peers run the whole simulation. No rollback (World is non-copyable:
script threads, particles, terrain state); input delay absorbs the latency (section 4).

Three corrections to the prior analysis, found in the code:

1. **The pause is not in the input stream today.** With the front end, `GameFlow::step`
   (`apps/game/game_flow.cpp`) drops `FrameInput::pausePressed` ("the front end owns P"); Esc and
   P go through the menus (`Frontend::handlePlayingInput`) to `GameHost::setPaused`, which calls
   `World::setPaused` directly, outside the step. Online, a pause applied on one peer a few frames
   before the other desyncs the worlds. So the pause becomes a **held bit of each peer's input
   record** (section 4.2), applied by both peers at the same frame through the existing
   `pausePressed` edge of `GameSession::step` (which also clears `p_action` on resume, 1.3 state 6).
   The same holds for every other `GameHost` call that touches the world: the hint box's OK
   (`dismissHint`), `clearPlayerActions`, the camera setting (`applySettings` writes
   `world().camera().mode`, and the camera is simulation state: the collision viewport and the
   on-screen bits depend on it), `startMission`. Section 6 says what each does online.
2. **The first game's mouse control is resolved inside `GameFlow::step`** (`mouseControlBits`
   from the pointer into `held[0]`). Online, the local input must be fully resolved *before* it is
   sent, so that resolution moves to a `GameFlow::resolveLocalInput` called by the loop
   (section 6.2). The sequels' relative mouse steering (`FrameInput::mouseDx/Dy`, a float vector
   whose direction only is used: `level_runtime.cpp` normalises it and scales by
   `GameRules::mouseAccel`) is quantised to one byte online (section 4.2).
3. **FMA is a real, measured gap** (section 7.1): a clang `-mfma -ffp-contract=fast` build of
   `as3d_sim` produces 301 fused instructions and its state dump differs from the baseline in 130
   of 196 lines after 3600 frames of mission 1 (AirStrike 3D, bot) and 128 of 224 (AirStrike 2,
   pilot). Today only the script module and the 3DS build set `-ffp-contract=off`; the Android
   arm64 build (clang, where contraction is on by default and the hardware has FMA) fuses
   everywhere else, so an Android peer would desync against a desktop or web peer within seconds.

## 2. Transport

### 2.1 Libraries

| Library | Version (pinned) | Licence | Role |
|---|---|---|---|
| libdatachannel | v0.24.6 (2026-09-26) | MPL-2.0 | WebRTC data channels on desktop and Android: ICE (libjuice), DTLS (Mbed TLS), SCTP (usrsctp) |
| libjuice | v1.7.4 (libdatachannel's `deps/libjuice` at v0.24.6) | MIT | ICE agent with STUN (no TURN needed) |
| usrsctp | the commit libdatachannel v0.24.6 pins in `deps/usrsctp` | BSD-3 | SCTP in user space |
| plog | the commit libdatachannel v0.24.6 pins in `deps/plog` | MIT | libdatachannel's logger |
| Mbed TLS | 3.6.8 (2026-11-16, the 3.6 LTS line) | Apache-2.0 OR GPL-2.0-or-later | DTLS and the certificate fingerprint; chosen over OpenSSL because it builds from source with CMake on all three toolchains with no configure step |
| datachannel-wasm | commit f9200eb942f66ddbfecf088ce7122d095aca1c3e (2026-09-27, no release tags) | MIT | the same `rtc::PeerConnection` / `rtc::DataChannel` C++ API over the browser's `RTCPeerConnection` for Emscripten |

All are compatible with this repository's MIT licence; the release notes and `docs/web.md` /
`docs/android.md` get a "Third-party" line naming them. `NO_MEDIA=ON`, `NO_WEBSOCKET=ON`,
`NO_EXAMPLES=ON`, `NO_TESTS=ON`, `USE_MBEDTLS=ON` for libdatachannel (option names from its
BUILDING.md; the implementer verifies them against the pinned tree). The one C++ API on all three
platforms (`rtc::PeerConnection`, `rtc::DataChannel`, `rtc::Description`, `rtc::Candidate`) means
**one backend source file** `engine/src/net/rtc/transport_rtc.cpp`, with `#ifdef __EMSCRIPTEN__`
only where datachannel-wasm's subset differs (the implementer checks its `wasm/include/rtc`
headers: `localDescription()`, `gatheringState`, `onGatheringStateChange`, `Reliability` are
needed).

**Fetching** (`tools/fetch_third_party.sh net`, into `${AS3D_DATA_ROOT}/third_party_local/`, like
the SDL2, libopenmpt, libarchive and xz trees): libdatachannel's release tarballs do not carry its
submodules, so the script clones `libdatachannel` at tag `v0.24.6` with `--depth 1`, checks
`git rev-parse HEAD` against the commit SHA pinned in the script, then
`git submodule update --init --depth 1 deps/libjuice deps/usrsctp deps/plog` (not libsrtp, not
json) and checks each submodule's SHA against the script's pins. datachannel-wasm the same way at
its pinned commit. Mbed TLS as a release tarball (`mbedtls-3.6.8.tar.bz2` from the GitHub
release) with the pinned SHA-256, in the `fetch_verified` style of the script ("Verified by this
script against the file downloaded from the URL above on <date>"). Each download goes into an
empty directory of its own. The CMake side (`engine/src/net/net.cmake`, like
`engine/src/formats/archive.cmake`) adds the trees with `add_subdirectory` (Mbed TLS with
`ENABLE_TESTING=OFF ENABLE_PROGRAMS=OFF`, libdatachannel with the options above and
`MBEDTLS_INCLUDE_DIR`/libraries pointed at the in-tree build) or, for the web, adds
datachannel-wasm and links its JS library.

**Size impact** (estimates; the final test measures them): desktop static link ~2.5 MB; Android
`libmain.so` +1.5 to 2.5 MB per ABI (libdatachannel ~1 MB, usrsctp ~0.4 MB, Mbed TLS ~0.6 MB,
libjuice ~0.15 MB), so the APK grows by ~4 to 5 MB with two ABIs; the wasm grows by <100 KB
(datachannel-wasm is a thin bridge; the browser does WebRTC). Android needs
`<uses-permission android:name="android.permission.INTERNET"/>` in the manifest (absent today).

### 2.2 Channels: unreliable inputs plus a reliable control channel

Two data channels on one peer connection, both opened by the host (the offerer) with
`negotiated = true` and fixed stream ids so that neither side waits for an `onDataChannel`:

| Channel | id | `rtc::Reliability` | Carries |
|---|---|---|---|
| `in` | 0 | `unordered = true`, `maxRetransmits = 0` | `Inputs` packets, one per local simulation step |
| `ctl` | 1 | reliable, ordered (defaults) | everything else (section 3) |

Why not one reliable ordered channel: a lost packet then blocks every later input for a
retransmission round trip (SCTP head-of-line blocking), a stall of RTT+ for one loss. With an
unordered, unreliable channel and **redundancy** (every `Inputs` packet repeats the last
`kRedundancy = 12` local frames, 200 ms of cover, at 4 bytes per frame), a lost packet costs
nothing: the next one, 16.7 ms later, carries the same frames. The lockstep only stalls when the
frame it needs has not arrived by the time it needs it, which the input delay makes rare.
Bandwidth: a packet is 14 + 12 x 4 = 62 bytes, 60 per second, ~3.7 kB/s each way plus
DTLS/SCTP overhead: nothing.

### 2.3 Signalling by codes

WebRTC needs each side's *session description* (ICE credentials, DTLS fingerprint, candidates)
on the other side. No signalling server: the description travels in the code the players paste
to each other. Trickle ICE is off: each side waits for candidate gathering to complete (public
STUN answered or timed out, at most 5 s) and the code holds every candidate.

`as3d::net::SignalInfo` (`as3d/net_signal.h`) is the set of fields that matter; the code is its
compact binary form, the SDP on either side is rebuilt from it (`sdpFromSignal`) and parsed into
it (`signalFromSdp`), so the code never carries raw SDP:

```
u8   version          kSignalVersion = 1
u8   kind             0 = invite (offer), 1 = reply (answer)
u8   n, n bytes       ice-ufrag (ASCII, 4..32)
u8   n, n bytes       ice-pwd (ASCII, 22..32)
u8   algo             0 = sha-256 (only value); then 32 bytes of fingerprint
u8   setup            0 = actpass (invite), 1 = active (reply), 2 = passive
u16  sctpPort         5000
u8   count            candidates, at most kMaxSignalCandidates = 6
   per candidate:
   u8   kind (bits 0..1: 0 host, 1 srflx, 2 prflx) | bit 4 (0 IPv4, 1 IPv6)
   u32  priority
   16 or 4 bytes       address
   u16  port
u32  check            CRC-32 of everything before it
```

Candidate selection keeps the code short: UDP only (TCP candidates dropped), no loopback, no
IPv6 link-local (`fe80::/10`), server-reflexive first, then host IPv4, then host IPv6, cut at
six. Foundations are regenerated as the candidate's index; `raddr`/`rport` are written as
`0.0.0.0 0` (optional in SDP and not used). A typical invite is 85 to 130 bytes; the worst case
(six candidates, IPv6) ~220 bytes. **Encoding: Crockford base32** (digits and capitals without
I, L, O, U: every menu font draws them, nothing turns into a link or an emoji in chat apps), a
dash every 8 characters for reading; the decoder ignores dashes, spaces, line breaks and case and
maps I/L to 1 and O to 0. A typical code is 140 to 210 characters, worst case ~360. No
compression: the content is random bytes, deflate would not help (measured intent, not a guess:
credentials and fingerprint are 60 random bytes already).

The rebuilt SDP (what `setRemoteDescription` receives, both in libdatachannel and in Chromium
through datachannel-wasm; the implementer checks Firefox and Safari accept it, it is the minimal
data-channel-only description every WebRTC stack produces itself):

```
v=0
o=- <sessionId> 0 IN IP4 127.0.0.1
s=-
t=0 0
a=group:BUNDLE 0
a=msid-semantic: WMS
m=application 9 UDP/DTLS/SCTP webrtc-datachannel
c=IN IP4 0.0.0.0
a=mid:0
a=ice-ufrag:<ufrag>
a=ice-pwd:<pwd>
a=fingerprint:sha-256 <AA:BB:...>
a=setup:<actpass|active|passive>
a=sctp-port:5000
a=max-message-size:262144
a=candidate:<i> 1 udp <priority> <ip> <port> typ <host|srflx raddr 0.0.0.0 rport 0> generation 0
a=end-of-candidates
```

`sessionId` is the first 8 bytes of SHA-256(ufrag) as decimal, so both sides are deterministic
functions of the code. The parser (`signalFromSdp`) reads `a=ice-ufrag`, `a=ice-pwd`,
`a=fingerprint`, `a=setup`, `a=sctp-port` and `a=candidate` lines wherever they appear (session or
media level) and ignores everything else; candidates may also be passed separately (the
`onLocalCandidate` form).

Flow: host `Transport::hostOffer()` -> gathering -> `localSignal()` -> invite code; guest
`joinOffer(remote)` -> gathering -> reply code; host `acceptAnswer(remote)` -> ICE connects both
ways -> DTLS -> channels open -> `Connected`. The invite code is an offer of `setup:actpass`
(the host ends up the DTLS server; the guest's answer says `active`), the host is the ICE
controlling agent. Codes expire: an ICE agent that waits more than 10 minutes for the reply is
closed ("The invite code expired; make a new one"). A code is single-use by construction (the
credentials die with the agent).

**STUN**: `stun:stun.l.google.com:19302` and `stun:stun.cloudflare.com:3478`, both given to the
agent (the first that answers wins); `AS3D_STUN` (desktop and Android, through
`LoopOptions::stunServers`), `?stun=` (web) override, `none` disables (loopback tests). No TURN.
When ICE fails or stays `checking` for `kConnectTimeout = 20 s` the session ends with the
"can't connect" message of section 9. Symmetric NAT on both sides is the usual cause; one open
side suffices.

**LAN discovery** (the bonus, not required): not in v1. Codes work on a LAN as they are (host
candidates are in them).

### 2.4 The `Transport` interface (engine, no sockets in the header)

```cpp
// as3d/net_transport.h (as3d::net)
struct TransportConfig {
    std::vector<std::string> stunServers; // "stun:host:port"; empty = none
    bool verboseLog = false;              // AS3D_NET lines for every state change
};
class Transport {
public:
    enum class State { New, Gathering, SignalReady, Connecting, Connected, Failed, Closed };
    virtual ~Transport() = default;
    virtual void hostOffer() = 0;                           // host: start gathering an offer
    virtual void joinOffer(const SignalInfo& invite) = 0;   // guest: set the offer, gather an answer
    virtual void acceptAnswer(const SignalInfo& reply) = 0; // host: set the answer
    virtual bool localSignal(SignalInfo& out) const = 0;    // true once gathering completed
    virtual void sendCtl(const u8* data, size_t n) = 0;     // reliable, ordered; at most kMaxCtlMessage bytes
    virtual void sendInputs(const u8* data, size_t n) = 0;  // unreliable, unordered; at most kMaxInputsMessage bytes
    virtual bool pollCtl(std::vector<u8>& out) = 0;         // one message, in order; false when none
    virtual bool pollInputs(std::vector<u8>& out) = 0;      // one packet; false when none
    virtual void update() = 0;                              // once per display frame on the game thread
    virtual State state() const = 0;
    virtual std::string error() const = 0;                  // why Failed / Closed
    virtual void close() = 0;
};
constexpr size_t kMaxCtlMessage = 1024, kMaxInputsMessage = 128;
// The rtc backend (libdatachannel, datachannel-wasm). The null backend returns nullptr and
// sets *error to "online play is not in this build".
std::unique_ptr<Transport> makeRtcTransport(const TransportConfig& config, std::string* error);
// Two transports joined in process, for tests: `latencyMs` each way, `lossPercent` on the
// inputs channel only (ctl never loses), both deterministic from `seed`. The codes still go
// through hostOffer / localSignal / joinOffer / acceptAnswer (fake SignalInfo), so the lobby
// code path is the real one.
struct LoopbackOptions { int latencyMs = 0; int lossPercent = 0; u32 seed = 1; };
void makeLoopbackPair(std::unique_ptr<Transport>& a, std::unique_ptr<Transport>& b, const LoopbackOptions& o);
```

The rtc backend's callbacks run on libdatachannel's threads (on the web, on the main thread):
they only push into mutex-guarded queues; `pollCtl`/`pollInputs`/`update` run on the game thread.
Nothing in `net` depends on SDL, GL or the game modules; `as3d_net` links `as3d_core` only, and
the rtc backend the third-party targets.

## 3. Protocol (the `ctl` channel and the `Inputs` packet)

All integers little-endian, strings as `u8 length + bytes` (ASCII/UTF-8, at most 64 bytes),
first byte = message type. Every decoder is bounds-checked and returns false on anything odd;
a bad message ends the session with "protocol error" (section 9). Unknown types are ignored
(forward compatibility within a protocol version).

| Type | Name | Direction | Payload |
|---|---|---|---|
| 0x01 | `Hello` | both, first message | `u16 protocolVersion` (= `kNetProtocolVersion` = 1), `str buildId` (`AS3D_BUILD_ID`), `u8 platform` (0 desktop, 1 web, 2 android), `str gameKey`, `u8[32] simFingerprint` (section 5.3), `u8 role` (0 host, 1 guest) |
| 0x02 | `Bye` | both | `u8 reason` (0 quit, 1 lobby left, 2 protocol error, 3 desync, 4 timeout), `str text` |
| 0x03 | `Ping` | both, every 1 s | `u32 seq`, `u32 sentMs` (sender's clock) |
| 0x04 | `Pong` | both | the `Ping`'s `seq` and `sentMs` echoed |
| 0x10 | `Lobby` | host -> guest, on every change | `u8 mission` (0-based), `u8 difficulty`, `u8 hostHeli`, `u8 guestHeli` (as the host sees it), `u8 godMode` |
| 0x11 | `LobbyGuest` | guest -> host, on every change | `u8 heli`, `u8 ready` |
| 0x12 | `StartMission` | host -> guest | `u32 token`, `u32 seed`, `u8 inputDelay`, `u8 camera`, `u8 godMode`, then a `MissionStart` (`u8 mission, u8 difficulty, u8 players, u8 heli[2], u8 lives[2], i64 banked[2], u8 restart, u8 carryUpgrades, u8 upgrades[2][kMaxWeaponSlots], u8 weapon[2], f64 rankAccumulator[2]` as the `as3d::ui::MissionStart` fields) |
| 0x13 | `Ready` | both | `u32 token`: my level is loaded, I am at frame 0 of it |
| 0x14 | `FlowChoice` | host -> guest | `u8 choice`: 0 Next, 1 Restart, 2 Continue (checkpoint), 3 Quit to menu, 4 StartCampaign (from the lobby) |
| 0x20 | `Hash` | both | `u32 token`, `u32 frame`, `u64 hash` |
| 0x21 | `Desync` | both | `u32 token`, `u32 frame`, `u64 mine`, `u64 yours` |

`Inputs` (0x30, the `in` channel): `u32 token`, `u32 ack` (the highest frame for which the sender
holds the other side's input with no gap below it), `u32 firstFrame`, `u8 count` (1..`kRedundancy`),
then `count` x `InputRecord`:

```cpp
struct InputRecord {           // 4 bytes on the wire
    u16 held = 0;              // p_action bits of this peer's player (10 bits used)
    u8 flags = 0;              // kConfirm = 1 (hint OK), kPause = 2 (held while this peer pauses), kSteer = 4
    u8 steer = 0;              // with kSteer: the mouse-steer direction, 0..255 = angle * 256 / 2 pi
};
```

Packets of a `token` other than the current level's are dropped (the barrier, section 4.3).

**Versioning**: `kNetProtocolVersion` (u16) is checked first; a mismatch ends with "different
version" (section 9). Then `buildId` = `AS3D_BUILD_ID`, a compile definition set by the root
`CMakeLists.txt` from `git describe --tags --always --dirty` at configure time (every platform
build configures through it: the desktop, Android via `add_subdirectory`, the web via its own
project that adds the engine; "unknown" without git). Two different builds may simulate
differently (any change in the simulation sources), so by default the session is refused unless
both ids are equal; `AS3D_NET_ANY_BUILD=1` (desktop, Android intent extra `net_any_build`) or
`?netanybuild=1` (web) turns the check into a warning line, for development between a desktop
build and a web build of the same tree (their ids are equal when built from the same commit
without local changes, which is the normal case). Then `gameKey` must be equal (the Online screen
lives inside one game's menus), then the data fingerprint (section 5.3).

## 4. The lockstep loop

### 4.1 Frames, delay, who flies whom

**The host is player 1, the guest player 2**, always (the HUD sides and the two-player rules
follow, and the lobby needs no side choice). Each peer's local devices are merged into *one*
player's input the way a one-player game merges them today (`keys_` player-1 bindings, every pad
ORed through `pad_`, touch, the mouse): `PlayerPads` and the Players screen are not used online.
On the guest, the local `held[0]` becomes the record sent as player 2.

The lockstep (`as3d::net::Lockstep`, `engine/src/net/lockstep.cpp`) numbers simulation frames
from 0 at every level start (`token` identifies the level start). With input delay `d`, the local
input sampled at local frame `f` is the record for frame `f + d`; frames `0..d-1` of both sides
are pre-filled with empty records. Frame `f` is simulated when both records for `f` are present
(`canStep()`); otherwise the loop **stalls** (no step this display frame; the accumulator is
clamped to one step so the stall is not replayed as a burst when input arrives; `kMaxCatchUp = 5`
still bounds a catch-up). The HUD shows "Waiting for the other player..." after 0.5 s of stall
(section 8.5).

`d` is chosen by the host at every level start from the lobby's ping (median of the last 5
`Ping` round trips): `d = clamp(ceil((rtt / 2) / 16.67 ms) + 1, 2, 10)` (RTT 30 ms -> 2 frames =
33 ms, 100 ms -> 4 frames = 67 ms, 250 ms -> 9 frames), carried in `StartMission`, fixed for
that level (no mid-level change in v1: a change would need its own agreed frame; the per-level
choice already adapts). The pre-filled frames mean the first `d` frames after the barrier ignore
input, which no one notices.

**Pacing**: each `Inputs` packet acks the highest contiguous remote frame held. When
`localFrame - remoteAck > d + 2`, the remote is behind (its machine is slower or its frames are
being dropped by the page's throttling): `shouldThrottle()` tells the loop to skip one step this
display frame, at most once every 10 frames, so the faster side drifts back instead of stalling
hard every few seconds. No clock synchronisation beyond that: both sides run their own 60 Hz
accumulator, and the stall is the only hard constraint.

### 4.2 What goes into a record, and the merge

Per local step, `NetPlay` builds the local `InputRecord` from the resolved local `FrameInput`
(section 6.2):

* `held` = `in.held[0]` (10 bits; the Confirm and Pause actions have no bits).
* `kConfirm` while `confirmRequested_` (set by the hint box's OK, section 6.3; held for 1 frame).
* `kPause` while this peer holds the pause: the in-game menu open, P pressed, the sequels' start
  and end dialogues, the app in the background, the web page hidden or out of full screen on a
  phone. The world is paused at frame `F` **iff** either side's record for `F` has `kPause`.
* `kSteer` + `steer` when the sequels' relative mouse steering is live
  (`GameFlow::relativeMouseActive()`) and the motion this step is not zero: `steer =
  round(atan2(dy, dx) * 256 / 2pi) & 255` (this one `atan2` is on the sending side only and never
  reaches the simulation).

The merge into the `FrameInput` both peers give `GameFlow::step` for frame `F` (host record `h`,
guest record `g`):

* `held[0] = h.held`, `held[1] = g.held`;
* `confirm = h.confirm || g.confirm`;
* `pausePressed = ((h.pause || g.pause) != world.paused())` while no hint box and no level-end
  state owns the pause (the same guard `GameSession::step` applies): the edge the session already
  turns into `World::setPaused` plus the `p_action` clear on resume. Both sides compute it from
  the same records and the same world, so it is deterministic;
* `mouseSteer = h.steer present`, `mouseDx/Dy = kSteerTable[h.steer]`: a `constexpr` table of 256
  `(cos, sin)` float literals in `as3d/net_lockstep.h` (generated once, committed as literals, so
  no trigonometry runs in the simulation path and both sides see the same bits). Only player 1
  steers by mouse in the sequels (`level_runtime.cpp`: `p == 0`), so only the host's byte is used.

### 4.3 The start barrier

A level start (`StartMission`, section 6.4) carries a new `token`. Each side loads its level
(seconds, behind the loading screen), then sends `Ready(token)` from `GameFlow::levelLoadedHook`.
`NetPlay` steps the lockstep only once it has sent *and* received `Ready` for the current token;
meanwhile the loaded level is drawn frozen with "Waiting for the other player..." (the slow
loader never falls behind: nothing is simulated before both are ready). `Inputs` and `Hash` of an
old token are dropped. The barrier also covers Restart and Next.

### 4.4 Desync detection

`World::stateHash()` (new, `engine/src/game/world_hash.cpp`): FNV-1a 64 over the simulation's
cheap identifying state, in a fixed order: `frame_`, the RNG state, `time_` bits, `mapPos_` bits,
`levelComplete_`, `gameOver_`, `paused_`, `hintShowing_`; per player record: origin bits, health,
score bits, lives, `action`, weapon, upgrades, missile and power-up counts; the count of entities
in use; per in-use entity in slot order: origin bits, angles bits, health bits, state and flags;
`particles().liveCount()` and the particle hash `world_dump.cpp` already computes
("particles": "hash"). A few microseconds. `World::stateHashFull()` = FNV-1a of
`dumpStateJson()` (slow, for the tests' `--net-hash-full`).

Every `kHashEvery = 20` frames (after stepping a frame `F` with `F % 20 == 0`) each side sends
`Hash(token, F, h)` on `ctl` and keeps its last 64 own hashes by frame. On a remote hash whose
frame it has: compare; on a mismatch both sides (the one that saw it sends `Desync`, the other
reacts to the message) write `dumpStateJson()` to
`<gameDataDir(key)>/desync-<token>-<frame>-<host|guest>.json` (the current state, within 20
frames of the divergence: enough to diff, with the entity list and particles), log
`AS3D_DESYNC token=T frame=F mine=H yours=H` and end the session with the message of section 9.
Both dumps together are the bug report (the player sends theirs through the same chat).

## 5. Lobby, data identity and the session life cycle

### 5.1 States of `NetPlay` (apps/game/net_play.h)

```
Idle -> (Host) Gathering -> InviteReady (code shown) -> WaitingReply -> Connecting -> Handshake -> Lobby
     -> (Join) PasteInvite -> Gathering -> ReplyReady (code shown) -> Connecting -> Handshake -> Lobby
Lobby -> Loading (StartMission sent/received) -> Barrier (Ready) -> Playing -> (level end) -> Lobby-less
        level flow (section 6.4) -> Playing ... -> Ended (message) -> Idle
Any state -> Failed (message) -> Idle
```

`Handshake`: both send `Hello` as soon as `ctl` opens; the checks of section 3 in order; the
host's `Lobby` message follows its `Hello`.

### 5.2 The lobby

The host decides mission, difficulty and god mode (tests); each side picks its own helicopter
among the helicopters **its own profile** has unlocked (the guest's choice is sent in
`LobbyGuest`); the mission must be unlocked in the **host's** profile only (a guest may join a
friend's later mission; it unlocks in the guest's profile when completed, section 5.4). The host's
initial values are what Start Game held when "Online" was chosen. The guest toggles Ready; the
host's Start is enabled while the guest is Ready. Start sends `FlowChoice(StartCampaign)` then
`StartMission` (section 6.4). Either side's Back sends `Bye(lobby left)` and closes.

### 5.3 Same game data: the simulation fingerprint

The paks' SHA-256 (`tools/games.json` `pak_sha256`, `as3d/game_import.h` `KnownPakHash`) is the
wrong key: copies that differ only in textures or in the pak layout (the gametop repack, the
nVidia pak1, a copy without pak3) simulate alike, and hashing 100 MB from the web's IndexedDB
file system takes seconds. Instead `as3d::simDataFingerprint(Vfs&)` (`as3d/sim_fingerprint.h`,
`engine/src/game/sim_fingerprint.cpp`): SHA-256 (`as3d/sha256.h`) over the mounted files that can
reach the simulation, in sorted path order, each as `path, u32 size, bytes`: every entry of
`vfs.list("")` **except** those whose extension is one of the media types
`tga bmp jpg jpeg png dds wav ogg mp3 xm it s3m mod mo3 mid ttf fnt avi bik` (the implementer
confirms the list against what the paks hold, `tools/pak_entries/`; anything unknown stays in),
and every file at the root (the paks hold none; the root of an extracted data directory holds the
executable's texts file, which made a desktop playing from `assets_extracted/` refuse every peer
playing from the paks: found by the final test).
Scripts, object definitions, weapons, particle systems, levels.txt, maps and models (collision
data) are in. Computed once per game session when the Online screen first opens (the screen says
"Checking game files..." for the second it takes; ~20 to 40 MB read) and cached in `NetPlay`.
Sent in `Hello`; a mismatch ends with the "game files differ" message. The fingerprint is logged
(`AS3D_NET fingerprint=...`) so two players can compare by eye.

### 5.4 Profiles and saves (decision: keep it simple)

Each peer runs the same front-end campaign on its own `Profile`: `Frontend::onEndLevel` runs on
both (the reports are identical, the simulation is), so **both profiles get the unlocks** of a
completed mission (the next mission, the helicopter) and the sequels' checkpoint, and each side's
Game Over / Game Complete name entry and high-score table are its own (the score is the shared
co-op score the games already compute for two players). The guest's profile gets exactly what a
local co-op's single profile would, and so does the host's. No online statistics, no shared
tables. A guest whose checkpoint predates this session only matters for the sequels' "Continue"
on the very first game over: the host's `StartMission` carries lives, score and rank, so the
world is right even then (the guest's front-end campaign counters may differ from the host's
until the next level end; accepted).

## 6. Game integration (`apps/game`)

### 6.1 `NetPlay`

`apps/game/net_play.{h,cpp}` (not compiled on the 3DS; `apps/game/CMakeLists.txt` adds it with
`AS3D_WITH_NET`): owns the `Transport`, the `Lockstep`, the fingerprint, the lobby state, the
pings; implements `as3d::ui::OnlineHost` (section 8.1) for the front end and `OnlineFlowHooks`
(below) for `GameFlow`; the loop calls `update()` once per display frame (transport polling, ctl
messages, timeouts) and `beforeStep()/afterStep()` around each simulation step.

```cpp
class NetPlay : public as3d::ui::OnlineHost {
public:
    struct Options { std::vector<std::string> stunServers; bool anyBuild = false; bool hashFull = false;
                     int hashEvery = as3d::net::kHashEvery; std::string codeOutPath, replyInPath; /* tests */ };
    NetPlay(GameSession& session, GameFlow& flow, const Options& o);
    void setTransportFactory(std::function<std::unique_ptr<as3d::net::Transport>(std::string*)> f); // tests: loopback
    void update(double nowSeconds);          // per display frame
    bool active() const;                     // a session is on (lobby or playing)
    bool playing() const;                    // a level is running under the lockstep
    // The loop's step gate: true when frame() can be simulated; `merged` is the FrameInput to step.
    bool prepareStep(const as3d::FrameInput& localResolved, as3d::FrameInput& merged);
    void afterStep();                        // hashes, the next Inputs packet
    bool throttle() const;                   // skip one step this display frame
    bool stalled() const; double stalledSeconds() const;
    // GameFlow hooks (section 6.3, 6.4)
    void localPause(bool on); void requestConfirm();
    bool interceptStartMission(const as3d::ui::MissionStart& ms, as3d::ui::MissionStart& use, as3d::u32& seed, int& camera); // host: sends; guest: waits for the host's
    void levelLoaded();                      // sends Ready
    void flowChoice(as3d::ui::OnlineFlowChoice c); // from the front end's buttons (host) or Quit (guest)
};
```

`AS3D_NET` log markers (through `AS3D_INFO`, with `--markers` as the others): `AS3D_NET state=S`,
`AS3D_NET code kind=invite|reply chars=N`, `AS3D_NET connected rtt_ms=R platform=P build=B`,
`AS3D_NET start token=T seed=S delay=D mission=M`, `AS3D_NET ready token=T`, `AS3D_NET stall ms=M`
(when a stall ends, if over 100 ms), `AS3D_NET hash frame=F h=H` (every hash sent),
`AS3D_DESYNC ...`, `AS3D_NET end reason=R`.

### 6.2 `GameWindow` (game_loop.cpp) and the local input

`LoopOptions` gets `online` (`enum { Off, Host, Join }` for the command line), `stunServers`,
`netAnyBuild`, `netHashFull`, `netCodeOut`, `netReplyIn`, `netInviteIn`, `netCodeOut` (tests,
section 14). `GameWindow` owns a `NetPlay` beside `GameFlow` when `AS3D_WITH_NET`. In
`simulate()`, when `net_->playing()`:

```
local = keys_.takeFrame() | pad_.takeFrame() (every pad) | touch | bot/script (source_)
stack_.flow->resolveLocalInput(local)      // mouse-control bits into held[0]; relative-mouse motion kept
if (!net_->prepareStep(local, merged)) { stall: clamp acc_ to dt_; break; }
ev = stack_.flow->step(merged, /*fromNet=*/true)
net_->afterStep()
```

`GameFlow::resolveLocalInput(FrameInput&)` is the code `GameFlow::step` runs today before the
session step (the `!playing()` clear, the first game's `mouseControlBits`, the relative-mouse
flag); offline `step()` calls it itself, so offline behaviour is unchanged. With `fromNet` the
pause edge is kept and the resolution skipped. Bot runs online (`--bot`): the host takes
`botInputPlayers(world, frame).held[0]`, the guest `.held[1]` (the two-pilot bot of
`as3d/input.h`; a pure function of the world and the frame, identical on both sides).

The AS3D_INPUT marker keeps logging the merged input. `AS3D_GAME_FRAME` unchanged.

### 6.3 `GameFlow` online behaviour (the `GameHost` calls that touch the world)

`GameFlow::setNetPlay(NetPlay*)`; `online()` is true while a session is active. Then:

| Call | Offline (today) | Online |
|---|---|---|
| `setPaused(true)` | `World::setPaused(true)` | `net->localPause(true)`; the world pauses when both sides reach the frame carrying it (a few frames: the menu is already up, nobody notices) |
| `setPaused(false)` | `dismissHint` if a hint shows, else `setPaused(false)` | if a hint shows: `net->requestConfirm()`; else `net->localPause(false)` |
| `clearPlayerActions` | clears `p_action` | nothing (the merged pause edge does it in `GameSession::step`, on both sides at the same frame) |
| `settingsChanged` / `applySettings` | volumes, keys, **camera mode** | volumes, keys; the camera stays the host's (`StartMission::camera`; the Options camera spinner shows "(host's)" and F9 is ignored, section 8.5) |
| `startMission(ms)` | loads | host: `interceptStartMission` sends `StartMission` (seed from `std::random_device` unless `--seed`, the delay from the ping, the host's camera and god mode) and loads with those; guest: loads with the *host's* message (its own `ms` is logged if it differs) |
| `loadAttract` | loads the attract level | after a session ends only (the main menu); never during a session |
| `typed(c)` (cheats) | may change the world | ignored online (cheats would desync; the god-mode flag is the host's, in `StartMission`) |
| `onBackground` | opens the in-game menu | the same; `localPause(true)` follows from `setPaused(true)` |
| `quit`, `changeGame` | | end the session first (`Bye`) |

`GameFlow::step(in, fromNet)`: keeps `in.pausePressed` when `fromNet`; the hint-box confirm path
is unchanged (`confirm && menuOpen && topScreen == Hint -> fe_->update(Enter)` closes the box on
both sides, since both records carry the merged `confirm`). The `Hint` screen's own OK/Enter is
redirected by the front end online (section 8.3) to `OnlineHost::requestConfirm()` instead of
`resumePlay()`.

`GameSession::LevelSetup` gets `u32 seed = 0` (0: the session's `WorldConfig::seed`) and
`int cameraOverride = -1`, `int godMode = -1` (-1: as configured); `startMission` applies them to
the `WorldConfig` it re-initialises the world with. `GameSession` exposes
`as3d::u64 stateHash() const` and `stateHashFull()`.

### 6.4 Level flow: the host chooses, the guest follows

The Mission Complete, Game Complete, Game Over screens and the in-game menu run on both sides
(same events, same reports). Online, the **host's** Next / Restart / Continue / Quit do what they
do today *and* send `FlowChoice`; the **guest's** screens show "Waiting for the host..." in place
of those buttons (Quit stays: it ends the session for both). On `FlowChoice` the guest's front
end runs the same action (`Frontend::onlineApplyFlow`, section 8.1): Next ->
`continueCampaign()`, Restart -> `startLevel(true)`, Continue -> the checkpoint start, Quit ->
`quitToMainMenu(bank as the in-game Quit does)` then the session ends with "The other player
left" on the guest only if the host chose Quit (the host sees no notice). The guest's own
`startMission` call that follows is intercepted and replaced by the host's `StartMission`
message, which the host sends right after `FlowChoice` on the ordered channel (so it is there, or
arrives within the round trip; the guest's `GameFlow` holds a pending start until it does). Every
start carries a new `token` and goes through the barrier.

A `Bye` from either side, a transport failure, a 10 s silence (no `Inputs`, no `Pong`) or a
desync ends the session: the world is dropped, both front ends go to the main menu
(`Frontend::onlineEnded(message)`: `quitToMainMenu(false, false)`, the attract level, then the
notice screen with the message of section 9). Scores of an unfinished level are not banked
(as a Quit).

## 7. Determinism across desktop, web and Android

### 7.1 Measured: FMA (section 1 item 3)

Experiment (this spec, 2026-11-19, laptop x86-64): `apps/sim_tool` configured standalone with
`clang++ -mfma -ffp-contract=fast` (`AS3D_WITH_ARCHIVES=OFF`), against the ordinary `build/`
`as3d_sim` (gcc, no `-mfma`: no fusion). `--game as3d --level 1 --frames 3600 --bot --dump-state`:
130 of 196 dump lines differ (the camera and particle hashes among them); `--game as2 --level 1
--frames 3600 --pilot`: 128 of 224. `objdump` counts 301 `vfmadd/vfnmadd/vfmsub` in the fused
binary. The Android arm64 build is clang with contraction on by default on a target that has FMA:
**it fuses today** (the 3DS build sets `-ffp-contract=off` explicitly, the script module too; the
rest of the engine does not), so the measured divergence is what an Android peer would show.
x86-64 (no `-mfma`) and wasm (no FMA instruction without relaxed-simd) do not fuse, which is why
desktop and web agree today.

**Fix**: `add_compile_options(-ffp-contract=off)` in the root `CMakeLists.txt` (covers the
engine, apps, tests, and the Android build, which adds the root) and in `apps/web/CMakeLists.txt`
(its own project). Desktop and web end states do not change (they never fused), so the expected
numbers (`testdata/golden/*/expected.json` `bot_regression`) stay; the 3DS already has it.

### 7.2 libm: the last bit differs between C libraries

The simulation calls transcendental functions at ~60 sites in `engine/src/game` (19 `sin`, 14
`cos`, 9 `atan2`, 7 `fmod`, 4 `atan`, 3 `acos`, 2 `tan`, 1 `exp`, 1 `log`; `builtins_math.cpp`,
`builtins_sequel.cpp`, `builtins_vector.cpp`, `game_camera.cpp`, `level_runtime.cpp`,
`object_tree.cpp`, `particles.cpp`, `terrain.cpp`, `water.cpp`, `waypoint.cpp`, `world_path.cpp`,
`world_think.cpp`, `world_internal.h`), in `engine/src/core/core.cpp`, and inline in
`as3d/math.h` (the rotation matrix builders and `anglesToAxis`, ~10 sites, used by the simulation
and the renderer alike). `sqrt`, `floor`, `ceil`, `trunc` and `fmod` are exact (correctly rounded
or exact by IEEE 754) on every platform: nothing to do for those (18 `sqrt`, 13 `floor`, 1
`trunc`, 7 `fmod`). The others are not: glibc (desktop) uses its own correctly-rounded-in-practice
implementations, Bionic (Android) the ARM optimized-routines for the float variants, Emscripten
musl's (FreeBSD msun), and they differ in the last bit for some inputs. A one-ulp difference in a
`sin` of an angle feeds a position, which changes a collision frames later: the desync is
certain, only its moment varies. The script VM itself (`engine/src/script`) and the Python
reference interpreter (`tools/ref/rcsl_vm.py`) use no libm, so the bit-for-bit VM trace tests are
untouched; the script *builtins* (`builtins_math.cpp`) do.

**Fix**: `as3d/sim_math.h` + `engine/src/core/sim_math.cpp`: `simSinf, simCosf, simTanf,
simAtanf, simAtan2f, simAcosf, simAsinf, simExpf, simLogf, simPowf` and the double variants used
(`simSin, simCos, simTan, simAtan, simAtan2, simAcos, simExp, simLog`), ported from musl (MIT;
FreeBSD msun lineage: float functions computed through double polynomial kernels and
`__rem_pio2f`, double ones through `__rem_pio2` and the `__sin/__cos/__tan` kernels). They are
plain IEEE double arithmetic: identical on x86-64 (SSE2, no x87), arm64 and wasm once contraction
is off (7.1), and on the 3DS (ARM32 VFP, double is IEEE). The port keeps musl's code shape and
constants; it carries the MIT notice. Every libm call in the files above is replaced (the
renderer may call them too, harmless); a grep in the tests
(`apps/tests/sim_math_test.cpp`: no `std::sin|cos|tan|atan|acos|asin|exp|log|pow\(` left in
`engine/src/game`, `engine/src/script`, `engine/src/core`, `as3d/math.h`, `as3d/vec.h`, enforced by
a test that greps the sources from `AS3D_REPO_ROOT`) guards against regressions.

**Expected states change once**: the results differ from glibc's in the last bit for some
arguments, so the desktop's end states will move (the dump-vs-dump tests compare two runs of the
same binary and stay green; `bot_regression` end frame / score / lives in the three
`expected.json` may change, as may the pilot numbers in `docs/missions-status*.md`). This is the
deliberate one-time change: the implementer runs the bot regression and the two-player soak,
updates the expected numbers in the same commit, and the commit message and the changelog say
so. The 3DS soak comparison (`tools/3ds_soak_compare.py`, 3DS vs desktop `as3d_sim`) keeps
working: both sides use `sim_math`.

Known-answer test: `sim_math_test.cpp` checks ~200 inputs per function (ranges the game uses:
degrees to radians, small and large angles, negative, near pi/2 multiples) against hex bit
patterns computed once on the desktop and committed in the test, and the same table is hashed by
`as3d_web --simmath-check` (the page's `?simmath=1` prints `AS3D_SIMMATH hash=...` to the console)
so the final test compares desktop and wasm without an Android device (section 14).

### 7.3 The rest of the audit (the determinism task checks and fixes)

* **Sorting with ties**: `std::sort` is not stable and libstdc++ (desktop) and libc++ (Android,
  Emscripten) order equal keys differently. In the simulation: `engine/src/game/world_particles.cpp:131`
  sorts `candidates_` by a comparator (check for ties: add a slot-index tie-break),
  `spawn.cpp:13` is `stable_sort` (fine); the `defs.cpp`/`game_data.cpp` sorts are on names at
  load (total orders, fine). Rule: no `std::sort` on a non-total key in the simulation.
* **Hash containers**: `unordered_map`/`unordered_set` iteration order differs between standard
  libraries. `builtins_table.cpp`, `object_tree.cpp`, `script_host.cpp`, `defs.cpp`, `precache.cpp`
  use them: each must be lookup-only, or iterated only for output that does not feed the
  simulation (the task reads each; any iteration that spawns, orders or counts becomes a
  vector or a `std::map`).
* **float -> int of out-of-range or NaN** is UB and differs by architecture (x86 gives
  `INT_MIN`, arm64 saturates, wasm saturates with Emscripten's default). The task greps
  `static_cast<int>`/`(int)` of floats in the simulation and clamps where the range is not
  provably safe (the engine's `ftol` helper, if any, becomes the single place).
* **Uninitialised reads**, pointer comparisons as keys, `float` promoted through `long double`
  (none expected: `-fno-exceptions`, no x87), denormals (flush-to-zero is off by default in user
  space on all three; nothing to do), `std::string`/`std::vector` capacities as behaviour (none).
* **Compiler flags**: no `-ffast-math` anywhere (checked); `-O2` vs `RelWithDebInfo` do not
  change IEEE results with contraction off. The web build is `Release`, fine.
* The residual risk (compiler bugs, an audit miss) is what the desync detector of 4.4 is for: it
  turns a silent divergence into a report with two dumps.

Correction (owner, 2026-11-19): an Android emulator IS installed: `~/Android/sdk/emulator/emulator`,
AVD `atticpad-test` (android-36 google_apis x86_64, 2 GB), KVM usable; the host is 10.0.2.2 from
the emulator. The final test (section 14) runs an Android peer in it against a desktop peer
(identical end states), and an x86_64 APK's `as3d_sim`-equivalent known-answer hash for Bionic's
libm. ARM code (the FMA risk) is only reachable through the image's ARM translation if an
arm64-only APK installs; whether that translation is bit-faithful is to be checked, not assumed.
The emulator needs RAM: run it alone, never next to builds. Beyond that, the Android guarantee is
the same clang family, the same flags (`-ffp-contract=off`), the same `sim_math`, and the
sort/hash/conversion audit, with the desync detector as the net. The 3DS console soaks remain the
only real ARM hardware check (coarse: end counts, not a hash).

## 8. The front end

### 8.1 The `OnlineHost` interface (the front end's view of the session)

```cpp
// as3d/online.h (as3d::ui), implemented by apps/game NetPlay; FakeOnlineHost in the tests
enum class OnlinePhase { Idle, Checking, Gathering, InviteReady, WaitingReply, ReplyReady,
                         Connecting, Handshake, Lobby, Loading, Barrier, Playing, Failed, Ended };
enum class OnlineFlowChoice { Next, Restart, Continue, Quit, StartCampaign };
struct OnlineLobby {           // what the lobby screen shows; the host's values are authoritative
    int mission = 0, difficulty = 2, heli[2] = {1, 0};
    bool guestReady = false, godMode = false;
    int pingMs = -1, inputDelay = 0;
    std::string peerPlatform;  // "desktop", "web", "android"
};
struct OnlineStatus {
    OnlinePhase phase = OnlinePhase::Idle;
    bool host = false;
    std::string code;          // the invite (host) or reply (guest) code to show, with dashes
    std::string message;       // the failure / end message of section 9, or the status line
    OnlineLobby lobby;
    bool stalled = false;      // in play: waiting for the other side
    bool remotePaused = false; // in play: the other side holds the pause
};
class OnlineHost {
public:
    virtual ~OnlineHost() = default;
    virtual const OnlineStatus& status() const = 0;
    virtual void hostGame(const OnlineLobby& initial) = 0;   // from the Online screen
    virtual void joinGame(const std::string& inviteCode) = 0; // the pasted invite
    virtual void acceptReply(const std::string& replyCode) = 0;
    virtual void cancel() = 0;                               // Back anywhere before play; Bye
    virtual void setLobby(const OnlineLobby& l) = 0;         // host: mission, difficulty, heli[0], godMode
    virtual void setMyHelicopter(int heli) = 0;              // guest (heli[1]); host may use setLobby
    virtual void setReady(bool ready) = 0;                   // guest
    virtual void startGame() = 0;                            // host, when the guest is ready
    virtual void chooseFlow(OnlineFlowChoice c) = 0;         // host's end-screen buttons; Quit from either
    virtual void requestConfirm() = 0;                       // the hint box's OK
    virtual std::string clipboardText() = 0;                 // Paste (SDL_GetClipboardText; web: the page's dialog)
    virtual void setClipboardText(const std::string& s) = 0; // Copy
    virtual bool pageDialog() const { return false; }       // web: the page shows/collects the codes (8.4)
    virtual void acknowledgeMessage() = 0;                   // the notice's OK (phase back to Idle)
};
```

`Frontend` additions (`as3d/frontend.h`, bodies in `engine/src/ui/screens_online.cpp`):
`void setOnlineHost(OnlineHost* h)`; `bool online() const` (a host is set and its phase is
Lobby..Playing); `void onlineApplyFlow(OnlineFlowChoice c)`; `void onlineStartCampaign(const
OnlineLobby& l)` (sets `heli_`, `difficultyChoice_`, then `startCampaign(l.mission, l.difficulty,
2)`; both sides call it from `FlowChoice(StartCampaign)` / the host's Start);
`void onlineEnded(std::string message)` (section 6.4); `void setOnlineNote(std::string)` (the
in-play line, 8.5). New screens: `Screen::Online`, `OnlineHostCode`, `OnlineJoinCode`,
`OnlineLobby`, `OnlineNotice` (added to `screenName`, `kAllScreens`, `kSequelScreens` and the
plain list), all built in `screens_online.cpp` in the Start Game look of `screens_players.cpp`
(letterbox, header, Back where Start Game has it) for the first game, and in the sequels' widget
style (`addSequelButton`, `as2_widgets.cpp`) for the sequels and Gulf's plain list, chosen by
`sequel()` / `plain()` as the other screens do. `FrontendContent::onlineMode` (false by default;
true when the host set an `OnlineHost`) gates the "Online" choice.

### 8.2 Way in: "Online" in Start Game

* First game (`screens_start.cpp`): the "Game mode" spinner gets a third value, `label.players.online`
  = "Online", shown when `content_.onlineMode`. Start with it chosen opens `Screen::Online` with
  the mission, difficulty and helicopter of the screen as the lobby's initial values.
* AirStrike 2 (`screens_as2_start.cpp`, the `kGameMode` spinner: Single / Cooperative) and Gulf's
  plain list (`screens_plain.cpp`): the same third value "Online"; Next opens `Screen::Online`.
  Helicopters are chosen in the lobby (not in HeliSelect) online.

### 8.3 The screens (texts in 9; every string through `texts_.get` with the defaults in
`frontend_texts.cpp`)

**Online** (`Screen::Online`): header "ONLINE"; two text buttons "Host a game" and "Join a game";
the line "You need the other player's invite or reply code: send codes through any chat."; Back.
The first visit runs the fingerprint: "Checking game files..." replaces the buttons until
`OnlineStatus.phase` leaves `Checking`.

**Host** (`Screen::OnlineHostCode`): "Your invite code:"; a code box (a read-only edit field
`addEdit` showing the code with dashes, wrapped over up to 4 lines of 48 characters; selectable
by Copy only); "Copy" button (-> `setClipboardText`); the warning line (9: `online.code_warning`);
"Send it to the other player, then paste their reply code here:"; an edit field (`addEdit`, Char
events; Ctrl+V and the "Paste" button insert `clipboardText()`); "Connect" (-> `acceptReply`);
status line (`status().message`: "Gathering addresses...", "Waiting for the reply code",
"Connecting...", a failure); Back (-> `cancel`). On the web with `pageDialog()` the code box and
the paste field are replaced by "The code is shown on the page" and the page's dialog (8.4) does
both; the screen still shows the status.

**Join** (`Screen::OnlineJoinCode`): "Paste the invite code:" edit + Paste + "Next" (->
`joinGame`); then (phase `ReplyReady`) "Your reply code:" code box + Copy + the warning + "Send it
back to the host. Waiting for the connection..."; status; Back.

**Lobby** (`Screen::OnlineLobby`): "Connected: <Host|Guest> on <platform>, ping NN ms"; rows:
"Mission" (host: the Start Game mission list or spinner over the host's unlocked missions; guest:
text), "Difficulty" (host: spinner; guest: text), "Your helicopter" (spinner over the profile's
unlocked helicopters, names as Start Game shows them; the first game may reuse `addHeliGrid` with
the online side's player only), "Other player's helicopter" (text), "Input delay: N frames (NN ms)"
(text, from `pingMs`); buttons: Back (-> `cancel`), host: "Start" (enabled while `guestReady`),
guest: "Ready" / "Not ready" toggle (-> `setReady`) and "Waiting for the host to start...".
Every change of the host's spinners -> `setLobby`.

**Notice** (`Screen::OnlineNotice`): the message (wrapped), OK (-> `acknowledgeMessage`, back
to the main menu, or to the Online screen when the failure happened before the lobby).

**In play**: the `Hint` screen online: OK/Enter/tap -> `requestConfirm()` instead of
`resumePlay()` (the box closes when the merged confirm reaches both). The in-game menu (`InGame`)
online: "Resume" as today (the pause bit clears); "Options" hides the camera row; "Quit" ->
`chooseFlow(Quit)` then the usual quit. `MissionComplete`, `GameComplete`, `GameOver` on the
guest: Next / Restart / Continue replaced by the text "Waiting for the host..."; on the host the
buttons also call `chooseFlow`. Name entry is local on both.

### 8.4 Platform specifics of the codes

* **Desktop**: SDL clipboard (`SDL_SetClipboardText`, `SDL_GetClipboardText`; the host passes
  them to `NetPlay` through `LoopOptions` hooks so `NetPlay` has no SDL). Ctrl+V in the edit
  field pastes (the loop turns Ctrl+V into a `Char` sequence of the clipboard text when the
  front end `wantsTextInput()`).
* **Android**: SDL's Android clipboard (JNI inside SDL 2.30). The soft keyboard opens for the
  edit field as for name entry; the "Paste" button is the normal path. The share sheet is a
  possible later nicety (not in v1).
* **Web**: the canvas cannot read the clipboard without a user gesture and a permission prompt
  (`navigator.clipboard.readText`: Chrome asks, Firefox refuses outside its paste menu). So the
  page shows the codes: `web_main.cpp` exports `as3d_web_online_show_code(kind, code)` (the
  engine's `OnlineHost::setClipboardText` path and the `ReplyReady`/`InviteReady` phases call
  `window.as3dPage.onOnlineCode(kind, code)`), `app.js` opens a `<dialog>` over the canvas with
  the code in a `<textarea readonly>`, a Copy button (`navigator.clipboard.writeText` on the click,
  with "select all" as the fallback), a "Share" button where `navigator.share` exists (phones),
  and, for the step that needs the other side's code, an `<input>` with a Paste button
  (`readText` where allowed, else the user long-presses/Ctrl+V into the field) and Continue,
  which calls `Module.ccall('as3d_web_online_code', ..., [text])` -> `NetPlay::joinGame` /
  `acceptReply`. `pageDialog()` is true on the web. `?join=<code>` pre-fills the invite (the
  page strips it from the URL after reading), so a host can also send a link
  `https://.../?game=as3d&join=...` (the game key is part of the link). The page stays static.

### 8.5 In play

A one-line note at the top right of the play area (`Frontend::setOnlineNote`, drawn in
`drawOver` while playing, the HUD message style): "Online 45 ms" normally; "Waiting for the other
player..." while `stalled` (after 0.5 s); "Paused by the other player" while `remotePaused` and
not by us; "Connection lost..." for the last seconds before the 10 s timeout ends the session.
The window's pause overlay (`pausedByPlayer`) is unchanged.

## 9. Texts (keys in `frontend_texts.cpp`; English, the games' own tone, no exclamation marks)

| Key | Text |
|---|---|
| `label.players.online` | Online |
| `online.title` | ONLINE |
| `online.host` / `online.join` | Host a game / Join a game |
| `online.intro` | You need the other player's invite or reply code. Send codes through any chat. |
| `online.checking` | Checking game files... |
| `online.invite_label` | Your invite code: |
| `online.reply_label` | Your reply code: |
| `online.paste_invite` | Paste the invite code: |
| `online.paste_reply` | Send it to the other player, then paste their reply code here: |
| `online.reply_sent` | Send it back to the host. Waiting for the connection... |
| `online.code_warning` | The code contains your network addresses. Share it only with the person you play with. |
| `online.copy` / `online.paste` / `online.connect` / `online.next` / `online.ready` / `online.not_ready` / `online.start` | Copy / Paste / Connect / Next / Ready / Not ready / Start |
| `online.page_code` | The code is shown on the page. |
| `online.gathering` | Finding your network addresses... |
| `online.waiting_reply` | Waiting for the reply code |
| `online.connecting` | Connecting... |
| `online.connected` | Connected: %s on %s, ping %d ms |
| `online.lobby.mission` / `.difficulty` / `.your_heli` / `.other_heli` / `.delay` | Mission / Difficulty / Your helicopter / Other player's helicopter / Input delay: %d frames (%d ms) |
| `online.waiting_host` | Waiting for the host... |
| `online.waiting_start` | Waiting for the host to start... |
| `online.note.ping` | Online %d ms |
| `online.note.stalled` | Waiting for the other player... |
| `online.note.remote_paused` | Paused by the other player |
| `online.note.losing` | Connection lost... |
| `online.err.bad_code` | That code is not valid. Paste the whole code. |
| `online.err.wrong_kind` | That is an invite code; the host needs your reply code. / That is a reply code; paste the host's invite code. |
| `online.err.expired` | The invite code expired. Make a new one. |
| `online.err.cant_connect` | Can't connect to the other player. Both of you may be behind strict routers. One of you can forward a UDP port on the router, or you can both join a virtual LAN (ZeroTier, Tailscale, Hamachi), or play on the same network. |
| `online.err.version` | The other player runs a different version (%s, you have %s). Both must use the same release. |
| `online.err.files` | Your game files differ from the other player's. Both must play from the same version of %s. |
| `online.err.game` | The other player has a different game selected (%s). |
| `online.err.protocol` | The other player's game sent something this version does not understand. |
| `online.err.left` | The other player left. |
| `online.err.lost` | The connection to the other player was lost. |
| `online.err.desync` | The game went out of sync at frame %d (a bug). Both games wrote desync-%d.json in their save folder: please send both to the project. |
| `online.err.not_built` | Online play is not in this build. |
| `online.options.camera_host` | (the host's) |

## 10. Security and abuse notes

* The codes carry the players' **IP addresses** (host candidates: LAN and public) and single-use
  ICE credentials; the UI says so (`online.code_warning`). WebRTC's DTLS encrypts the channels and
  the fingerprint in the code authenticates the peer; a third party with the code before the
  intended peer could connect in their place, which is the same trust as sharing a phone number
  through the same chat. STUN tells Google's or Cloudflare's server the player's address (the
  price of no own infrastructure; `AS3D_STUN=none` for a LAN).
* Every wire decoder is bounds-checked with fixed maxima (section 3); nothing from the wire names
  a file, a path, an allocation size beyond the limits, or runs a script. The lobby accepts only
  values inside the game's rules (`missionCount`, `difficultyCount`, `helicopterCount`).
* A malicious peer can at most desync (which ends the session) or stall (which times out).
* Nothing is stored: no addresses, no history, no names. Logs hold the fingerprints and codes'
  lengths, never the codes.

## 11. Out of scope (v1)

Rollback; more than two players; mid-level delay changes; reconnecting to a running session;
spectators; text or voice chat; TURN; LAN discovery (bonus, later); the 3DS; Android share sheet;
the Players screen's device assignment online (every local device flies the one local player);
cheats online; sequels' HeliSelect online (the lobby's spinner instead); recording online runs as
input scripts (works: the recorder sees the merged input, but the delay is not in the script;
later).

## 12. Build options and platforms

* `AS3D_WITH_NET` (root CMake option, ON by default off the 3DS, forced OFF on the 3DS;
  `AS3D_NET_BACKEND` = `rtc` or `null`, like the other backends in `engine/CMakeLists.txt`). With
  the `null` backend (or OFF) the engine compiles the neutral `net` sources (codec, lockstep,
  loopback: always, they have no dependencies, and the tests need them) but no third-party code,
  `makeRtcTransport` returns null with the `not_built` message, and the "Online" choice is not
  offered (`FrontendContent::onlineMode` stays false because the loop sets no `OnlineHost`).
* Desktop: `tools/ci.sh` builds with the default ON (the fetch runs from CMake when the trees are
  missing, as `archive.cmake` does; `AS3D_NET_FETCH=0` forbids downloads in CI). Linking is static.
* Android: `android/app/src/main/cpp/CMakeLists.txt` inherits the root; the manifest gets
  `INTERNET`; `tools/android_build.sh` runs `fetch_third_party.sh net`.
* Web: `apps/web/CMakeLists.txt` adds datachannel-wasm (its `--js-library` is in the link flags)
  and `tools/web_build.sh` copies nothing extra (the JS is linked into `as3d_web.js`).
* 3DS: `tools/3ds_build.sh` must still build: the `null` backend, `apps/3ds` does not compile
  `net_play.cpp`, and the `GameFlow`/`GameSession` changes it compiles are guarded by
  `#if AS3D_WITH_NET` only where they would pull `net_play.h` (the `setNetPlay` pointer and the
  hook calls are small enough to compile everywhere against a forward declaration).

## 13. Tasks

Seven tasks. **Wave 0** is T0 alone (a few hours; everything else branches from it). **Wave 1** is
T1 to T6 in parallel worktrees. Merge order: T0, then T1, T2, T3 (any order; they touch disjoint
files), then T4, then T5 and T6. Each task's worktree builds and runs its own smoke test only;
the integration plan (section 14) runs once after the last merge. No two parallel tasks own the
same file; the seams are the headers T0 lands first. Commit style: one long descriptive subject
line as in `git log`.

### T0: Interfaces and skeleton (Opus) -- wave 0

Goal: every header and stub below exists, the tree builds on desktop with `AS3D_WITH_NET=ON` and
`OFF`, so that T1 to T6 compile against the same contract from day one.

Owns (creates): `engine/include/as3d/net_transport.h`, `net_protocol.h`, `net_lockstep.h`
(with the `kSteerTable` literals, generated by a one-off Python line and pasted),
`net_signal.h`, `sim_math.h`, `sim_fingerprint.h`, `online.h`; `engine/src/net/module.cmake`,
`net.cmake` (the third-party part empty with a `# T3` note), `engine/src/net/protocol.cpp`,
`signal.cpp`, `lockstep.cpp`, `loopback.cpp` (stubs that compile: functions return false/empty),
`engine/src/net/rtc/transport_rtc.cpp` (stub returning null), `engine/src/net/null/transport_null.cpp`;
`engine/src/core/sim_math.cpp` (stubs forwarding to `std::` for now), `engine/src/game/world_hash.cpp`
(stub returning 0), `engine/src/game/sim_fingerprint.cpp` (stub), `engine/src/ui/screens_online.cpp`
(stubs for the new `Frontend` members: screens that show only "ONLINE" and Back),
`apps/game/net_play.h` + `net_play.cpp` (the class of 6.1 with empty bodies); edits: the root
`CMakeLists.txt` (`AS3D_WITH_NET`, `AS3D_BUILD_ID`), `engine/CMakeLists.txt` (the `net` module and
backend variable), `apps/game/CMakeLists.txt` (net_play.cpp), `apps/web/CMakeLists.txt` (the
`AS3D_WITH_NET` plumbing, nothing linked yet), `apps/3ds/CMakeLists.txt` (null backend),
the declarations only in `as3d/world.h` (`u64 stateHash() const; u64 stateHashFull() const;`),
`as3d/frontend.h` (section 8.1 members, the new `Screen` values, `FrontendContent::onlineMode`),
`apps/game/game_session.h` (`LevelSetup::seed/cameraOverride/godMode`, `stateHash()`),
`apps/game/game_flow.h` (`setNetPlay`, `resolveLocalInput`, `step(in, fromNet)`), `apps/game/game_loop.h`
(`LoopOptions` fields of 6.2). The signatures are those written in sections 2.4, 3, 4, 6.1, 8.1
(T0 may refine names but then updates this document in the same commit: the spec is the
contract).

Dependencies: none. Smoke: `cmake -S . -B build -DAS3D_WITH_NET=ON && cmake --build build -j2
--target as3d_game as3d_sim as3d_tests` links; the same with `OFF`; `build/apps/as3d_tests
-tc="*determin*"` passes (nothing changed in behaviour); `emcmake cmake` of `apps/web` configures.
Acceptance: the above, plus `tools/3ds_build.sh` is at least configured (`AS3D_NET_BACKEND=null`
path reviewed; the docker build itself is in section 14).

#### T0 notes (decisions taken while landing the contract; the headers are authoritative)

Where the code differs from the sections above, the headers win:

* **Build options live in `engine/CMakeLists.txt`**, not the root: the web project adds `engine/`
  directly, so that is the one file every platform build passes through. `AS3D_WITH_NET`
  (option, ON; forced OFF on the 3DS), `AS3D_NET_BACKEND` (`rtc` default, forced `null`
  without `AS3D_WITH_NET`), `AS3D_BUILD_ID` (internal cache entry from `git describe --tags
  --always --dirty`, recomputed at every configure). The root and `apps/web` CMake files carry a
  `# T2` placeholder comment for `-ffp-contract=off`; `apps/web/CMakeLists.txt` a T3 line for
  datachannel-wasm; `engine/src/net/net.cmake` returns early without `rtc` and is empty (T3).
* `as3d_net` exports the compile definitions `AS3D_WITH_NET=0|1` and `AS3D_NET_RTC=0|1` to
  everything that links the modules (apps/game, the tests, the 3DS app). `AS3D_BUILD_ID` is a
  definition of `engine/src/net/net_common.cpp` only (a new T0 file, so that a commit recompiles
  one file): read it through `as3d::net::netBuildId()`. That file also holds `peerPlatformName`,
  `localPlatform` and `transportStateName`.
* `as3d/core.h` gains `as3d::u64` and `as3d::i64`.
* **net_transport.h**: as 2.4, plus `rtcTransportAvailable()` (false with `null` and in T0's rtc
  stub; T3 returns true; the window offers "Online" only then), `Transport::Failure`
  (`CantConnect`, `Expired`, ...; T3 sets it so the screen picks the text), `transportStateName`,
  `kDefaultStunServers`, `kGatherTimeoutSeconds = 5`, `kConnectTimeoutSeconds = 20`,
  `kOfferExpirySeconds = 600`, and `LoopbackOptions::nowSeconds` (a test clock; null = steady).
* **net_protocol.h**: one struct per message (`HelloMsg`, `ByeMsg`, `PingMsg` (Ping and Pong),
  `LobbyMsg`, `LobbyGuestMsg`, `StartMissionMsg` with a `WireMissionStart`, `ReadyMsg`,
  `FlowChoiceMsg`, `HashMsg`, `DesyncMsg`, `InputsPacket`, `InputRecord`), enums `MsgType`,
  `PeerPlatform`, `PeerRole`, `ByeReason`, `FlowCode`; `encodeX(const XMsg&, std::vector<u8>&)`
  (replaces the contents) / `decodeX(const u8*, size_t, XMsg&)` for every X; `messageType()`;
  `kNoFrame` (an `ack` with nothing held); `kWireWeaponSlots = 20` (static_asserted against
  `kMaxWeaponSlots` in `net_play.cpp`); `kPingIntervalSeconds`, `kSilenceTimeoutSeconds`,
  `kStallNoteSeconds`.
* **net_signal.h**: as 2.3 plus the pieces T1 needs to test separately: `encodeSignalBinary` /
  `decodeSignalBinary`, `base32Encode` / `base32Decode`, `candidateFromSdpLine`,
  `candidateAddress`. `signalFromSdp` does not set `kind` (not in the SDP; the caller does).
* **net_lockstep.h**: `Lockstep(bool host)`; `setLocal` sets the record of `frame() + delay()`
  and the **first call per frame wins** (returns false after), since a record may already have
  gone out; `addRemote` also takes an `InputsPacket`; `shouldThrottle()` is non-const (it
  remembers the last throttle); the hash check is `noteLocalHash` / `checkRemoteHash` (void)
  plus `desynced(&frame, &mine, &yours)`, the remote hash kept until the local one exists;
  `stalls()` / `noteStall()`; free `inputDelayForRtt(rttMs)` and `steerByte(dx, dy)`;
  `kSteerTable` is `SteerVec{x, y}[256]` (the three axis values below 1e-15 written as 0). The
  private section is T1's to change.
* **sim_math.h**: the names of 7.2 plus `u64 simMathCheckHash()` (T2: FNV-1a of the known-answer
  results; the `sim_math hash` test prints it and T4's `web_main.cpp` logs it for `?simmath=1` as
  `AS3D_SIMMATH hash=`). **sim_fingerprint.h**: `void simDataFingerprintBytes(Vfs&, u8 out[32])`.
* **online.h**: `OnlineStatus::losing` added (the "Connection lost..." note of 8.5).
* **frontend.h**: the five `Screen` values are named `online`, `online_host`, `online_join`,
  `online_lobby`, `online_notice`; `screenFromName` knows them, but they are **not yet in
  `kAllScreens` / `kSequelScreens`** (existing tests iterate those lists): T5 adds them with the
  real screens. `Frontend::open` sends all five to one private builder
  `Menu buildOnlineScreen(Screen)` in `screens_online.cpp`, in every style (after the sequels'
  `applyLook`), so T5 never edits the dispatch. Also `onlineHost()` and `onlineNote()` getters;
  private `online_`, `onlineNote_`.
* **GameFlow**: `step(input, bool fromNet = false)`; `resolveLocalInput(FrameInput&) const` is the
  code step() ran before, extracted and called by step() at the same place (after the hint-box
  confirm) when `!fromNet`, so offline is unchanged. `online()` returns false until T4.
  Section 12's "hook calls compile everywhere against a forward declaration" cannot work for
  member calls: T4 puts every `net_->...` call in `game_flow.cpp` under `#if AS3D_WITH_NET` with
  `#include "net_play.h"` (the 3DS and OFF builds have no `net_play.cpp`).
* **NetPlay** (6.1): `interceptStartMission(ms, NetLevelStart& use)` (a struct with the
  MissionStart, seed, camera and god mode) instead of three out-parameters; no `flowChoice`
  (the OnlineHost's `chooseFlow` is the one); added `endSession(ByeReason)`, `pageCode(code)`
  (the web dialog), `throttle()` non-const; `Options` also has `seed`, `faultFrame`,
  `inviteInPath`, `invite`, `autoLobby`, `clipboardGet/Set`, `pageDialog`, `showCode`, `askCode`.
* **Tests**: `net_play.cpp` is compiled into `as3d_tests` by `apps/CMakeLists.txt` when
  `AS3D_WITH_NET` (as the flow and session are by their tests): tests include `net_play.h`, never
  `net_play.cpp`, and guard online cases with `#if AS3D_WITH_NET`.
* **LoopOptions** (6.2): `online` (`LoopOptions::Online::{Off, Host, Join}`), `stunServers`,
  `netAnyBuild`, `netHashFull`, `netHashEvery`, `netFaultFrame`, `netInvite` (`--invite` /
  `--join CODE`), `netInviteIn`, `netReplyIn`, `netCodeOut`, `clipboardGet/Set`,
  `onlinePageDialog`, `onlineShowCode`, `onlineAskCode`; free functions `hostOnlineCode(code)` and
  `hostOnlineDismiss()` (empty in `game_loop.cpp` until T4) for `web_main.cpp`'s exports.
* `GameSession::LevelSetup` gets `seed`, `cameraOverride`, `godMode` (declared; T4 applies them);
  `GameSession::stateHash()` / `stateHashFull()` are inline over the world.
* The 3DS: nothing to pass; `engine/CMakeLists.txt` forces the null backend for `NINTENDO_3DS`
  (`apps/3ds/CMakeLists.txt` carries a comment only).
* **T1 (landed after T0; what the other tasks rely on):**
  * **Pacing rule changed** (`Lockstep::shouldThrottle`, the comment in `net_lockstep.h` updated,
    no signature change). 4.1's `frame - remoteAck > delay + 2` cannot fire while the remote keeps
    acking and, when it would, the stall comes first. Instead each side compares its lead over the
    remote (`frame + delay - newest remote record`) with the remote's lead over it as its newest
    packet saw it (`last record - ack`); a difference of 3 or more (about a frame and a half ahead)
    throttles, at most once every 10 frames, never while stalled. The caller asks once per display
    frame **before** `setLocal`, and on true skips `setLocal` and the step but still sends the
    packet. No pacing until the remote's ack covers one of our real records (frame >= delay;
    before that its lead reads too small). Balanced links never throttle (tested at 0, 50, 150 ms);
    a side held back makes the other throttle.
  * Stalls measured over the loopback pair (3,000 frames, delay from the latency): none at 0 %
    loss; ~0.5 to 2 % of frames at 10 %; 7 to 12 % at 30 % (a stall needs the two or so packets
    that can still arrive in time all lost; the per-level delay has a margin of one or two frames
    over the one-way latency). A lossier link would want a larger delay than 4.1's formula.
  * `setLocal` normalises the record (unknown flag bits dropped, `steer` zeroed without `kSteer`)
    because `decodeInputs` refuses anything else; it also fills any frames the caller skipped, so
    the local ring is gap-free. `addRemote` does not check the token: the caller drops packets of
    another token (as the loopback test does). Frame numbers for `noteLocalHash` /
    `checkRemoteHash` are the lockstep's (`frame() - 1` after the step).
  * Decoders are strict: exact length, no trailing bytes; `Bye.reason <= 4`, `FlowChoice <= 4`,
    `Hello.role <= 1` (the platform byte is free), the booleans 0/1, `StartMission.inputDelay` in
    2..10 and `players` in 1..2, `Inputs.count` in 1..12 with frames below `kNoFrame`. `decodeHello`
    returns any `protocolVersion` (the caller compares it first). An accepted buffer always
    re-encodes to the same bytes (fuzz-tested).
  * Codes: `signalFromSdp` drops TCP, relay, component-2, loopback, link-local (IPv6 fe80::/10 and,
    beyond 2.3, IPv4 169.254/16) and **mDNS host candidates** (`<uuid>.local`, which Chromium writes
    for host addresses by default): a web peer's code carries its srflx (and IPv6 host) candidates
    only, so two browsers on one LAN connect through their public address (NAT hairpinning). The
    SDP session id is SHA-256(ufrag)'s first 8 bytes big-endian with the top bit cleared. `setup`
    and the sha-256 fingerprint are required; `sctp-port` defaults to 5000. A typical invite (one srflx, one
    host IPv4) is 170 characters, six IPv6 candidates 380.
  * The loopback pair: the host is `SignalReady` after `hostOffer` + `update`, the guest
    `SignalReady` after `joinOffer` + `update` and `Connecting` once the host accepted;
    both `Connected` on an `update` after `latencyMs`; sends before `Connected` are dropped;
    `close()` makes the other side `Failed` with `Failure::Closed` after the latency.
* **T3 (the rtc backend; what T4 to T6 rely on):**
  * **Channels are announced in band (DCEP), not `negotiated`** (2.2 changed): datachannel-wasm's
    `DataChannelInit` has no `negotiated`/`id`, and all three platforms must do the same. The host
    creates `in` (unordered, `maxRetransmits = 0`) and `ctl` (reliable, ordered) before its offer;
    the guest takes them from `onDataChannel` by label. Same reliability as 2.2.
  * **DTLS roles reversed** (2.3 changed): the invite's `SignalInfo::setup` is `Active` (the host
    is the DTLS client), so the reply is `passive` and the guest only waits. Measured: a reply
    pasted on the host 90 s after the guest made it still connects (with the guest as DTLS client
    its handshake would start on the host's early ICE answers and give up within about a minute).
    The host's own SDP still says `actpass`; only the code differs.
  * States: host `New -> Gathering -> SignalReady` (`localSignal` true) `-> Connecting`
    (`acceptAnswer`) `-> Connected` (both channels open). **The guest goes `Gathering ->
    SignalReady -> Connected` with no `Connecting`** (it cannot tell when the host pasted the
    reply; the loopback pair does show `Connecting` for it). Timeouts: gathering cut at 5 s (the
    code holds what was found), host `Connecting` -> `Failed/CantConnect` after 20 s, either side
    `SignalReady` -> `Failed/Expired` after 600 s. ICE failing before `Connected` ->
    `CantConnect`; the peer connection failing or a channel closing after it (the other side quit
    or vanished; libjuice's consent check gives up after 30 s of silence) -> `Failed/Closed`.
    `close()` -> `Closed`. `error()` holds the English detail for the log.
  * Threads: libdatachannel's callbacks (the browser's on the web) only fill mutex-guarded queues;
    every `Transport` member is for the game thread. `update()` once per display frame drives the
    timers and the state; polling works in any state. Sends before `Connected`, over the size
    limits, or on `in` while more than 16 KB wait in SCTP are dropped silently. Queues: `in` keeps
    the newest 512 packets, `ctl` fails the transport (`Other`) past 16,384 unread messages.
  * STUN: `TransportConfig::stunServers` as given; empty or `"none"` entries mean no server (the
    caller turns `AS3D_STUN` / `?stun=` into the list; `as3d_net_tool` reads `AS3D_STUN` itself).
    With no STUN the code carries host candidates only (a LAN address: loopback is dropped), which
    is what `as3d_net_tool pair --stun none` connects over.
  * `apps/net_tool` (`as3d_net_tool pair|host|join`), and a raw-SDP path for it only
    (`engine/src/net/rtc/transport_rtc_raw.h`, `--raw-sdp`). The rtc code is reached only through
    `makeRtcTransport`: until T4 calls it, the game binaries link none of it.
* **T4 (game integration; what T5, T6 and the final test rely on):**
  * `NetLevelStart` lives in `apps/game/game_flow.h` (the 3DS and OFF builds compile GameFlow
    without `net_play.h`); every `net_->` call in `game_flow.cpp` and `game_loop.cpp` is under
    `#if AS3D_WITH_NET`. `GameFlow::pollNetStart()` runs the guest's start held pending once the
    host's `StartMission` arrived; `GameFlow::levelLoaded` sends `Ready` (`NetPlay::levelLoaded`).
  * **Online level starts build a new `World`** (`LevelSetup::freshWorld`): `World::init` keeps the
    entity slots' generation counters, which the attract level under the lobby advanced by a
    different amount on each side, so the dumps (and `stateHashFull`) of two peers differed in the
    `gen` and reference fields although they simulated alike.
  * Pause (6.3): `setPaused(true)` while a hint box or a level end owns the pause is not this
    player's pause and sets no bit; the sequels' start-dialogue pause no longer pauses the world
    directly online (`levelLoaded`), it goes through the records like any other. The new level's
    start clears the local pause and confirm. The merged confirm closes the hint box in
    `GameFlow::step(in, true)` with `World::dismissHint` + `Frontend::onlineHintConfirmed` (the
    box's own OK only calls `requestConfirm`). The bot's hint OK (`FrameInput::confirm`) also
    becomes `kConfirm`.
  * Every local device flies the local player online: `held[0] | held[1]` of the local input
    (pads a two-player level would give player 2) becomes the record; the bot gives the host
    `botInputPlayers(world, world.frame()).held[0]`, the guest `.held[1]`.
  * The host's Quit sends `Bye` only (no `FlowChoice(Quit)`); the guest shows "The other player
    left" from the `Bye`. A guest receiving `FlowChoice` Next / Restart / Continue stops its
    lockstep at once (phase `Loading`), so the tail of the old level may differ in length between
    the sides; nothing of it survives the fresh world.
  * Input delay: 4.1's formula on the median of the last five round trips, **plus one frame when
    those five spread over more than 16.7 ms** (a jittery link); no extra frame otherwise. The
    bot lobby (`--bot`) starts only after three pongs, so the delay comes from a measured ping.
  * Silence: `lastHeard` counts any ctl message or Inputs packet; it is moved to "now" after this
    side's own level load. "Connection lost..." (`OnlineStatus::losing`) after 3 s of silence.
  * A transport kept 1.5 s after the end (`Bye`, `Desync`) so that the last message leaves before
    the connection closes.
  * `--frames N` online counts frames simulated under the lockstep (all levels); at N the side
    sends a `Hash` of its last frame (if that frame was not hashed anyway) and waits for the other
    side's hash of a frame at least as far (or 15 s), then writes `--dump-state` and ends with
    `Bye(Quit)`; exit 0. A desync is a clean end too (exit 0); any other end before N exits 1.
  * Command line (`main.cpp`): `--online host|join`, `--invite CODE` / `--join CODE`,
    `--invite-in FILE`, `--reply-in FILE`, `--code-out FILE` (written through a rename),
    `--stun LIST|none` (and `AS3D_STUN`), `--net-any-build` (and `AS3D_NET_ANY_BUILD=1`),
    `--net-hash-full`, `--net-hash-every N`, `--net-fault-frame F` (the F-th online frame,
    0-based, on this side), `--seed S` (the host's level seed; random without), `--markers` (the
    AS3D_* lines of a windowed run). `--online` forces the front end; the session starts by itself
    once the main menu is up and opens the Online and code screens as a player would. With
    `--headless` the run has no window and no GL (a 60 Hz wall clock; `AS3D_LEVEL_LOADED` and,
    with `--markers`, `AS3D_GAME_FRAME` every 600 online frames).
  * Web (`web_main.cpp`): `--join CODE`, `--stun LIST`, `--net-any-build`, `--simmath` (logs
    `AS3D_SIMMATH hash=%016llx` at start), `--dump-state` (at the end of the run one line
    `AS3D_DUMP_STATE b64=<base64 of the JSON>` on stdout, the console). Exports
    `as3d_web_online_code(const char* text)` and `as3d_web_online_dismiss()`; calls
    `window.as3dPage.onOnlineCode(kind, code)` (host: "invite" when its code is ready; guest:
    "reply"; again on Copy) and `window.as3dPage.onOnlineAskCode(kind)` (host: "reply" right after
    its invite; guest: "invite" when the join screen opens with no code). Dismiss only goes back
    from the guest's empty join screen. `?join=` (as `--join`) needs the menus.
  * Android: extras `net_any_build` (boolean) and `stun` (string) through `GameActivity.java`.
* **T6 (web page, docs, tooling):** the dialog is `<dialog id="online">` in `index.html`
  (textarea of this side's code, Copy, Share where `navigator.share` exists, "Copy join link" on the
  host's invite when the game is known, the pasted field with Paste / Continue, Close); `app.js`
  keeps `window.as3dState.online` = `{shown: [{kind, code}], asked: [kind], submitted, dismissed}`
  and logs `AS3D_WEB online_code|online_ask|online_copy|online_submit|online_dismiss` for the walks.
  Close hides the dialog (calls `as3d_web_online_dismiss` only on the guest's empty invite
  field); Continue calls `as3d_web_online_code`. Page parameters: `?join=` (read once, removed
  from the address, charset-checked), `?stun=`, `?netanybuild=1`, `?simmath=1`, `?dumpstate=1`. The
  walks start Chromium with `--disable-features=WebRtcHideLocalIpsWithMdns` (mDNS candidate names
  are dropped from codes). The web lobby has no auto-bot (`autoLobby` needs `online != Off`):
  the walks tap Ready (id 14) and Start (id 13) on `online_lobby`. `tools/online_test.sh` runs
  items 1 to 3 (`--games`, `--frames`, `--long`, `--web URL`, `--quick`).

### T1: Protocol, codes and the lockstep core (Opus)

Goal: `engine/src/net/{protocol,signal,lockstep,loopback}.cpp` real, with unit tests.

Owns: those four files, `apps/tests/net_protocol_test.cpp`, `net_signal_test.cpp`,
`net_lockstep_test.cpp`, `net_loopback_test.cpp`. Depends on T0. Contract: the headers of T0
(`as3d/net_protocol.h`: `encodeHello/decodeHello`... one `encode`/`decode` pair per message of
section 3 on `std::vector<u8>` / `(const u8*, size_t)`, `encodeInputs/decodeInputs`;
`as3d/net_signal.h`: `encodeSignalCode`, `decodeSignalCode`, `signalFromSdp`, `sdpFromSignal`,
`selectCandidates`; `as3d/net_lockstep.h`: the `Lockstep` class of section 4 with `start(token,
delay)`, `frame()`, `setLocal(record)`, `addRemote(firstFrame, records, n, ack)`, `canStep()`,
`takeInputs(host, guest)`, `buildInputsPacket(out)`, `remoteAck()`, `shouldThrottle()`,
`noteLocalHash`, `checkRemoteHash`; `makeLoopbackPair`).

Tests: round trips of every message with edge values; fuzz each decoder with 10,000
random/truncated buffers (no crash, false returned); a code of a 6-candidate IPv6 `SignalInfo`
round-trips and is under 400 characters; the decoder accepts lowercase, dashes, spaces and
I/L/O substitutions; `sdpFromSignal(signalFromSdp(sdp))` is idempotent on a libdatachannel-style
and a Chromium-style SDP sample (two text fixtures in the test); two `Lockstep`s joined by the
loopback pair at 0, 50 and 150 ms latency and 0 % / 10 % / 30 % loss on inputs run 3,000 frames of
random inputs: every frame both sides take identical `(host, guest)` records, the stall count is
0 at 0 % loss with the computed delay and bounded at 30 %; the hash check flags an injected
mismatch; `shouldThrottle` fires when one side is held back.

Smoke: `build/apps/as3d_tests -tc="net*"` (< 15 s). Acceptance: all of the above green; no
dependency outside `as3d_core`; a `kSignalVersion` bump path documented in the header.

### T2: Determinism (Opus)

Goal: section 7 done: `sim_math` real and used everywhere in the simulation, `-ffp-contract=off`
engine-wide and in the web project, the sort/hash/conversion audit, `World::stateHash` and
`simDataFingerprint` real, the expected numbers updated once.

Owns: `engine/src/core/sim_math.cpp`, `as3d/sim_math.h` (may extend), every file under
`engine/src/game/`, `engine/src/script/`, `engine/src/core/` (libm replacement and the audit; T0's
stubs `world_hash.cpp`, `sim_fingerprint.cpp` become real), `as3d/math.h`, `as3d/vec.h`,
`as3d/world.h` (private members for the hash if needed), the `-ffp-contract=off` lines in the root
and web CMake files (T0 leaves a `# T2` placeholder comment in both so the edits do not collide),
`apps/tests/sim_math_test.cpp`, `apps/tests/world_hash_test.cpp`, `testdata/golden/*/expected.json`
(`bot_regression` only), `docs/missions-status*.md` (numbers only), a `CHANGELOG.md` entry under
Unreleased ("the simulation's trigonometry is now the engine's own... end states changed once").
Depends on T0.

Contract: `as3d/sim_math.h` names of 7.2; `World::stateHash()` as 4.4 (the field list is the
contract: T4 does not care how); `std::string simDataFingerprint(Vfs&)` returns 64 hex characters,
and `simDataFingerprintBytes(Vfs&, u8 out[32])`; the exclusion list of 5.3 lives in
`sim_fingerprint.cpp` as a `constexpr` array.

Smoke (< 2 min): `build/apps/as3d_tests -tc="sim_math*,world hash*,*bot regression*"`; then
`as3d_sim --game as3d --level 1 --frames 3600 --bot --dump-state a.json` twice, `cmp`; then the
FMA cross-check of 7.1 against the new tree (`cmake -S apps/sim_tool -B /tmp/fma
-DCMAKE_CXX_COMPILER=clang++ "-DCMAKE_CXX_FLAGS=-mfma -ffp-contract=fast" ...`: with the engine-wide
`-ffp-contract=off` coming *after* `CMAKE_CXX_FLAGS` on the command line, the dump must now be
identical to the default build's -- that is the proof that the option reaches every simulation
file; `objdump | grep -c vfmadd` = 0 in the simulation objects). Acceptance: the grep test finds
no libm call left in the simulation sources; the known-answer table is committed; the bot
regression and the two-player soak (`two_player_missions_test.cpp`, one game, can be run with
`-tc` filter) pass with the updated numbers; `tools/3ds_build.sh` still compiles `sim_math.cpp`
(no `<cmath>` dependency beyond what newlib has: the port uses only arithmetic and bit casts).

### T3: The rtc transport and the third-party build (Opus)

Goal: `makeRtcTransport` real on desktop, Android and the web; the libraries fetched and built
from pinned sources; a command-line tool that connects two transports.

Owns: `engine/src/net/rtc/transport_rtc.cpp`, `engine/src/net/null/transport_null.cpp`,
`engine/src/net/net.cmake` (the third-party build), `tools/fetch_third_party.sh` (the `net`
mode), `apps/net_tool/` (new: `CMakeLists.txt`, `main.cpp`: `as3d_net_tool host [--stun ...]`
prints the invite code and reads the reply from stdin; `join <invite>` prints the reply; both
then exchange 1,000 `Inputs`-sized packets and 100 ctl pings and print the RTT and loss;
`as3d_net_tool pair` does host and join in one process over loopback ICE with no stdin, for the
smoke test), `apps/CMakeLists.txt` (adds `net_tool`), `apps/web/CMakeLists.txt` (datachannel-wasm:
`add_subdirectory` and its `--js-library` link flag, under T0's `AS3D_WITH_NET` plumbing),
`android/app/src/main/AndroidManifest.xml` (INTERNET), `android/app/src/main/cpp/CMakeLists.txt`
and `android/app/build.gradle` if the NDK build needs a flag, `tools/android_build.sh` and
`tools/web_build.sh` (call the fetch), `docs/android.md` and `docs/web.md` "Third-party" lines.
Depends on T0 (and uses T1's `SignalInfo` conversions once merged; until then the stubs).

Contract: section 2.4 (`Transport`, `TransportConfig`, `makeRtcTransport`); channels of 2.2
with `negotiated` ids 0 and 1; the backend converts `rtc::Description` + candidates to
`SignalInfo` through `signalFromSdp` and back through `sdpFromSignal` (T1's; the backend never
parses SDP itself); the gathering cap of 5 s; the connect timeout of 20 s; the 10 minute offer
expiry. Logging through `AS3D_INFO` with the `AS3D_NET` prefix; libdatachannel's own log at
warning level only.

Smoke: `tools/fetch_third_party.sh net` (once); `cmake --build build -j2 --target as3d_net_tool`
and `build/apps/net_tool/as3d_net_tool pair --stun none` prints "connected" and "1000/1000
packets" within 10 s. Acceptance: the pair test; `host`/`join` across two terminals on this
machine; the web build links (`tools/web_build.sh byo`) and the page's console shows no missing
symbol; `tools/android_build.sh` with `AS3D_ANDROID_ABIS=arm64-v8a` builds (the APK size
difference is written into `docs/android.md`); the pinned SHAs are in the script with the
"Verified ... on <date>" lines.

### T4: Game integration: `NetPlay`, `GameFlow`, the loop, the command line (Opus)

Goal: section 6 done: an online session from the lobby to the end of a campaign, on the
desktop binary, with the loopback transport in the tests and the rtc transport by the options.

Owns: `apps/game/net_play.{h,cpp}`, `game_flow.{h,cpp}`, `game_session.{h,cpp}`,
`game_loop.{h,cpp}`, `main.cpp`, `web_main.cpp` (the C++ side of 8.4: the exports
`as3d_web_online_show_code`, `as3d_web_online_code`, `?stun=`, `?netanybuild=1`, `?join=` passed
as `--join CODE`), `android_main.cpp` (intent extras `net_any_build`, `stun`),
`apps/tests/online_flow_test.cpp` (two `GameStack`s with the front end, headless, joined by
`makeLoopbackPair`: lobby through the real `Frontend` screens by taps, a mission with
`botInputPlayers`, hashes checked every frame with `--net-hash-full`, Mission Complete, Next, a
hint box confirmed by the guest, a pause by each side, Quit; a second case injects a mismatch
through `World` and expects the desync end and the two dump files), `docs/spec/issues/180` status
lines. Depends on T0; T1, T2, T3 should be merged before its final test but its code compiles
against the stubs.

Contract: 6.1 to 6.4; the command line `as3d_game --online host|join [--invite CODE]
[--reply-in FILE] [--code-out FILE] [--stun LIST|none] [--net-any-build] [--net-hash-full]
[--net-hash-every N] [--seed S]` (`--code-out` writes the invite/reply code to a file as soon as it
exists and `--reply-in`/`--invite` read codes from files that appear later: the headless
integration test of section 14 drives two processes with them; with `--headless --bot` the whole
lobby is automatic: host sets the lobby from `--level/--difficulty`, guest readies, host starts);
the `OnlineHost` of 8.1 implemented in full; `LoopOptions::clipboardGet/clipboardSet` hooks (SDL
in `main.cpp`/the loop, the page on the web).

Smoke: `build/apps/as3d_tests -tc="online flow*"` (< 90 s, needs game data and a GLES context
like `game_flow_test`). Acceptance: the test; `as3d_game --online host --headless --bot --frames
3000 --code-out /tmp/i --reply-in /tmp/r --stun none --dump-state h.json` and the mirror `--online
join --invite-in /tmp/i --code-out /tmp/r --dump-state g.json` in two processes give identical
dumps (this is section 14 item 1, run once here as the task's own proof); `as3d_game --level 1
--bot --frames 600 --headless --dump-state` is byte-identical to the same run before T4 (offline
behaviour unchanged).

### T5: Front-end screens and texts (Sonnet)

Goal: section 8 and 9 done in the three front-end styles, tested against a fake host.

Owns: `engine/src/ui/screens_online.cpp` (replaces T0's stubs), `screens_start.cpp`,
`screens_as2_start.cpp`, `screens_plain.cpp` (the "Online" choice), `screens_game.cpp`,
`screens_as2_game.cpp` (the Hint OK relay, the guest's end screens, the in-game menu's Quit,
Options' camera row), `frontend.cpp` (`setOnlineHost`, `online()`, `onlineEnded`, the note in
`drawOver`, the P key online -> `host_.setPaused` is already the path: keep it), `frontend_texts.cpp`,
`as3d/frontend.h` (private members it needs; the public contract is T0's), `apps/tests/online_frontend_test.cpp`
with a `FakeOnlineHost` (records calls, lets the test set phases and codes), `docs/spec/frontend.md`
(a "10. Online (ours)" section pointing here). Depends on T0 only.

Contract: 8.1 (`OnlineHost`), the screen names and texts of 8.3 and 9, the `AS3D_MENU` lines the
web walks tap (every button of the new screens is a menu item, so `walk.py` can find them by
label as it does today).

Smoke: `build/apps/as3d_tests -tc="online front end*"` (< 10 s) and
`build/apps/as3d_game --headless --ui-script t.txt --frames 300` with a 6-line script that opens
Start Game, picks Online, Host, and takes a screenshot of each new screen (the script and a
`--selector-shot`-style gallery under `out/online/` for the owner to look at; the screenshots
are not committed). Acceptance: the tests; every text in section 9 present; the three styles
render without overlap at 800x600 (screenshots reviewed); the code box wraps a 360-character code;
the Players screen is bypassed online; nothing changes offline (`frontend_test`, `as2_frontend_test`,
`gulf_frontend_test`, `plain_frontend_test` still pass).

### T6: Web page, docs, and the integration test tooling (Sonnet)

Goal: the page's part of 8.4, the user docs, the changelog, and the scripts of section 14 ready
to run.

Owns: `apps/web/site/app.js`, `index.html`, `style.css` (the dialog: `onOnlineCode(kind, code)`,
Copy/Share/Paste/Continue, `?join=`, `?stun=`, `?netanybuild=1` passed to `callMain`),
`apps/web/test/walk.py` (scenario `online`: a Chromium guest against a desktop host process
started by the scenario with `--online host --headless --bot --code-out`, the invite read from
the file and passed as `?join=`, the reply read from the page's dialog textarea and written to
the host's `--reply-in` file, 60 s of play, `AS3D_NET` and `AS3D_GAME_FRAME` lines compared,
no `AS3D_DESYNC`; a second scenario `online_web` with two Chromium pages, one host one guest,
codes passed by the test between the dialogs), `tools/online_test.sh` (section 14 items 1 to 3:
the two-process desktop runs, the forced desync, the wasm `simmath` check; prints "online: OK"),
`docs/web.md` ("Online" section: how to host and join, the codes, the warning, the can't-connect
tips, `?join=`), `docs/android.md` (the same for the app), `README.md` (a status line),
`CHANGELOG.md` (Unreleased: "Online co-op..."), `docs/spec/README.md` index line. Depends on T0
for the names (`as3d_web_online_*`), T4 for the command line; the scripts can be written against
the spec and first run in section 14.

Contract: the exports and page callbacks of 8.4 exactly: `window.as3dPage.onOnlineCode(kind,
code)` (`kind` = "invite" | "reply"), `window.as3dPage.onOnlineAskCode(kind)` (open the paste
field for that kind), `Module.ccall('as3d_web_online_code', null, ['string'], [text])`,
`Module._as3d_web_online_dismiss()`; the `?join=`/`?stun=`/`?netanybuild=` parameters.

Smoke: `tools/web_build.sh byo` then `python apps/web/test/walk.py --only choose --url ...`
(the existing scenario still passes with the page changes; ~3 min); `node --check
apps/web/site/app.js`. Acceptance: the two online walk scenarios exist and are wired into
`--only`'s default list; the docs read as the owner's docs do (sections, same voice); the
changelog entry; the dialog works on a phone profile (the walk's phone scenario opens it: a tap
on Copy does not throw).

## 14. Final integration test plan (run once, after the last merge)

Setup on this machine (15 GB, OOMs easily): every test binary under `ulimit -v 4000000`, at most
two processes with game data at a time, builds with `-j2`. `tools/online_test.sh` runs items 1 to
3 and 5; items 4, 6, 7 by hand as written.

1. **Two desktop processes over loopback ICE** (the real rtc transport, `--stun none`): host
   `as3d_game --headless --online host --bot --frames 6000 --level 1 --seed 7 --code-out
   $T/invite --reply-in $T/reply --net-hash-full --dump-state $T/host.json --markers`, guest
   `--online join --invite-in $T/invite --code-out $T/reply --dump-state $T/guest.json --markers`,
   started 1 s apart; expect both exit 0, `cmp host.json guest.json` identical, no `AS3D_DESYNC`,
   the `AS3D_NET start` lines agree on token/seed/delay, `AS3D_NET stall` total under 1 s. Repeat
   for `--game as2` (mission 1, pilot: the relative mouse steering off since headless) and `gulf`.
   Then a 3-level run: `--frames 40000 --god` so the bot reaches Mission Complete twice and the
   host's automatic Next (the `--bot` lobby automation chooses Next) is followed by the guest:
   `AS3D_LEVEL_LOADED` twice on both, dumps identical.
2. **Forced desync**: the guest with `--net-fault-frame 900` (a test option of T4: flips one
   bit of player 2's health at that frame on that side only); expect `AS3D_DESYNC` on both within
   40 frames, both `desync-*.json` written, both processes end with the desync message in the log,
   exit 0 (a clean end).
3. **Determinism desktop vs wasm**: `tools/web_build.sh byo`, serve, open `?game=as3d&simmath=1`
   under Playwright and read `AS3D_SIMMATH hash=`; compare with `as3d_tests -tc="sim_math hash"`
   (prints the same hash). Then the bot run: `?game=as3d&level=1&bot=1&frames=3600&dumpstate=1`
   (T4 adds `dumpstate` to the web: the dump is printed to the console as one line, base64) against
   `as3d_sim --game as3d --level 1 --frames 3600 --bot --dump-state`: identical. Android: not
   runnable here (section 7.3); the APK is built (`AS3D_ANDROID_ABIS=arm64-v8a`) and installed on
   the owner's phone for item 6.
4. **Desktop <-> web (Playwright Chromium)**: `walk.py --only online` (T6): a desktop host process
   and a Chromium guest over loopback ICE with codes passed by the test; 60 s of play with the bot
   on both; `AS3D_GAME_FRAME` lines of both at frame 3000 show the same `score`, `lives`,
   `map_pos`; no `AS3D_DESYNC`; the host's Next after Mission Complete is followed (god mode run).
   Then `--only online_web` (two Chromium pages).
5. **Lockstep under loss**: the loopback unit test of T1 at 150 ms / 30 % (already in
   `as3d_tests`), and the two-process run of item 1 with `tc` (`tc qdisc add dev lo root netem
   delay 60ms loss 5%`, needs sudo: optional, by hand) to see stalls stay rare. Without root,
   `AS3D_NET_IMPAIR=DELAY_MS,LOSS_PERCENT` set for both processes (a test hook of the native rtc
   transport: every received message held DELAY_MS, LOSS_PERCENT of the sent Inputs packets
   dropped) gives the same link: `AS3D_NET_IMPAIR=60,5 tools/online_test.sh`.
6. **Owner's hands-on** (the only part needing another machine): desktop host, Android guest on
   the same Wi-Fi (codes by any chat); then the web guest on a phone browser; then one peer behind
   a mobile network (STUN path); then a pair that cannot connect (two mobile networks) to see the
   message and tips. The owner reports ping, input delay, feel.
7. **Nothing else broke**: `tools/ci.sh` for every game (the expected-state numbers updated by T2
   are what it checks), `walk.py` with its default scenario list (the existing ones plus the two
   online ones), `tools/3ds_build.sh` compiles (null backend), `tools/android_build.sh` builds,
   `tools/web_build.sh bundled` and `byo` build and `files_check.js` passes; the APK and wasm size
   deltas written into the docs.

**Run on 2026-11-20** (all green after the fixes, items 1 to 5 and 7; item 6 is the owner's):
item 1 for the three games and the 40,000-frame three-level run, item 2, item 3 (wasm
`c38a83fb2d80117b`, the bot dump byte-identical), item 4 (and every other walk), item 5 with
`AS3D_NET_IMPAIR` (no root here), item 7. Android on the emulator in place of "not runnable":
`c38a83fb2d80117b` and the bot dump identical to the desktop's on x86_64 and on arm64-v8a under
ndk_translation, and a desktop host with the app as guest, identical dumps (docs/android.md,
"Online co-op"). Bugs found and fixed: the guest's level length after the host's Next (the
`--frames` count and the markers drifted), the fingerprint counting the extracted texts file at
the root, the web page's code dialog staying over the lobby, `AS3D_MENU` not following the
lobby's Start, the window loop's markers and `?frames=` counting window frames in a menu-opened
session, `_malloc`/`_free` missing from the wasm exports for datachannel-wasm, messages lost when
they arrived with the close, the walks' second Playwright, and `files_check.js`'s stale Gulf
Thunder expectation.

## 15. Decisions taken here, and open questions for the owner

Decided (in this spec; the owner may overturn any):

* Delay-based lockstep, input delay fixed per level from the measured ping, 2..10 frames; no
  rollback (section 4).
* Unreliable unordered inputs channel with 12-frame redundancy plus a reliable control channel
  (2.2).
* Codes are compact binary in Crockford base32, no compression, candidates capped at six; the
  SDP is rebuilt, never shipped (2.3).
* libdatachannel + Mbed TLS 3.6 (native), datachannel-wasm (web); fetched by git at pinned commits
  for the submodule-bearing libraries, tarballs with SHA-256 for Mbed TLS (2.1).
* The host is player 1, the guest player 2; every local device flies the local player; no
  Players screen online (4.1).
* The pause is a held bit of each peer's input; the hint box's OK a confirm bit; the camera mode
  is the host's; cheats are off online; the host picks mission and difficulty, each picks their
  own unlocked helicopter, the mission needs to be unlocked for the host only; both profiles get
  the unlocks (5.2, 5.4, 6.3).
* The sequels' mouse steering is quantised to a byte and stays usable (4.2).
* `sim_math` (musl-derived) replaces libm in the simulation and `-ffp-contract=off` goes
  engine-wide; the desktop's end states change once (7.2).
* Data identity by a fingerprint of the non-media files, not the pak hashes (5.3).
* On the web the codes live in a page dialog, not in the canvas (8.4).

Open questions (with the recommendation):

1. **Accept the one-time change of the desktop end states** (7.2: `bot_regression` numbers and the
   missions-status numbers move; nothing visible to a player)? Recommendation: yes; it buys
   bit-identical simulation on all platforms, which online play cannot do without. The alternative
   (keep glibc on the desktop and accept desyncs with Android and the web) is not viable.
2. **STUN servers**: Google's and Cloudflare's public STUN (both free, both reveal the player's
   address to that provider during setup). Recommendation: yes, with `AS3D_STUN` to override or
   disable; name them in the docs.
3. **The APK grows by ~4 to 5 MB** (two ABIs) and the desktop binary by ~2.5 MB for the WebRTC
   stack. Recommendation: accept; an `AS3D_WITH_NET=OFF` build stays available. Measured by the
   final test (ON against OFF at the same commit): the APK +5.5 MB (libmain.so +2.7 MB arm64,
   +2.8 MB x86_64, stored uncompressed), the stripped desktop binary +2.8 MB (7.96 MB against
   5.20), the wasm +128 KB (the browser has the WebRTC stack).
4. **The guest's mission choice**: a guest may join a mission its own profile has not unlocked
   (it unlocks on completion). Recommendation: yes, it is the point of joining a friend; the
   alternative (both must have it unlocked) is one line to change.
5. **Codes in chat**: ~150 to 360 uppercase characters. Recommendation: accept for v1; a shorter
   code needs a rendezvous server, which the constraints forbid. The web link form
   (`?join=...`) makes the host's half one tap on a phone.

---

Version 1 (2026-11-19): the specification and the work breakdown.
Version 1.1 (2026-11-19): T0 landed the headers and stubs; "T0 notes" under T0 list where they
refine the sections above.
Version 1.2 (2026-11-20): T4 landed the game integration; "T4" notes under T0 list where it refines
sections 6 and 8.4.
