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
    if (!encoder.init(capture.get_codec_context()->width, capture.get_codec_context()->height, 60)) {
        return 1;
    }

    Network network;
    if (!network.init(5004)) {
        return 1;
    }

    if (!network.wait_for_client()) {
        std::cerr << "Failed to get client connection." << std::endl;
        return 1;
    }

    // Send SPS/PPS
    AVPacket* sps_pps_packet = encoder.get_extradata_packet();
    if (sps_pps_packet) {
        network.send_rtp_packet(sps_pps_packet->data, sps_pps_packet->size, 0, true);
    }

    int frame_count = 0;
    while (true) { // Run indefinitely
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
