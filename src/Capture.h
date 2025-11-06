#ifndef CAPTURE_H
#define CAPTURE_H

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libswscale/swscale.h>
}

#include <chrono>

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
    
    // Performance tracking
    std::chrono::high_resolution_clock::time_point last_frame_time;
    int64_t frame_count = 0;
};

#endif // CAPTURE_H
