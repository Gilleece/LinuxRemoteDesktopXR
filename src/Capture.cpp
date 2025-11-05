#include "Capture.h"
#include <iostream>

Capture::Capture() {
    avdevice_register_all();
}

Capture::~Capture() {
    if (sws_ctx) sws_freeContext(sws_ctx);
    if (codecContext) avcodec_free_context(&codecContext);
    if (formatContext) avformat_close_input(&formatContext);
    if (frame) av_frame_free(&frame);
    if (sw_frame) av_frame_free(&sw_frame);
}

bool Capture::init(const char* displayName, const char* framerate, const char* videoSize) {
    const AVInputFormat* inputFormat = av_find_input_format("x11grab");
    if (!inputFormat) {
        std::cerr << "Could not find x11grab input format." << std::endl;
        return false;
    }

    AVDictionary* options = nullptr;
    av_dict_set(&options, "framerate", framerate, 0);
    av_dict_set(&options, "video_size", videoSize, 0);

    if (avformat_open_input(&formatContext, displayName, const_cast<AVInputFormat*>(inputFormat), &options) != 0) {
        std::cerr << "Could not open input device." << std::endl;
        return false;
    }

    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        std::cerr << "Could not find stream information." << std::endl;
        return false;
    }

    videoStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex < 0) {
        std::cerr << "Could not find video stream." << std::endl;
        return false;
    }

    AVStream* videoStream = formatContext->streams[videoStreamIndex];
    const AVCodec* codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
    if (!codec) {
        std::cerr << "Could not find decoder." << std::endl;
        return false;
    }

    codecContext = avcodec_alloc_context3(codec);
    if (!codecContext) {
        std::cerr << "Could not allocate codec context." << std::endl;
        return false;
    }

    if (avcodec_parameters_to_context(codecContext, videoStream->codecpar) < 0) {
        std::cerr << "Could not copy codec parameters to context." << std::endl;
        return false;
    }

    if (avcodec_open2(codecContext, codec, nullptr) < 0) {
        std::cerr << "Could not open codec." << std::endl;
        return false;
    }

    frame = av_frame_alloc();
    sw_frame = av_frame_alloc();
    if (!frame || !sw_frame) {
        std::cerr << "Could not allocate frame." << std::endl;
        return false;
    }
    
    sw_frame->format = AV_PIX_FMT_NV12;
    sw_frame->width = codecContext->width;
    sw_frame->height = codecContext->height;
    if (av_frame_get_buffer(sw_frame, 0) < 0) {
        std::cerr << "Could not allocate buffer for sw_frame" << std::endl;
        return false;
    }

    return true;
}

AVFrame* Capture::capture_frame() {
    AVPacket packet;
    av_init_packet(&packet);

    while (av_read_frame(formatContext, &packet) >= 0) {
        if (packet.stream_index == videoStreamIndex) {
            if (avcodec_send_packet(codecContext, &packet) == 0) {
                int ret = avcodec_receive_frame(codecContext, frame);
                if (ret == 0) {
                    sws_ctx = sws_getContext(codecContext->width, codecContext->height, codecContext->pix_fmt,
                                             codecContext->width, codecContext->height, AV_PIX_FMT_NV12,
                                             SWS_BILINEAR, nullptr, nullptr, nullptr);
                    if (!sws_ctx) {
                        std::cerr << "Could not initialize sws context" << std::endl;
                        av_packet_unref(&packet);
                        return nullptr;
                    }
                    sws_scale(sws_ctx, (const uint8_t* const*)frame->data, frame->linesize, 0, codecContext->height,
                              sw_frame->data, sw_frame->linesize);
                    
                    av_packet_unref(&packet);
                    return sw_frame;
                }
            }
        }
        av_packet_unref(&packet);
    }
    return nullptr;
}

AVCodecContext* Capture::get_codec_context() {
    return codecContext;
}
