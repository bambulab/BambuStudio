#include "AVVideoDecoder.hpp"

extern "C"
{
    #include <libavutil/avutil.h>
    #include <libavutil/imgutils.h>
}

AVVideoDecoder::AVVideoDecoder()
{
    codec_ctx_ = avcodec_alloc_context3(nullptr);
}

AVVideoDecoder::~AVVideoDecoder()
{
    if (sws_ctx_)
        sws_freeContext(sws_ctx_);
    if (frame_)
        av_frame_free(&frame_);
    if (codec_ctx_)
        avcodec_free_context(&codec_ctx_);
}

int AVVideoDecoder::open(Bambu_StreamInfo const &info)
{
    auto codec_id = info.sub_type == AVC1 ? AV_CODEC_ID_H264 : AV_CODEC_ID_MJPEG;
    auto codec    = avcodec_find_decoder(codec_id);
    if (codec == nullptr) {
        fprintf(stderr, "Unsupported codec!\n");
        return -1; // Codec not found
    }
    /* open the coderc */
    if (avcodec_open2(codec_ctx_, codec, nullptr) < 0) {
        fprintf(stderr, "could not open codec\n");
        return -1;
    }

    // Allocate an AVFrame structure
    frame_ = av_frame_alloc();
    if (frame_ == nullptr)
        return -1;

    return 0;
}

int AVVideoDecoder::decode(const Bambu_Sample &sample)
{
    auto pkt = av_packet_alloc();
    int  ret = av_new_packet(pkt, sample.size);
    if (ret == 0)
        memcpy(pkt->data, sample.buffer, size_t(sample.size));
    got_frame_ = avcodec_receive_frame(codec_ctx_, frame_) == 0;
    ret = avcodec_send_packet(codec_ctx_, pkt);
    return ret;
}

int AVVideoDecoder::flush()
{
    int ret = avcodec_send_packet(codec_ctx_, nullptr);
    got_frame_ = avcodec_receive_frame(codec_ctx_, frame_) == 0;
    return ret;
}

void AVVideoDecoder::close()
{
}

bool AVVideoDecoder::toWxImage(wxImage &image, wxSize const &size2)
{
    if (!got_frame_)
        return false;

    auto size = size2;
    if (!size.IsFullySpecified())
        size = {frame_->width, frame_->height };
    if (size.GetWidth() & 0x0f)
        size.SetWidth((size.GetWidth() & ~0x0f) + 0x10);
    AVPixelFormat wxFmt = AV_PIX_FMT_RGB24;
    sws_ctx_   = sws_getCachedContext(sws_ctx_,
                                    frame_->width, frame_->height, AVPixelFormat(frame_->format),
                                    size.GetWidth(), size.GetHeight(), wxFmt,
                                    SWS_GAUSS,
                                    nullptr, nullptr, nullptr);
    if (sws_ctx_ == nullptr)
        return false;
    int length = size.GetWidth() * size.GetHeight() * 3;
    if (bits_.size() < length)
        bits_.resize(length);
    uint8_t * datas[]   = { bits_.data() };
    int      strides[] = { size.GetWidth() * 3 };
    int      result_h = sws_scale(sws_ctx_, frame_->data, frame_->linesize, 0, frame_->height, datas, strides);
    if (result_h != size.GetHeight()) {
        return false;
    }
    image = wxImage(size.GetWidth(), size.GetHeight(), bits_.data());
    return true;
}

bool AVVideoDecoder::toWxBitmap(wxBitmap &bitmap, wxSize const &size2)
{
    if (!got_frame_)
        return false;

    auto size = size2;
    if (!size.IsFullySpecified())
        size = {frame_->width, frame_->height };
    if (size.GetWidth() & 0x0f)
        size.SetWidth((size.GetWidth() & ~0x0f) + 0x10);
    AVPixelFormat wxFmt = AV_PIX_FMT_RGB32;
    sws_ctx_ = sws_getCachedContext(sws_ctx_,
                                    frame_->width, frame_->height, AVPixelFormat(frame_->format),
                                    size.GetWidth(), size.GetHeight(), wxFmt,
                                    SWS_GAUSS,
                                    nullptr, nullptr, nullptr);
    if (sws_ctx_ == nullptr)
        return false;
    int length = size.GetWidth() * size.GetHeight() * 4;
    if (bits_.size() < length)
        bits_.resize(length);
    uint8_t *datas[]   = { bits_.data() };
    int      strides[] = { size.GetWidth() * 4 };
    int      result_h  = sws_scale(sws_ctx_, frame_->data, frame_->linesize, 0, frame_->height, datas, strides);
    if (result_h != size.GetHeight()) {
        return false;
    }
    bitmap = wxBitmap((char const *) bits_.data(), size.GetWidth(), size.GetHeight(), 32);
    return true;
}
