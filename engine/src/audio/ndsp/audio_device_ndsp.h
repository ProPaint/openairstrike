// The Nintendo 3DS output device (M2.3, docs/3ds-port.md "Audio"): IAudioDevice over libctru's
// ndsp. makePlatformAudioDevice() calls ndspInit (it needs sdmc:/3ds/dspfirm.cdc on hardware);
// on failure it logs once and returns null, Audio::init fails and AudioBridge carries on with the
// Null device (silent).
//
// One channel of interleaved stereo PCM16 at 32728 Hz (the DSP's own rate, no DSP resampling),
// four wave buffers of 1024 frames (125 ms queued) in linear memory. A feeder thread on core 1
// (APT_SetAppCpuTimeLimit(30), one priority step above the thread that opens the device) sleeps
// on a LightEvent that the ndsp frame callback signals (every DSP frame, about 4.9 ms) and fills
// every finished buffer, in play order, through Mixer::renderS16. The mixer's own std::mutex is
// the only lock between the game thread and the feeder (std::mutex works under libctru: newlib's
// locks are libctru LightLocks; the feeder itself is a libctru Thread because std::thread cannot
// choose a core).
//
// This header is for the 3DS app's logs: the counters of the open device, readable from any
// thread while it runs.
#pragma once

#include <cstdint>

namespace as3d::audio::ndsp {

struct NdspStats {
    bool open = false;          // a device is open
    bool feederOnCore1 = false; // false: threadCreate on core 1 failed, the feeder runs on core 0
    uint32_t fills = 0;         // buffers rendered and queued after the first four
    uint32_t underruns = 0;     // fills that found no buffer still queued: the DSP had run dry
    uint32_t maxFillUs = 0;     // longest renderS16 of one buffer
    uint64_t renderUs = 0;      // time in renderS16 since open (feeder CPU)
    uint64_t openUs = 0;        // time since open
    int peak = 0;               // largest |sample| since the last read with resetPeak
    uint32_t droppedFrames = 0; // ndspGetDroppedFrames
};

// The open device's counters; all zero (open = false) without one. `resetPeak` restarts the peak.
NdspStats stats(bool resetPeak = false);
// One line for the log: channel format, rate, buffers, feeder core and priority.
const char* description();

} // namespace as3d::audio::ndsp
