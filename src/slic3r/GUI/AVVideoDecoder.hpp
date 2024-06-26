#ifndef AVVIDEODECODER_HPP
#define AVVIDEODECODER_HPP

#include "BambuTunnel.h"

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libswscale/swscale.h>
}
class wxBitmap;

class AVVideoDecoder
{
public:
    AVVideoDecoder();

    ~AVVideoDecoder();

public:
    int  open(Bambu_StreamInfo const &info);

    int  decode(Bambu_Sample const &sample);

    int  flush();

    void close();

    bool toWxImage(wxImage &image, wxSize const &size);

    bool toWxBitmap(wxBitmap &bitmap, wxSize const & size);

private:
    AVCodecContext *codec_ctx_ = nullptr;
    AVFrame *       frame_     = nullptr;
    SwsContext *    sws_ctx_   = nullptr;
    bool got_frame_ = false;
};

#endif // AVVIDEODECODER_HPP
