#include "Encoder.h"
#include <iostream>

Encoder::Encoder() {}

Encoder::~Encoder() {
    if (codecContext) avcodec_free_context(&codecContext);
    if (hw_frames_ctx) av_buffer_unref(&hw_frames_ctx);
    if (hw_device_ctx) av_buffer_unref(&hw_device_ctx);
}

bool Encoder::init(int width, int height, int framerate) {
    // Find the VAAPI H.264 encoder
    const AVCodec *codec = avcodec_find_encoder_by_name("h264_vaapi");
    if (!codec) {
        std::cerr << "h264_vaapi encoder not found" << std::endl;
        return false;
    }

    // Create codec context
    codecContext = avcodec_alloc_context3(codec);
    if (!codecContext) {
        std::cerr << "Could not allocate codec context" << std::endl;
        return false;
    }

    // Set codec parameters
    codecContext->width = width;
    codecContext->height = height;
    codecContext->time_base = {1, framerate};
    codecContext->framerate = {framerate, 1};
    codecContext->gop_size = 30;  // Keyframe every 0.5 seconds at 60fps for good balance
    codecContext->max_b_frames = 0;  // No B-frames
    codecContext->pix_fmt = AV_PIX_FMT_VAAPI;

    // Ultra-low latency flags
    codecContext->flags |= AV_CODEC_FLAG_LOW_DELAY;
    codecContext->flags2 |= AV_CODEC_FLAG2_FAST;
    
    // VAAPI-specific low latency options (using correct VAAPI parameters)
    av_opt_set(codecContext->priv_data, "quality", "3", 0);        // Valid range 0-7, 3 is good quality
    av_opt_set(codecContext->priv_data, "low_power", "1", 0);      // Use low-power encoding for speed
    av_opt_set(codecContext->priv_data, "async_depth", "1", 0);    // Minimize async queue

    // Use CQP (Constant Quality Parameter) mode - the only mode supported by your VAAPI driver
    codecContext->global_quality = 25;  // Quality level (lower = better quality)
    codecContext->bit_rate = 0;         // Set to 0 to use CQP mode
    codecContext->rc_max_rate = 0;
    codecContext->rc_min_rate = 0;
    codecContext->rc_buffer_size = 0;

    // Profile
    codecContext->profile = FF_PROFILE_H264_CONSTRAINED_BASELINE;
    
    // Create hardware device context
    if (av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI, "/dev/dri/renderD128", NULL, 0) < 0) {
        std::cerr << "Failed to create VAAPI hardware device context." << std::endl;
        return false;
    }
    
    // Create hardware frames context
    AVBufferRef *hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx);
    if (!hw_frames_ref) {
        std::cerr << "Failed to create VAAPI hardware frames context." << std::endl;
        return false;
    }
    AVHWFramesContext *frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
    frames_ctx->format    = AV_PIX_FMT_VAAPI;
    frames_ctx->sw_format = AV_PIX_FMT_NV12;
    frames_ctx->width     = codecContext->width;
    frames_ctx->height    = codecContext->height;
    frames_ctx->initial_pool_size = 4;  // Minimal pool for low latency
    if (av_hwframe_ctx_init(hw_frames_ref) < 0) {
        std::cerr << "Failed to initialize VAAPI hardware frames context." << std::endl;
        av_buffer_unref(&hw_frames_ref);
        return false;
    }
    codecContext->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
    av_buffer_unref(&hw_frames_ref);

    // Open codec
    if (avcodec_open2(codecContext, codec, NULL) < 0) {
        std::cerr << "Could not open codec" << std::endl;
        return false;
    }

    return true;
}AVCodecContext* Encoder::get_codec_context() {
    return codecContext;
}

AVPacket* Encoder::get_extradata_packet() {
    if (!codecContext || !codecContext->extradata) {
        return nullptr;
    }
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        return nullptr;
    }
    pkt->size = codecContext->extradata_size;
    pkt->data = (uint8_t*)av_malloc(pkt->size);
    if (!pkt->data) {
        av_packet_free(&pkt);
        return nullptr;
    }
    memcpy(pkt->data, codecContext->extradata, pkt->size);
    return pkt;
}

bool Encoder::encode(AVFrame* sw_frame, Network& network) {
    int ret;

    if (sw_frame) {
        // It's a regular frame, so transfer it to the GPU.
        AVFrame *hw_frame = av_frame_alloc();
        if (!hw_frame) {
            std::cerr << "Failed to allocate hardware frame." << std::endl;
            return false;
        }

        if (av_hwframe_get_buffer(codecContext->hw_frames_ctx, hw_frame, 0) < 0) {
            std::cerr << "Failed to get buffer for hardware frame." << std::endl;
            av_frame_free(&hw_frame);
            return false;
        }

        if (av_hwframe_transfer_data(hw_frame, sw_frame, 0) < 0) {
            std::cerr << "Failed to transfer data to hardware frame." << std::endl;
            av_frame_free(&hw_frame);
            return false;
        }
        
        hw_frame->pts = sw_frame->pts;

        ret = avcodec_send_frame(codecContext, hw_frame);
        av_frame_free(&hw_frame); // Free the container, the buffer is now owned by the codec
        if (ret < 0) {
            char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
            std::cerr << "Error sending a frame for encoding: " << err_buf << std::endl;
            return false;
        }
    } else {
        // If sw_frame is null, it's a flush request. Send a null frame to the encoder.
        ret = avcodec_send_frame(codecContext, NULL);
        if (ret < 0) {
            char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
            std::cerr << "Error sending a frame for encoding (flush): " << err_buf << std::endl;
            return false;
        }
    }

    // Receive encoded packets
    AVPacket packet;
    
    while (true) {
        av_init_packet(&packet);
        packet.data = NULL;
        packet.size = 0;

        ret = avcodec_receive_packet(codecContext, &packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
            std::cerr << "Error during encoding: " << err_buf << std::endl;
            break;
        }

        network.send_rtp_packet(packet.data, packet.size, packet.pts, false);
        av_packet_unref(&packet);
    }

    return true;
}
