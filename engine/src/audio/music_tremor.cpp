// OggMusic over tremor (libvorbisidec): the 3DS's pre-rendered music (music_ogg.h).
#include <tremor/ivorbisfile.h>

#include <cstdio>
#include <cstring>
#include <utility>

#include "as3d/audio.h"
#include "music_ogg.h"

namespace as3d::audio {
namespace {

constexpr int kBufFrames = 1024;

// One Ogg file in memory with its decoder.
struct Stream {
    Blob data;
    size_t pos = 0;
    OggVorbis_File vf{};
    bool open = false;
    int channels = 2;
    int rate = 0;
};

size_t memRead(void* ptr, size_t size, size_t nmemb, void* ds) {
    Stream* s = static_cast<Stream*>(ds);
    const size_t want = size * nmemb;
    const size_t left = s->data.size() - s->pos;
    const size_t n = want < left ? want : left;
    std::memcpy(ptr, s->data.data() + s->pos, n);
    s->pos += n;
    return size ? n / size : 0;
}

int memSeek(void* ds, ogg_int64_t offset, int whence) {
    Stream* s = static_cast<Stream*>(ds);
    ogg_int64_t base = 0;
    if (whence == SEEK_CUR) base = static_cast<ogg_int64_t>(s->pos);
    else if (whence == SEEK_END) base = static_cast<ogg_int64_t>(s->data.size());
    const ogg_int64_t p = base + offset;
    if (p < 0 || p > static_cast<ogg_int64_t>(s->data.size())) return -1;
    s->pos = static_cast<size_t>(p);
    return 0;
}

int memClose(void*) { return 0; }
long memTell(void* ds) { return static_cast<long>(static_cast<Stream*>(ds)->pos); }

bool openStream(Stream& s, Blob data, const char* what) {
    s.data = std::move(data);
    s.pos = 0;
    ov_callbacks cb;
    cb.read_func = memRead;
    cb.seek_func = memSeek;
    cb.close_func = memClose;
    cb.tell_func = memTell;
    const int r = ov_open_callbacks(&s, &s.vf, nullptr, 0, cb);
    if (r != 0) {
        AS3D_WARN("music: %s is not Ogg Vorbis (tremor error %d)", what, r);
        s.data.clear();
        return false;
    }
    const vorbis_info* vi = ov_info(&s.vf, -1);
    s.channels = vi ? vi->channels : 2;
    s.rate = vi ? static_cast<int>(vi->rate) : 0;
    if (s.channels < 1 || s.channels > 2 || s.rate <= 0) {
        AS3D_WARN("music: %s has %d channels at %d Hz, unsupported", what, s.channels, s.rate);
        ov_clear(&s.vf);
        s.data.clear();
        return false;
    }
    s.open = true;
    return true;
}

}  // namespace

struct OggMusic::Impl {
    Stream streams[2];  // 0: the music, 1: its game-over part (optional)
    int current = 0;
    // Decoded frames of the current stream (int16, interleaved by its channel count).
    int16_t buf[kBufFrames * 2];
    int bufFrames = 0, bufPos = 0;
    // Linear resampling when the file's rate is not the device's: the frame pair being
    // interpolated and the position between them.
    float prev[2] = {0, 0}, next[2] = {0, 0};
    double frac = 1.0;
    bool primed = false;

    ~Impl() {
        for (Stream& s : streams)
            if (s.open) ov_clear(&s.vf);
    }

    // The next frame of the current stream as floats. At the end of the music it loops; at the
    // end of the game-over part the music follows from its start, as the module does when it
    // loops after the game-over orders.
    bool nextFrame(float* lr) {
        if (bufPos >= bufFrames) {
            int tries = 0;
            for (;;) {
                Stream& s = streams[current];
                int bitstream = 0;
                const long got = ov_read(&s.vf, reinterpret_cast<char*>(buf),
                                         static_cast<int>(sizeof(int16_t) * kBufFrames * s.channels), &bitstream);
                if (got > 0) {
                    bufFrames = static_cast<int>(got / (sizeof(int16_t) * s.channels));
                    bufPos = 0;
                    if (bufFrames > 0) break;
                } else if (got == 0) {  // the end
                    if (++tries > 2) return false;
                    current = 0;
                    if (ov_pcm_seek(&streams[0].vf, 0) != 0) return false;
                } else if (++tries > 8) {  // OV_HOLE / OV_EBADLINK: skip a few, then give up
                    return false;
                }
            }
        }
        const Stream& s = streams[current];
        const int16_t* f = buf + bufPos * s.channels;
        lr[0] = f[0] / 32768.0f;
        lr[1] = s.channels > 1 ? f[1] / 32768.0f : lr[0];
        ++bufPos;
        return true;
    }

    void restart(int stream) {
        current = stream;
        ov_pcm_seek(&streams[current].vf, 0);
        bufFrames = bufPos = 0;
        primed = false;
        frac = 1.0;
    }
};

OggMusic::OggMusic() : impl_(new Impl()) {}
OggMusic::~OggMusic() { delete impl_; }

bool OggMusic::load(Blob main, Blob gameOver) {
    if (!openStream(impl_->streams[0], std::move(main), "the pre-rendered music")) return false;
    if (!gameOver.empty()) openStream(impl_->streams[1], std::move(gameOver), "its game-over part");
    impl_->restart(0);
    return true;
}

int OggMusic::fileRate() const { return impl_->streams[0].rate; }

bool OggMusic::read(int sampleRate, float* out, int frames) {
    Impl& m = *impl_;
    const Stream& s = m.streams[m.current];
    if (!s.open || sampleRate <= 0) {
        std::memset(out, 0, sizeof(float) * 2 * static_cast<size_t>(frames));
        return false;
    }
    if (s.rate == sampleRate) {
        for (int i = 0; i < frames; ++i) {
            if (!m.nextFrame(out + 2 * i)) {
                std::memset(out + 2 * i, 0, sizeof(float) * 2 * static_cast<size_t>(frames - i));
                return false;
            }
        }
        return true;
    }
    const double step = static_cast<double>(s.rate) / sampleRate;
    for (int i = 0; i < frames; ++i) {
        if (!m.primed) {
            if (!m.nextFrame(m.next)) {
                std::memset(out + 2 * i, 0, sizeof(float) * 2 * static_cast<size_t>(frames - i));
                return false;
            }
            m.primed = true;
            m.frac = 1.0;
        }
        while (m.frac >= 1.0) {
            m.prev[0] = m.next[0];
            m.prev[1] = m.next[1];
            if (!m.nextFrame(m.next)) {
                std::memset(out + 2 * i, 0, sizeof(float) * 2 * static_cast<size_t>(frames - i));
                return false;
            }
            m.frac -= 1.0;
        }
        const float t = static_cast<float>(m.frac);
        out[2 * i] = m.prev[0] + (m.next[0] - m.prev[0]) * t;
        out[2 * i + 1] = m.prev[1] + (m.next[1] - m.prev[1]) * t;
        m.frac += step;
    }
    return true;
}

int OggMusic::numOrders() const { return impl_->streams[1].open ? kGameOverMusicOrder + 1 : 1; }

bool OggMusic::setOrder(int order) {
    if (order < 0 || order >= numOrders()) return false;
    impl_->restart(order >= kGameOverMusicOrder && impl_->streams[1].open ? 1 : 0);
    return true;
}

int OggMusic::currentOrder() const { return impl_->current == 1 ? kGameOverMusicOrder : 0; }

}  // namespace as3d::audio
