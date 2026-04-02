//
// Created by Wenxuan Lin on 2025-02-23.
//

#include "audio.h"
#include "util.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

int audio_to_pcm(uint8_t* audio_data, int data_len, cb_codec callback, void* userdata) {
    AVFormatContext* format_context = nullptr;
    int ret;
    if (create_format_context(audio_data, data_len, &format_context) < 0) {
        return -1;
    }

    if (avformat_open_input(&format_context, nullptr, nullptr, nullptr) < 0) {
        av_free(format_context->pb->buffer);
        return -1;
    }

    ret = avformat_find_stream_info(format_context, nullptr);
    if (ret < 0) {
        return -1;
    }

    const int stream_index = av_find_best_stream(format_context, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (stream_index < 0) {
        return -1;
    }

    const AVStream *stream = format_context->streams[stream_index];

    const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        return -1;
    }

    AVCodecContext *decoder_ctx = avcodec_alloc_context3(decoder);

    avcodec_parameters_to_context(decoder_ctx, stream->codecpar);

    ret = avcodec_open2(decoder_ctx, decoder, nullptr);
    if (ret < 0) {
        return -1;
    }
    if (decoder_ctx->channel_layout == 0) {
        decoder_ctx->channel_layout = av_get_default_channel_layout(decoder_ctx->channels);
    }

    // Step 2: Decode audio
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    SwrContext *swr_context = swr_alloc_set_opts(
        nullptr,
        AV_CH_LAYOUT_MONO,
        AV_SAMPLE_FMT_S16,
        24000,
        av_get_default_channel_layout(decoder_ctx->channels),
        decoder_ctx->sample_fmt,
        decoder_ctx->sample_rate,
        0,
        nullptr
    );

    ret = swr_init(swr_context);

    while (av_read_frame(format_context, packet) == 0) {
        if (packet->stream_index != stream_index) {
            av_packet_unref(packet);
            continue;
        }
        ret = avcodec_send_packet(decoder_ctx, packet);
        while ((ret = avcodec_receive_frame(decoder_ctx, frame)) == 0) {
            AVFrame *out = av_frame_alloc();
            out->sample_rate = 24000;
            out->channel_layout = AV_CH_LAYOUT_MONO;
            out->channels = 1;
            out->format = AV_SAMPLE_FMT_S16;
            ret = swr_convert_frame(swr_context, out, frame);
            callback(userdata, out->data[0], out->nb_samples * out->channels * 2);
            av_frame_unref(frame);
            av_frame_free(&out);
        }
        av_packet_unref(packet);
    }

    swr_free(&swr_context);
    avcodec_close(decoder_ctx);
    avformat_close_input(&format_context);

    return 0;
}
