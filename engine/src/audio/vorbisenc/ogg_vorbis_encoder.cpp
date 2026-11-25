// Ogg Vorbis encoding over libvorbis (as3d/ogg_vorbis_encoder.h). The calls follow libvorbis's
// examples/encoder_example.c, as oggenc does: the three headers on pages of their own, then the
// audio packets paged by ogg_stream_pageout.
#include "as3d/ogg_vorbis_encoder.h"

#include <vorbis/vorbisenc.h>

#include <cstring>

namespace as3d::audio {

namespace {
constexpr int kSerial = 0x41334453;  // fixed: the same samples give the same file
}

struct OggVorbisEncoder::Impl {
    vorbis_info vi;
    vorbis_comment vc;
    vorbis_dsp_state vd;
    vorbis_block vb;
    ogg_stream_state os;
    bool infoInit = false, dspInit = false, finished = false;
};

OggVorbisEncoder::OggVorbisEncoder() : impl_(new Impl()) {}

OggVorbisEncoder::~OggVorbisEncoder() {
    if (impl_->dspInit) {
        ogg_stream_clear(&impl_->os);
        vorbis_block_clear(&impl_->vb);
        vorbis_dsp_clear(&impl_->vd);
    }
    if (impl_->infoInit) {
        vorbis_comment_clear(&impl_->vc);
        vorbis_info_clear(&impl_->vi);
    }
    delete impl_;
}

bool OggVorbisEncoder::begin(int channels, int rate, double quality) {
    if (impl_->infoInit || channels < 1 || channels > 2 || rate < 8000) {
        error_ = "bad encoder setup";
        return false;
    }
    channels_ = channels;
    vorbis_info_init(&impl_->vi);
    vorbis_comment_init(&impl_->vc);
    impl_->infoInit = true;
    if (vorbis_encode_init_vbr(&impl_->vi, channels, rate, static_cast<float>(quality / 10.0)) != 0) {
        error_ = "libvorbis refused the mode (channels, rate, quality)";
        return false;
    }
    vorbis_comment_add_tag(&impl_->vc, "ENCODER", "OpenAirStrike");
    vorbis_analysis_init(&impl_->vd, &impl_->vi);
    vorbis_block_init(&impl_->vd, &impl_->vb);
    ogg_stream_init(&impl_->os, kSerial);
    impl_->dspInit = true;

    ogg_packet header, comments, codebooks;
    vorbis_analysis_headerout(&impl_->vd, &impl_->vc, &header, &comments, &codebooks);
    ogg_stream_packetin(&impl_->os, &header);
    ogg_stream_packetin(&impl_->os, &comments);
    ogg_stream_packetin(&impl_->os, &codebooks);
    drain(true);  // the audio starts on a fresh page
    return true;
}

void OggVorbisEncoder::drain(bool flushAll) {
    ogg_page og;
    for (;;) {
        const int got = flushAll ? ogg_stream_flush(&impl_->os, &og) : ogg_stream_pageout(&impl_->os, &og);
        if (!got) break;
        bytes_.insert(bytes_.end(), og.header, og.header + og.header_len);
        bytes_.insert(bytes_.end(), og.body, og.body + og.body_len);
    }
}

bool OggVorbisEncoder::write(const std::int16_t* interleaved, std::size_t frames) {
    if (!impl_->dspInit || impl_->finished) {
        error_ = "the encoder is not running";
        return false;
    }
    constexpr std::size_t kBlock = 1024;
    while (frames > 0) {
        const std::size_t n = frames < kBlock ? frames : kBlock;
        float** buf = vorbis_analysis_buffer(&impl_->vd, static_cast<int>(n));
        for (std::size_t i = 0; i < n; ++i)
            for (int c = 0; c < channels_; ++c)
                buf[c][i] = static_cast<float>(interleaved[i * static_cast<std::size_t>(channels_) + static_cast<std::size_t>(c)]) / 32768.0f;
        vorbis_analysis_wrote(&impl_->vd, static_cast<int>(n));
        interleaved += n * static_cast<std::size_t>(channels_);
        frames -= n;
        ogg_packet op;
        while (vorbis_analysis_blockout(&impl_->vd, &impl_->vb) == 1) {
            vorbis_analysis(&impl_->vb, nullptr);
            vorbis_bitrate_addblock(&impl_->vb);
            while (vorbis_bitrate_flushpacket(&impl_->vd, &op)) ogg_stream_packetin(&impl_->os, &op);
        }
        drain(false);
    }
    return true;
}

bool OggVorbisEncoder::finish() {
    if (!impl_->dspInit || impl_->finished) {
        error_ = "the encoder is not running";
        return false;
    }
    vorbis_analysis_wrote(&impl_->vd, 0);  // end of stream
    ogg_packet op;
    while (vorbis_analysis_blockout(&impl_->vd, &impl_->vb) == 1) {
        vorbis_analysis(&impl_->vb, nullptr);
        vorbis_bitrate_addblock(&impl_->vb);
        while (vorbis_bitrate_flushpacket(&impl_->vd, &op)) ogg_stream_packetin(&impl_->os, &op);
    }
    drain(true);
    impl_->finished = true;
    return true;
}

}  // namespace as3d::audio
