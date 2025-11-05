#include "Capture.h"
#include "Encoder.h"
#include "Network.h"
#include <iostream>

int main() {
    const char* displayName = getenv("DISPLAY");
    if (!displayName) {
        std::cerr << "DISPLAY environment variable not set." << std::endl;
        return -1;
    }

    Capture capture;
    if (!capture.init(displayName, "60", "1920x1080")) {
        return -1;
    }

    AVCodecContext* capture_codec_ctx = capture.get_codec_context();

    Encoder encoder;
    if (!encoder.init(capture_codec_ctx->width, capture_codec_ctx->height, 60, 1)) {
        return -1;
    }

    Network network;
    if (!network.init("127.0.0.1", 4242)) {
        return -1;
    }

    // Send SPS/PPS
    AVCodecContext* encoder_ctx = encoder.get_codec_context();
    if (encoder_ctx->extradata_size > 0) {
        network.send_rtp_packet(encoder_ctx->extradata, encoder_ctx->extradata_size, 0, true);
    }

    int frame_count = 0;
    while (frame_count < 300) { // Capture 300 frames (5 seconds at 60fps)
        AVFrame* frame = capture.capture_frame();
        if (frame) {
            frame->pts = frame_count;
            if (!encoder.encode(frame, network)) {
                break;
            }
            frame_count++;
        }
    }

    // Flush the encoder
    encoder.encode(nullptr, network);

    std::cout << "Streaming complete." << std::endl;

    return 0;
}
