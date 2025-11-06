#include "Capture.h"
#include "Encoder.h"
#include "Network.h"
#include "MouseTracker.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>

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

    MouseTracker mouseTracker;
    if (!mouseTracker.init()) {
        std::cerr << "Failed to initialize mouse tracker." << std::endl;
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

    // Frame timing control for 60 FPS
    const auto target_frame_time = std::chrono::microseconds(16667); // ~60 FPS
    auto last_frame_time = std::chrono::high_resolution_clock::now();
    
    int frame_count = 0;
    std::atomic<bool> running(true);
    
    // Mouse tracking variables
    int last_mouse_x = -1, last_mouse_y = -1;
    auto last_mouse_time = std::chrono::high_resolution_clock::now();
    
    while (running) {
        auto frame_start = std::chrono::high_resolution_clock::now();
        
        AVFrame* frame = capture.capture_frame();
        if (frame) {
            frame->pts = frame_count;
            
            // Start encoding in parallel (non-blocking if possible)
            if (!encoder.encode(frame, network)) {
                std::cerr << "Encoding failed, stopping..." << std::endl;
                break;
            }
            frame_count++;
            
            // Performance stats every 60 frames (1 second at 60fps)
            if (frame_count % 60 == 0) {
                auto now = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_frame_time);
                double fps = 60000.0 / elapsed.count();
                std::cout << "FPS: " << fps << " (Frame: " << frame_count << ")" << std::endl;
                last_frame_time = now;
            }
        }
        
        // Send mouse position updates (more frequently than video frames)
        auto now = std::chrono::high_resolution_clock::now();
        auto mouse_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_mouse_time);
        if (mouse_elapsed.count() >= 8) {  // Update mouse every 8ms (~120Hz)
            int mouse_x, mouse_y;
            if (mouseTracker.get_mouse_position(mouse_x, mouse_y)) {
                if (mouse_x != last_mouse_x || mouse_y != last_mouse_y) {
                    network.send_mouse_position(mouse_x, mouse_y);
                    last_mouse_x = mouse_x;
                    last_mouse_y = mouse_y;
                }
            }
            last_mouse_time = now;
        }
        
        // Frame rate limiting - sleep if we're ahead of schedule
        auto frame_end = std::chrono::high_resolution_clock::now();
        auto frame_duration = std::chrono::duration_cast<std::chrono::microseconds>(frame_end - frame_start);
        if (frame_duration < target_frame_time) {
            std::this_thread::sleep_for(target_frame_time - frame_duration);
        }
    }

    // Flush the encoder
    encoder.encode(nullptr, network);

    std::cout << "Streaming complete." << std::endl;

    return 0;
}
