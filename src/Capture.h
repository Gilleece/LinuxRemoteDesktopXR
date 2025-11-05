#ifndef CAPTURE_H
#define CAPTURE_H

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libswscale/swscale.h>
}

class Capture {
public:
    Capture();
    ~Capture();

    bool init(const char* displayName, const char* framerate, const char* videoSize);
    AVFrame* capture_frame();
    AVCodecContext* get_codec_context();
    AVBufferRef* get_hw_device_ctx();

private:
    AVFormatContext* formatContext = nullptr;
    AVCodecContext* codecContext = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* sw_frame = nullptr;
    SwsContext* sws_ctx = nullptr;
    int videoStreamIndex = -1;
};

#endif // CAPTURE_H
