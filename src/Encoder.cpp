#include "Encoder.h"
#include <iostream>
#include <X11/Xlib.h>
#include <libavutil/opt.h>

extern "C" {
#include <libavutil/hwcontext_vaapi.h>
}

Encoder::Encoder() {}

Encoder::~Encoder() {
    if (codecContext) avcodec_free_context(&codecContext);
    if (hw_frames_ctx) av_buffer_unref(&hw_frames_ctx);
    if (hw_device_ctx) av_buffer_unref(&hw_device_ctx);
}

bool Encoder::init(int width, int height, int framerate_num, int framerate_den) {
    const AVCodec* codec = avcodec_find_encoder_by_name("h264_vaapi");
    if (!codec) {
        std::cerr << "Could not find h264_vaapi encoder." << std::endl;
        return false;
    }

    codecContext = avcodec_alloc_context3(codec);
    if (!codecContext) {
        std::cerr << "Could not allocate codec context." << std::endl;
        return false;
    }

    // Create VA-API hardware device context
    int ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI, NULL, NULL, 0);
    if (ret < 0) {
        std::cerr << "Failed to create VA-API hardware device context." << std::endl;
        return false;
    }

    // Create hardware frames context
    hw_frames_ctx = av_hwframe_ctx_alloc(hw_device_ctx);
    if (!hw_frames_ctx) {
        std::cerr << "Failed to create VA-API hardware frames context." << std::endl;
        return false;
    }

    AVHWFramesContext* frames_ctx = (AVHWFramesContext*)(hw_frames_ctx->data);
    frames_ctx->format = AV_PIX_FMT_VAAPI;
    frames_ctx->sw_format = AV_PIX_FMT_NV12;
    frames_ctx->width = width;
    frames_ctx->height = height;
    frames_ctx->initial_pool_size = 20;

    if (av_hwframe_ctx_init(hw_frames_ctx) < 0) {
        std::cerr << "Failed to initialize VA-API hardware frames context." << std::endl;
        return false;
    }

    codecContext->hw_frames_ctx = av_buffer_ref(hw_frames_ctx);
    if (!codecContext->hw_frames_ctx) {
        std::cerr << "Failed to set hardware frames context." << std::endl;
        return false;
    }

    codecContext->width = width;
    codecContext->height = height;
    codecContext->pix_fmt = AV_PIX_FMT_VAAPI;
    codecContext->time_base = {framerate_den, framerate_num};
    codecContext->framerate = {framerate_num, framerate_den};
    codecContext->gop_size = 30;
    codecContext->profile = FF_PROFILE_H264_CONSTRAINED_BASELINE;
    codecContext->global_quality = 25; // CQP value

    if (avcodec_open2(codecContext, codec, nullptr) < 0) {
        std::cerr << "Could not open codec." << std::endl;
        return false;
    }

    std::cout << "h264_vaapi encoder initialized successfully." << std::endl;
    return true;
}

AVCodecContext* Encoder::get_codec_context() {
    return codecContext;
}

bool Encoder::encode(AVFrame* sw_frame, Network& network) {
    if (sw_frame) {
        AVFrame* hw_frame = av_frame_alloc();
        if (!hw_frame) {
            std::cerr << "Could not allocate hardware frame." << std::endl;
            return false;
        }

        if (av_hwframe_get_buffer(hw_frames_ctx, hw_frame, 0) < 0) {
            std::cerr << "Could not get hardware frame buffer." << std::endl;
            av_frame_free(&hw_frame);
            return false;
        }

        if (av_hwframe_transfer_data(hw_frame, sw_frame, 0) < 0) {
            std::cerr << "Could not transfer data to hardware frame." << std::endl;
            av_frame_free(&hw_frame);
            return false;
        }

        hw_frame->pts = sw_frame->pts;

        if (avcodec_send_frame(codecContext, hw_frame) < 0) {
            std::cerr << "Could not send frame to encoder." << std::endl;
            av_frame_free(&hw_frame);
            return false;
        }

        av_frame_free(&hw_frame);
    } else {
        if (avcodec_send_frame(codecContext, nullptr) < 0) {
            std::cerr << "Could not flush encoder." << std::endl;
            return false;
        }
    }

    AVPacket packet;
    av_init_packet(&packet);

    while (true) {
        int ret = avcodec_receive_packet(codecContext, &packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            std::cerr << "Error during encoding." << std::endl;
            return false;
        }

        network.send_rtp_packet(packet.data, packet.size, packet.pts, false);
        av_packet_unref(&packet);
    }

    return true;
}
