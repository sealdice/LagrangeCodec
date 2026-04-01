//
// Created by Wenxuan Lin on 2025-02-23.
//

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "util.h"
#include "video.h"

int save_frame_as_png(AVFrame* frame, int width, int height, uint8_t*& out, int& out_len) {
    auto png_codec = avcodec_find_encoder(AV_CODEC_ID_PNG);
    if (!png_codec) {
        LC_LOGE("ERROR: AV_CODEC_ID_PNG codec not found\n");
        return -1;
    }

    AVCodecContext* codec_context = avcodec_alloc_context3(png_codec);
    if (!codec_context) {
        LC_LOGE("ERROR: Failed to allocate codec context for AV_CODEC_ID_PNG\n");
        return -1;
    }

    codec_context->bit_rate = 0;
    codec_context->width = width;
    codec_context->height = height;
    codec_context->pix_fmt = AV_PIX_FMT_RGB24;  // RGB format
    codec_context->time_base = AVRational{1, 25}; // Assume 25 fps for example

    if (avcodec_open2(codec_context, png_codec, nullptr) < 0) {
        LC_LOGE("ERROR: Failed to open codec\n");
        avcodec_free_context(&codec_context);
        return -1;
    }

    AVPacket pkt;
    av_init_packet(&pkt);

    int ret = avcodec_send_frame(codec_context, frame);
    if (ret < 0) {
        LC_LOGE("ERROR: Failed to send frame to encoder\n");
        avcodec_free_context(&codec_context);
        return -1;
    }

    ret = avcodec_receive_packet(codec_context, &pkt); // Receive the encoded PNG packet
    if (ret < 0) {
        LC_LOGE("ERROR: Failed to receive packet\n");
        avcodec_free_context(&codec_context);
        return -1;
    }

    out_len = pkt.size;
    out = static_cast<uint8_t*>(av_malloc(out_len));
    memcpy(out, pkt.data, out_len);

    av_packet_unref(&pkt);
    avcodec_free_context(&codec_context);

    return 0;
}

EXPORT int video_first_frame(uint8_t* video_data, int data_len, uint8_t*& out, int& out_len) {
    AVFormatContext* format_context = nullptr;

    int ret_code = create_format_context(video_data, data_len, &format_context);
    if (ret_code < 0) {
        LC_LOGE("ERROR: failed to create format context\n");
        return -1;
    }

    ret_code = avformat_open_input(&format_context, nullptr, nullptr, nullptr);
    if (ret_code < 0) {
        LC_LOGE("ERROR: failed to open the media stream\n");
        return -1;
    }

    if (avformat_find_stream_info(format_context, nullptr) < 0) {
        LC_LOGE("ERROR: failed to find stream info\n");
        return -1;
    }

    const AVCodec *codec = nullptr;
    int video_stream_index = -1;
    for (unsigned int i = 0; i < format_context->nb_streams; i++) {
        if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            codec = avcodec_find_decoder(format_context->streams[i]->codecpar->codec_id);
            break;
        }
    }

    if (video_stream_index == -1) {
        LC_LOGE("ERROR: no video stream found\n");
        return -1;
    }

    AVCodecContext* codec_context = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_context, format_context->streams[video_stream_index]->codecpar);

    if (avcodec_open2(codec_context, codec, nullptr) < 0) {
        LC_LOGE("ERROR: failed to open the codec\n");
        return -1;
    }

    AVFrame* frame = av_frame_alloc();
    AVPacket packet;

    while (av_read_frame(format_context, &packet) >= 0) {
        if (packet.stream_index == video_stream_index) {
            int response = avcodec_send_packet(codec_context, &packet);
            if (response < 0) {
                LC_LOGE("ERROR: failed to send packet\n");
                return -1;
            }

            response = avcodec_receive_frame(codec_context, frame);
            if (response == 0) { // Frame successfully decoded
                break;
            }
        }
        av_packet_unref(&packet);
    }

    // Save the frame as an image
    SwsContext* sws_context = sws_getContext(
        frame->width, frame->height, codec_context->pix_fmt,
        frame->width, frame->height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    AVFrame* rgb_frame = av_frame_alloc();
    rgb_frame->width = frame->width;
    rgb_frame->height = frame->height;
    rgb_frame->format = AV_PIX_FMT_RGB24;

    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, frame->width, frame->height, 1);
    auto buffer = static_cast<uint8_t *>(av_malloc(num_bytes * sizeof(uint8_t)));
    av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, buffer, AV_PIX_FMT_RGB24, frame->width, frame->height, 1);

    sws_scale(sws_context, frame->data, frame->linesize, 0, frame->height, rgb_frame->data, rgb_frame->linesize);
    save_frame_as_png(rgb_frame, rgb_frame->width, rgb_frame->height, out, out_len);

    av_free(format_context->pb->buffer);
    av_free(buffer);
    av_frame_free(&rgb_frame);
    sws_freeContext(sws_context);
    av_frame_free(&frame);
    avcodec_free_context(&codec_context);
    avformat_close_input(&format_context);

    return 0;
}

EXPORT int video_get_size(uint8_t* video_data, int data_len, VideoInfo& info) {
    AVFormatContext* format_context = nullptr;

    int ret_code = create_format_context(video_data, data_len, &format_context);
    if (ret_code < 0) {
        av_free(format_context->pb->buffer);
        return -1;
    }

    ret_code = avformat_open_input(&format_context, nullptr, nullptr, nullptr);
    if (ret_code < 0) {
        av_free(format_context->pb->buffer);
        return -1;
    }

    if (avformat_find_stream_info(format_context, nullptr) < 0) {
        av_free(format_context->pb->buffer);
        avformat_close_input(&format_context);
        return {};
    }

    AVCodecParameters* codec_parameters = nullptr;
    int index = -1;
    for (unsigned int i = 0; i < format_context->nb_streams; i++) {
        if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            index = i;
            codec_parameters = format_context->streams[i]->codecpar;
            break;
        }
    }

    if (index == -1) {
        av_free(format_context->pb->buffer);
        avformat_close_input(&format_context);
        return -1;
    }

    info = { codec_parameters->width, codec_parameters->height, (format_context->duration / AV_TIME_BASE) };

    av_free(format_context->pb->buffer);
    avformat_close_input(&format_context);
    return 0;
}
