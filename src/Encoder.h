#ifndef ENCODER_H
#define ENCODER_H

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vaapi.h>
}

#include "Network.h"
#include <fstream>

class Encoder {
public:
    Encoder();
    ~Encoder();

    bool init(int width, int height, int framerate_num, int framerate_den);
    AVCodecContext* get_codec_context();
    bool encode(AVFrame* sw_frame, Network& network);

private:
    AVCodecContext* codecContext = nullptr;
    AVBufferRef* hw_device_ctx = nullptr;
    AVBufferRef* hw_frames_ctx = nullptr;
};

#endif // ENCODER_H
