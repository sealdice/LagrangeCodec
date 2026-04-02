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

int save_frame_as_png(AVFrame* frame, int width, int height, uint8_t** out, int* out_len) {
    if (!out || !out_len) {
        return LAGRANGECODEC_ERROR_INVALID_ARGUMENT;
    }

    *out = nullptr;
    *out_len = 0;

    if (!frame || width <= 0 || height <= 0) {
        return LAGRANGECODEC_ERROR_INVALID_ARGUMENT;
    }

    auto png_codec = avcodec_find_encoder(AV_CODEC_ID_PNG);
    if (!png_codec) {
        LC_LOGE("ERROR: AV_CODEC_ID_PNG codec not found\n");
        return LAGRANGECODEC_ERROR_CODEC_NOT_FOUND;
    }

    AVCodecContext* codec_context = avcodec_alloc_context3(png_codec);
    if (!codec_context) {
        LC_LOGE("ERROR: Failed to allocate codec context for AV_CODEC_ID_PNG\n");
        return LAGRANGECODEC_ERROR_ALLOCATION_FAILED;
    }

    codec_context->bit_rate = 0;
    codec_context->width = width;
    codec_context->height = height;
    codec_context->pix_fmt = AV_PIX_FMT_RGB24;  // RGB format
    codec_context->time_base = AVRational{1, 25}; // Assume 25 fps for example

    if (avcodec_open2(codec_context, png_codec, nullptr) < 0) {
        LC_LOGE("ERROR: Failed to open codec\n");
        avcodec_free_context(&codec_context);
        return LAGRANGECODEC_ERROR_CODEC_OPEN_FAILED;
    }

    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        avcodec_free_context(&codec_context);
        return LAGRANGECODEC_ERROR_ALLOCATION_FAILED;
    }

    int ret = avcodec_send_frame(codec_context, frame);
    if (ret < 0) {
        LC_LOGE("ERROR: Failed to send frame to encoder\n");
        av_packet_free(&pkt);
        avcodec_free_context(&codec_context);
        return LAGRANGECODEC_ERROR_DECODE_FAILED;
    }

    ret = avcodec_receive_packet(codec_context, pkt); // Receive the encoded PNG packet
    if (ret < 0) {
        LC_LOGE("ERROR: Failed to receive packet\n");
        av_packet_free(&pkt);
        avcodec_free_context(&codec_context);
        return LAGRANGECODEC_ERROR_OUTPUT_FAILED;
    }

    *out_len = pkt->size;
    *out = static_cast<uint8_t*>(av_malloc(*out_len));
    if (!*out) {
        av_packet_free(&pkt);
        avcodec_free_context(&codec_context);
        return LAGRANGECODEC_ERROR_ALLOCATION_FAILED;
    }
    memcpy(*out, pkt->data, *out_len);

    av_packet_free(&pkt);
    avcodec_free_context(&codec_context);

    return LAGRANGECODEC_OK;
}

EXPORT int video_first_frame(uint8_t* video_data, int data_len, uint8_t** out, int* out_len) {
    LC_TRACE_LITERAL("TRACE_LITERAL video_first_frame enter");
    AVFormatContext* format_context = nullptr;
    AVCodecContext* codec_context = nullptr;
    const AVCodec* codec = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* rgb_frame = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* sws_context = nullptr;
    uint8_t* buffer = nullptr;
    int result = LAGRANGECODEC_ERROR_DECODE_FAILED;
    int video_stream_index = -1;
    bool decoded = false;
    int num_bytes = 0;

    if (out) {
        *out = nullptr;
    }
    if (out_len) {
        *out_len = 0;
    }

    if (!video_data || data_len <= 0 || !out || !out_len) {
        LC_LOGE("video_first_frame invalid args data=%p len=%d out=%p out_len=%p", video_data, data_len, out, out_len);
        return LAGRANGECODEC_ERROR_INVALID_ARGUMENT;
    }

    LC_LOGI("video_first_frame enter data=%p len=%d", video_data, data_len);

    int ret_code = create_format_context(video_data, data_len, &format_context);
    if (ret_code != LAGRANGECODEC_OK) {
        LC_LOGE("ERROR: failed to create format context\n");
        return ret_code;
    }

    ret_code = avformat_open_input(&format_context, nullptr, nullptr, nullptr);
    if (ret_code < 0) {
        LC_LOGE("ERROR: failed to open the media stream ret=%d", ret_code);
        result = LAGRANGECODEC_ERROR_OPEN_INPUT_FAILED;
        goto cleanup;
    }

    if (avformat_find_stream_info(format_context, nullptr) < 0) {
        LC_LOGE("ERROR: failed to find stream info");
        result = LAGRANGECODEC_ERROR_STREAM_INFO_FAILED;
        goto cleanup;
    }
    LC_LOGI("video_first_frame stream_count=%u", format_context->nb_streams);

    for (unsigned int i = 0; i < format_context->nb_streams; i++) {
        if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            codec = avcodec_find_decoder(format_context->streams[i]->codecpar->codec_id);
            break;
        }
    }

    if (video_stream_index == -1) {
        LC_LOGE("ERROR: no video stream found");
        result = LAGRANGECODEC_ERROR_STREAM_NOT_FOUND;
        goto cleanup;
    }
    LC_LOGI("video_first_frame selected stream_index=%d codec=%s", video_stream_index, codec && codec->name ? codec->name : "(null)");

    if (!codec) {
        result = LAGRANGECODEC_ERROR_CODEC_NOT_FOUND;
        goto cleanup;
    }

    codec_context = avcodec_alloc_context3(codec);
    if (!codec_context) {
        result = LAGRANGECODEC_ERROR_ALLOCATION_FAILED;
        goto cleanup;
    }
    ret_code = avcodec_parameters_to_context(codec_context, format_context->streams[video_stream_index]->codecpar);
    if (ret_code < 0) {
        result = LAGRANGECODEC_ERROR_CODEC_OPEN_FAILED;
        goto cleanup;
    }

    if (avcodec_open2(codec_context, codec, nullptr) < 0) {
        LC_LOGE("ERROR: failed to open the codec\n");
        result = LAGRANGECODEC_ERROR_CODEC_OPEN_FAILED;
        goto cleanup;
    }

    frame = av_frame_alloc();
    packet = av_packet_alloc();
    if (!frame || !packet) {
        result = LAGRANGECODEC_ERROR_ALLOCATION_FAILED;
        goto cleanup;
    }

    while (av_read_frame(format_context, packet) >= 0) {
        LC_LOGI("video_first_frame av_read_frame stream=%d size=%d", packet->stream_index, packet->size);
        if (packet->stream_index == video_stream_index) {
            int response = avcodec_send_packet(codec_context, packet);
            if (response < 0) {
                LC_LOGE("ERROR: failed to send packet ret=%d", response);
                av_packet_unref(packet);
                result = LAGRANGECODEC_ERROR_DECODE_FAILED;
                goto cleanup;
            }

            response = avcodec_receive_frame(codec_context, frame);
            if (response == 0) { // Frame successfully decoded
                LC_LOGI("video_first_frame decoded frame width=%d height=%d format=%d", frame->width, frame->height, frame->format);
                decoded = true;
                av_packet_unref(packet);
                break;
            }
            if (response != AVERROR(EAGAIN) && response != AVERROR_EOF) {
                LC_LOGE("video_first_frame receive_frame failed ret=%d", response);
                av_packet_unref(packet);
                result = LAGRANGECODEC_ERROR_DECODE_FAILED;
                goto cleanup;
            }
        }
        av_packet_unref(packet);
    }

    if (!decoded || frame->width <= 0 || frame->height <= 0) {
        LC_LOGE("video_first_frame decode failed decoded=%d width=%d height=%d", decoded ? 1 : 0, frame ? frame->width : -1, frame ? frame->height : -1);
        result = LAGRANGECODEC_ERROR_DECODE_FAILED;
        goto cleanup;
    }

    // Save the frame as an image
    sws_context = sws_getContext(
        frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
        frame->width, frame->height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_context) {
        result = LAGRANGECODEC_ERROR_CONVERSION_FAILED;
        goto cleanup;
    }

    rgb_frame = av_frame_alloc();
    if (!rgb_frame) {
        result = LAGRANGECODEC_ERROR_ALLOCATION_FAILED;
        goto cleanup;
    }
    rgb_frame->width = frame->width;
    rgb_frame->height = frame->height;
    rgb_frame->format = AV_PIX_FMT_RGB24;

    num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, frame->width, frame->height, 1);
    if (num_bytes <= 0) {
        result = LAGRANGECODEC_ERROR_OUTPUT_FAILED;
        goto cleanup;
    }
    buffer = static_cast<uint8_t *>(av_malloc(static_cast<size_t>(num_bytes)));
    if (!buffer) {
        result = LAGRANGECODEC_ERROR_ALLOCATION_FAILED;
        goto cleanup;
    }
    ret_code = av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, buffer, AV_PIX_FMT_RGB24, frame->width, frame->height, 1);
    if (ret_code < 0) {
        result = LAGRANGECODEC_ERROR_OUTPUT_FAILED;
        goto cleanup;
    }

    sws_scale(sws_context, frame->data, frame->linesize, 0, frame->height, rgb_frame->data, rgb_frame->linesize);
    result = save_frame_as_png(rgb_frame, rgb_frame->width, rgb_frame->height, out, out_len);
    LC_LOGI("video_first_frame save_frame_as_png result=%d out=%p out_len=%d", result, out ? *out : nullptr, out_len ? *out_len : -1);

cleanup:
    LC_LOGI("video_first_frame cleanup result=%d format_context=%p codec_context=%p frame=%p rgb_frame=%p packet=%p sws=%p buffer=%p", result, format_context, codec_context, frame, rgb_frame, packet, sws_context, buffer);
    av_free(buffer);
    av_packet_free(&packet);
    av_frame_free(&rgb_frame);
    av_frame_free(&frame);
    sws_freeContext(sws_context);
    avcodec_free_context(&codec_context);
    destroy_format_context(&format_context);
    LC_LOGI("video_first_frame cleanup done result=%d", result);

    return result;
}

EXPORT int video_get_size(uint8_t* video_data, int data_len, VideoInfo* info) {
    LC_TRACE_LITERAL("TRACE_LITERAL video_get_size enter");
    LC_TRACE_POINT("TRACE video_get_size:enter");
    AVFormatContext* format_context = nullptr;
    AVCodecParameters* codec_parameters = nullptr;
    int result = LAGRANGECODEC_ERROR_DECODE_FAILED;
    int index = -1;

    if (info) {
        *info = {};
    }

    if (!video_data || data_len <= 0 || !info) {
        LC_LOGE("video_get_size invalid args data=%p len=%d info=%p", video_data, data_len, info);
        return LAGRANGECODEC_ERROR_INVALID_ARGUMENT;
    }

    LC_TRACE_POINT("TRACE video_get_size:before-create-format-context");
    LC_LOGI("video_get_size enter data=%p len=%d", video_data, data_len);

    int ret_code = create_format_context(video_data, data_len, &format_context);
    if (ret_code != LAGRANGECODEC_OK) {
        return ret_code;
    }

    ret_code = avformat_open_input(&format_context, nullptr, nullptr, nullptr);
    if (ret_code < 0) {
        LC_LOGE("video_get_size avformat_open_input failed ret=%d", ret_code);
        result = LAGRANGECODEC_ERROR_OPEN_INPUT_FAILED;
        goto cleanup;
    }
    LC_TRACE_POINT("TRACE video_get_size:after-open-input");

    if (avformat_find_stream_info(format_context, nullptr) < 0) {
        LC_LOGE("video_get_size avformat_find_stream_info failed");
        result = LAGRANGECODEC_ERROR_STREAM_INFO_FAILED;
        goto cleanup;
    }
    LC_LOGI("video_get_size stream_count=%u duration=%lld", format_context->nb_streams, static_cast<long long>(format_context->duration));

    for (unsigned int i = 0; i < format_context->nb_streams; i++) {
        if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            index = i;
            codec_parameters = format_context->streams[i]->codecpar;
            break;
        }
    }

    if (index == -1) {
        LC_LOGE("video_get_size no video stream found");
        result = LAGRANGECODEC_ERROR_STREAM_NOT_FOUND;
        goto cleanup;
    }

    *info = { codec_parameters->width, codec_parameters->height, (format_context->duration / AV_TIME_BASE) };
    result = LAGRANGECODEC_OK;
    LC_LOGI("video_get_size success width=%d height=%d duration=%lld", info->width, info->height, static_cast<long long>(info->duration));

cleanup:
    LC_TRACE_POINT("TRACE video_get_size:cleanup");
    LC_LOGI("video_get_size cleanup result=%d format_context=%p index=%d codec_parameters=%p", result, format_context, index, codec_parameters);
    destroy_format_context(&format_context);
    LC_LOGI("video_get_size cleanup done result=%d", result);
    return result;
}
