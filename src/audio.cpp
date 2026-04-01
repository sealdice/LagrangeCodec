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

EXPORT int audio_to_pcm(uint8_t* audio_data, int data_len, cb_codec callback, void* userdata) {
    AVFormatContext* format_context = nullptr;
    AVCodecContext* decoder_ctx = nullptr;
    const AVStream* stream = nullptr;
    const AVCodec* decoder = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    SwrContext* swr_context = nullptr;
    int result = LAGRANGECODEC_ERROR_DECODE_FAILED;
    int ret = 0;
    int stream_index = -1;
    bool produced_pcm = false;

    auto emit_converted_frame = [&](AVFrame* decoded_frame) -> int {
        const int out_samples = static_cast<int>(av_rescale_rnd(
            swr_get_delay(swr_context, decoder_ctx->sample_rate) + decoded_frame->nb_samples,
            SILKV3_SAMPLE_RATE,
            decoder_ctx->sample_rate,
            AV_ROUND_UP
        ));

        uint8_t* out_buffer = nullptr;
        int out_linesize = 0;
        int conversion_result = av_samples_alloc(
            &out_buffer,
            &out_linesize,
            1,
            out_samples,
            AV_SAMPLE_FMT_S16,
            0
        );
        if (conversion_result < 0 || !out_buffer) {
            av_freep(&out_buffer);
            return LAGRANGECODEC_ERROR_ALLOCATION_FAILED;
        }

        conversion_result = swr_convert(
            swr_context,
            &out_buffer,
            out_samples,
            const_cast<const uint8_t**>(decoded_frame->extended_data),
            decoded_frame->nb_samples
        );
        if (conversion_result < 0) {
            av_freep(&out_buffer);
            return LAGRANGECODEC_ERROR_CONVERSION_FAILED;
        }

        const int output_size = av_samples_get_buffer_size(
            &out_linesize,
            1,
            conversion_result,
            AV_SAMPLE_FMT_S16,
            1
        );
        if (output_size <= 0) {
            av_freep(&out_buffer);
            return LAGRANGECODEC_ERROR_OUTPUT_FAILED;
        }

        callback(userdata, out_buffer, output_size);
        produced_pcm = true;
        av_freep(&out_buffer);
        return LAGRANGECODEC_OK;
    };

    if (!audio_data || data_len <= 0 || !callback) {
        return LAGRANGECODEC_ERROR_INVALID_ARGUMENT;
    }

    if (create_format_context(audio_data, data_len, &format_context) != LAGRANGECODEC_OK) {
        LC_LOGE("ERROR: failed to create format context\n");
        return LAGRANGECODEC_ERROR_ALLOCATION_FAILED;
    }

    ret = avformat_open_input(&format_context, nullptr, nullptr, nullptr);
    if (ret < 0) {
        LC_LOGE("ERROR: failed to open input\n");
        result = LAGRANGECODEC_ERROR_OPEN_INPUT_FAILED;
        goto cleanup;
    }

    ret = avformat_find_stream_info(format_context, nullptr);
    if (ret < 0) {
        LC_LOGE("ERROR: failed to stream info\n");
        result = LAGRANGECODEC_ERROR_STREAM_INFO_FAILED;
        goto cleanup;
    }

    LC_LOGD("DEBUG: number of streams found: %d\n", format_context->nb_streams);
    stream_index = av_find_best_stream(format_context, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (stream_index < 0) {
        LC_LOGE("ERROR: no audio stream found\n");
        result = LAGRANGECODEC_ERROR_STREAM_NOT_FOUND;
        goto cleanup;
    }

    stream = format_context->streams[stream_index];
    if (!stream || !stream->codecpar) {
        result = LAGRANGECODEC_ERROR_STREAM_NOT_FOUND;
        goto cleanup;
    }

    decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        LC_LOGE("ERROR: no decoder found\n");
        result = LAGRANGECODEC_ERROR_CODEC_NOT_FOUND;
        goto cleanup;
    }

    decoder_ctx = avcodec_alloc_context3(decoder);
    if (!decoder_ctx) {
        result = LAGRANGECODEC_ERROR_ALLOCATION_FAILED;
        goto cleanup;
    }

    ret = avcodec_parameters_to_context(decoder_ctx, stream->codecpar);
    if (ret < 0) {
        result = LAGRANGECODEC_ERROR_CODEC_OPEN_FAILED;
        goto cleanup;
    }

    ret = avcodec_open2(decoder_ctx, decoder, nullptr);
    if (ret < 0) {
        LC_LOGE("ERROR: failed to open the decoder\n");
        result = LAGRANGECODEC_ERROR_CODEC_OPEN_FAILED;
        goto cleanup;
    }
    if (decoder_ctx->sample_rate <= 0 || decoder_ctx->channels <= 0) {
        result = LAGRANGECODEC_ERROR_CODEC_OPEN_FAILED;
        goto cleanup;
    }

    if (decoder_ctx->channel_layout == 0) {
        decoder_ctx->channel_layout = av_get_default_channel_layout(decoder_ctx->channels);
    }
    LC_LOGD(
        "DEBUG: Setting up decoder - sample format: %s, sample rate: %d Hz, "
        "channels: %d \n",
        av_get_sample_fmt_name(static_cast<AVSampleFormat>(stream->codecpar->format)),
        stream->codecpar->sample_rate, stream->codecpar->channels);

    packet = av_packet_alloc();
    frame = av_frame_alloc();
    if (!packet || !frame) {
        result = LAGRANGECODEC_ERROR_ALLOCATION_FAILED;
        goto cleanup;
    }

    swr_context = swr_alloc_set_opts(
        nullptr,
        AV_CH_LAYOUT_MONO,
        AV_SAMPLE_FMT_S16,
        SILKV3_SAMPLE_RATE,
        av_get_default_channel_layout(decoder_ctx->channels),
        decoder_ctx->sample_fmt,
        decoder_ctx->sample_rate,
        0,
        nullptr
    );

    if (!swr_context) {
        result = LAGRANGECODEC_ERROR_ALLOCATION_FAILED;
        goto cleanup;
    }

    ret = swr_init(swr_context);
    if (ret < 0) {
        result = LAGRANGECODEC_ERROR_CONVERSION_FAILED;
        goto cleanup;
    }

    while ((ret = av_read_frame(format_context, packet)) == 0) {
        if (packet->stream_index != stream_index) {
            av_packet_unref(packet);
            continue;
        }

        ret = avcodec_send_packet(decoder_ctx, packet);
        av_packet_unref(packet);
        if (ret < 0) {
            result = LAGRANGECODEC_ERROR_DECODE_FAILED;
            goto cleanup;
        }

        while (true) {
            ret = avcodec_receive_frame(decoder_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                result = LAGRANGECODEC_ERROR_DECODE_FAILED;
                goto cleanup;
            }

            result = emit_converted_frame(frame);
            av_frame_unref(frame);
            if (result != LAGRANGECODEC_OK) {
                goto cleanup;
            }
        }
    }

    if (ret != AVERROR_EOF) {
        result = LAGRANGECODEC_ERROR_DECODE_FAILED;
        goto cleanup;
    }

    ret = avcodec_send_packet(decoder_ctx, nullptr);
    if (ret < 0 && ret != AVERROR_EOF) {
        result = LAGRANGECODEC_ERROR_DECODE_FAILED;
        goto cleanup;
    }

    while (true) {
        ret = avcodec_receive_frame(decoder_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            result = LAGRANGECODEC_ERROR_DECODE_FAILED;
            goto cleanup;
        }
        result = emit_converted_frame(frame);
        av_frame_unref(frame);
        if (result != LAGRANGECODEC_OK) {
            goto cleanup;
        }
    }

    result = produced_pcm ? LAGRANGECODEC_OK : LAGRANGECODEC_ERROR_DECODE_FAILED;

cleanup:
    swr_free(&swr_context);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&decoder_ctx);
    destroy_format_context(&format_context);

    return result;
}
