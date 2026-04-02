//
// Created by Wenxuan Lin on 2025-02-23.
//

#include "audio.h"
#include "util.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>

void lc_probe_audio_to_pcm_entry(void);
}

EXPORT int audio_to_pcm(uint8_t* audio_data, int data_len, cb_codec callback, void* userdata) {
    lc_probe_audio_to_pcm_entry();
    LC_TRACE_LITERAL("TRACE_LITERAL audio_to_pcm enter");
    LC_TRACE_POINT("TRACE audio_to_pcm:enter");
    AVFormatContext* format_context = nullptr;
    AVCodecContext* decoder_ctx = nullptr;
    const AVStream* stream = nullptr;
    const AVCodec* decoder = nullptr;
    AVCodecParserContext* parser = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    SwrContext* swr_context = nullptr;
    int result = LAGRANGECODEC_ERROR_DECODE_FAILED;
    int ret = 0;
    int stream_index = -1;
    bool produced_pcm = false;
    bool used_android_parser = false;

    auto ensure_swr_context = [&](AVFrame* decoded_frame) -> int {
        if (swr_context) {
            return LAGRANGECODEC_OK;
        }

        if (!decoded_frame) {
            LC_LOGE("audio_to_pcm ensure_swr_context missing decoded_frame");
            return LAGRANGECODEC_ERROR_INVALID_ARGUMENT;
        }

        const int input_sample_rate = decoded_frame->sample_rate > 0 ? decoded_frame->sample_rate : decoder_ctx->sample_rate;
        const int input_channels = decoded_frame->channels > 0 ? decoded_frame->channels : decoder_ctx->channels;
        const int64_t input_channel_layout =
            decoded_frame->channel_layout != 0 ? decoded_frame->channel_layout :
            (decoder_ctx->channel_layout != 0 ? decoder_ctx->channel_layout : av_get_default_channel_layout(input_channels));
        const auto input_sample_fmt = static_cast<AVSampleFormat>(decoded_frame->format);

        {
            char probe_buffer[192];
            int probe_len = std::snprintf(
                probe_buffer,
                sizeof(probe_buffer),
                "PROBE audio_to_pcm first frame sample_rate=%d channels=%d channel_layout=%lld format=%d\n",
                input_sample_rate,
                input_channels,
                static_cast<long long>(input_channel_layout),
                static_cast<int>(input_sample_fmt)
            );
            if (probe_len > 0) {
                lc_trace_buffer(probe_buffer, static_cast<size_t>(probe_len));
            }
        }

        LC_LOGI(
            "audio_to_pcm ensure_swr_context frame sample_rate=%d channels=%d channel_layout=%lld sample_fmt=%d decoder_sample_rate=%d decoder_channels=%d decoder_layout=%lld",
            input_sample_rate,
            input_channels,
            static_cast<long long>(input_channel_layout),
            static_cast<int>(input_sample_fmt),
            decoder_ctx ? decoder_ctx->sample_rate : -1,
            decoder_ctx ? decoder_ctx->channels : -1,
            static_cast<long long>(decoder_ctx ? decoder_ctx->channel_layout : 0)
        );

        if (input_sample_rate <= 0 || input_channels <= 0 || input_channel_layout == 0 || input_sample_fmt == AV_SAMPLE_FMT_NONE) {
            LC_LOGE(
                "audio_to_pcm ensure_swr_context invalid input format sample_rate=%d channels=%d channel_layout=%lld sample_fmt=%d",
                input_sample_rate,
                input_channels,
                static_cast<long long>(input_channel_layout),
                static_cast<int>(input_sample_fmt)
            );
            return LAGRANGECODEC_ERROR_CODEC_OPEN_FAILED;
        }

        swr_context = swr_alloc_set_opts(
            nullptr,
            AV_CH_LAYOUT_MONO,
            AV_SAMPLE_FMT_S16,
            SILKV3_SAMPLE_RATE,
            input_channel_layout,
            input_sample_fmt,
            input_sample_rate,
            0,
            nullptr
        );

        if (!swr_context) {
            LC_LOGE("audio_to_pcm ensure_swr_context swr_alloc_set_opts returned null");
            return LAGRANGECODEC_ERROR_ALLOCATION_FAILED;
        }

        LC_TRACE_POINT("PROBE audio_to_pcm before swr_init");
        const int init_ret = swr_init(swr_context);
        {
            char probe_buffer[128];
            int probe_len = std::snprintf(probe_buffer, sizeof(probe_buffer), "PROBE audio_to_pcm after swr_init ret=%d\n", init_ret);
            if (probe_len > 0) {
                lc_trace_buffer(probe_buffer, static_cast<size_t>(probe_len));
            }
        }
        if (init_ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE] = {};
            av_strerror(init_ret, errbuf, sizeof(errbuf));
            LC_LOGE("audio_to_pcm ensure_swr_context swr_init failed ret=%d err=%s", init_ret, errbuf);
            return LAGRANGECODEC_ERROR_CONVERSION_FAILED;
        }

        LC_LOGI("audio_to_pcm ensure_swr_context swr_init ok");
        return LAGRANGECODEC_OK;
    };

    auto emit_converted_frame = [&](AVFrame* decoded_frame) -> int {
        int ensure_ret = ensure_swr_context(decoded_frame);
        if (ensure_ret != LAGRANGECODEC_OK) {
            return ensure_ret;
        }

        const int out_samples = static_cast<int>(av_rescale_rnd(
            swr_get_delay(swr_context, decoded_frame->sample_rate > 0 ? decoded_frame->sample_rate : decoder_ctx->sample_rate) + decoded_frame->nb_samples,
            SILKV3_SAMPLE_RATE,
            decoded_frame->sample_rate > 0 ? decoded_frame->sample_rate : decoder_ctx->sample_rate,
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
        LC_LOGE("audio_to_pcm invalid args data=%p len=%d callback=%p userdata=%p", audio_data, data_len, callback, userdata);
        return LAGRANGECODEC_ERROR_INVALID_ARGUMENT;
    }

    LC_TRACE_POINT("TRACE audio_to_pcm:before-create-format-context");
    LC_LOGI("audio_to_pcm enter data=%p len=%d callback=%p userdata=%p", audio_data, data_len, callback, userdata);

    if (create_format_context(audio_data, data_len, &format_context) != LAGRANGECODEC_OK) {
        LC_LOGE("ERROR: failed to create format context\n");
        return LAGRANGECODEC_ERROR_ALLOCATION_FAILED;
    }

    LC_TRACE_POINT("TRACE audio_to_pcm:before-open-input");

    ret = avformat_open_input(&format_context, nullptr, nullptr, nullptr);
    if (ret < 0) {
        LC_LOGE("ERROR: failed to open input ret=%d", ret);
        result = LAGRANGECODEC_ERROR_OPEN_INPUT_FAILED;
        goto cleanup;
    }
    LC_LOGI("audio_to_pcm avformat_open_input ok context=%p streams=%u", format_context, format_context ? format_context->nb_streams : 0);
    LC_TRACE_POINT("TRACE audio_to_pcm:after-open-input");

#if defined(__ANDROID__)
    LC_TRACE_LITERAL("TRACE_LITERAL audio_to_pcm skip-find-stream-info-android");
    LC_TRACE_POINT("PROBE audio_to_pcm skipping avformat_find_stream_info on Android");
#else
    LC_TRACE_LITERAL("TRACE_LITERAL audio_to_pcm before-find-stream-info");
    LC_TRACE_POINT("PROBE audio_to_pcm before avformat_find_stream_info");

    ret = avformat_find_stream_info(format_context, nullptr);
    {
        char probe_buffer[128];
        int probe_len = std::snprintf(probe_buffer, sizeof(probe_buffer), "PROBE audio_to_pcm after avformat_find_stream_info ret=%d\n", ret);
        if (probe_len > 0) {
            lc_trace_buffer(probe_buffer, static_cast<size_t>(probe_len));
        }
    }
    if (ret < 0) {
        LC_LOGE("ERROR: failed to stream info ret=%d", ret);
        result = LAGRANGECODEC_ERROR_STREAM_INFO_FAILED;
        goto cleanup;
    }
#endif

#if defined(__ANDROID__)
    LC_TRACE_LITERAL("TRACE_LITERAL audio_to_pcm before-manual-stream-scan");
    LC_TRACE_POINT("PROBE audio_to_pcm before manual audio stream scan");

    stream_index = find_first_stream_index(format_context, AVMEDIA_TYPE_AUDIO, &stream, nullptr);
    {
        char probe_buffer[128];
        int probe_len = std::snprintf(probe_buffer, sizeof(probe_buffer), "PROBE audio_to_pcm after manual audio stream scan ret=%d\n", stream_index);
        if (probe_len > 0) {
            lc_trace_buffer(probe_buffer, static_cast<size_t>(probe_len));
        }
    }
#else
    LC_TRACE_LITERAL("TRACE_LITERAL audio_to_pcm before-find-best-stream");
    LC_TRACE_POINT("PROBE audio_to_pcm before av_find_best_stream");

    LC_LOGD("DEBUG: number of streams found: %d\n", format_context->nb_streams);
    stream_index = av_find_best_stream(format_context, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    {
        char probe_buffer[128];
        int probe_len = std::snprintf(probe_buffer, sizeof(probe_buffer), "PROBE audio_to_pcm after av_find_best_stream ret=%d\n", stream_index);
        if (probe_len > 0) {
            lc_trace_buffer(probe_buffer, static_cast<size_t>(probe_len));
        }
    }
#endif

    if (stream_index < 0) {
        LC_LOGE("ERROR: no audio stream found ret=%d", stream_index);
        result = LAGRANGECODEC_ERROR_STREAM_NOT_FOUND;
        goto cleanup;
    }
    LC_LOGI("audio_to_pcm selected stream_index=%d", stream_index);

#if !defined(__ANDROID__)
    stream = format_context->streams[stream_index];
#endif
    if (!stream || !stream->codecpar) {
        LC_LOGE("audio_to_pcm invalid stream/codecpar stream=%p codecpar=%p", stream, stream ? stream->codecpar : nullptr);
        result = LAGRANGECODEC_ERROR_STREAM_NOT_FOUND;
        goto cleanup;
    }

    decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        LC_LOGE("ERROR: no decoder found codec_id=%d", stream->codecpar->codec_id);
        result = LAGRANGECODEC_ERROR_CODEC_NOT_FOUND;
        goto cleanup;
    }
    LC_LOGI("audio_to_pcm decoder=%s codec_id=%d", decoder->name ? decoder->name : "(null)", stream->codecpar->codec_id);

    decoder_ctx = avcodec_alloc_context3(decoder);
    if (!decoder_ctx) {
        result = LAGRANGECODEC_ERROR_ALLOCATION_FAILED;
        goto cleanup;
    }

    ret = avcodec_parameters_to_context(decoder_ctx, stream->codecpar);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(ret, errbuf, sizeof(errbuf));
        LC_LOGE("audio_to_pcm avcodec_parameters_to_context failed ret=%d err=%s", ret, errbuf);
        result = LAGRANGECODEC_ERROR_CODEC_OPEN_FAILED;
        goto cleanup;
    }

#if defined(__ANDROID__)
    decoder_ctx->thread_count = 1;
    decoder_ctx->thread_type = 0;
    LC_LOGI("audio_to_pcm forcing single-thread decoder on Android");
#endif

    decoder_ctx->pkt_timebase = stream->time_base;
    LC_LOGI(
        "audio_to_pcm decoder_ctx before open sample_rate=%d channels=%d channel_layout=%lld sample_fmt=%d pkt_timebase=%d/%d",
        decoder_ctx->sample_rate,
        decoder_ctx->channels,
        static_cast<long long>(decoder_ctx->channel_layout),
        decoder_ctx->sample_fmt,
        decoder_ctx->pkt_timebase.num,
        decoder_ctx->pkt_timebase.den
    );

    ret = avcodec_open2(decoder_ctx, decoder, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(ret, errbuf, sizeof(errbuf));
        LC_LOGE("ERROR: failed to open the decoder ret=%d err=%s", ret, errbuf);
        result = LAGRANGECODEC_ERROR_CODEC_OPEN_FAILED;
        goto cleanup;
    }
    LC_LOGI(
        "audio_to_pcm decoder_ctx after open sample_rate=%d channels=%d channel_layout=%lld sample_fmt=%d frame_size=%d",
        decoder_ctx->sample_rate,
        decoder_ctx->channels,
        static_cast<long long>(decoder_ctx->channel_layout),
        decoder_ctx->sample_fmt,
        decoder_ctx->frame_size
    );

    LC_TRACE_POINT("PROBE audio_to_pcm before packet_alloc");
    packet = av_packet_alloc();
    LC_TRACE_POINT("PROBE audio_to_pcm after packet_alloc");
    frame = av_frame_alloc();
    LC_TRACE_POINT("PROBE audio_to_pcm after frame_alloc");
    if (!packet || !frame) {
        result = LAGRANGECODEC_ERROR_ALLOCATION_FAILED;
        goto cleanup;
    }

#if defined(__ANDROID__)
    parser = av_parser_init(stream->codecpar->codec_id);
    {
        char probe_buffer[128];
        int probe_len = std::snprintf(probe_buffer, sizeof(probe_buffer), "PROBE audio_to_pcm after parser_init parser=%p\n", parser);
        if (probe_len > 0) {
            lc_trace_buffer(probe_buffer, static_cast<size_t>(probe_len));
        }
    }
    if (!parser) {
        LC_LOGE("audio_to_pcm av_parser_init failed codec_id=%d", stream->codecpar->codec_id);
        result = LAGRANGECODEC_ERROR_CODEC_OPEN_FAILED;
        goto cleanup;
    }
    used_android_parser = true;

    const uint8_t* parse_data = audio_data;
    int parse_bytes_remaining = data_len;
    while (parse_bytes_remaining > 0) {
        packet->data = nullptr;
        packet->size = 0;

        LC_TRACE_POINT("PROBE audio_to_pcm before parser_parse2");
        ret = av_parser_parse2(
            parser,
            decoder_ctx,
            &packet->data,
            &packet->size,
            parse_data,
            parse_bytes_remaining,
            AV_NOPTS_VALUE,
            AV_NOPTS_VALUE,
            0
        );
        {
            char probe_buffer[128];
            int probe_len = std::snprintf(probe_buffer, sizeof(probe_buffer), "PROBE audio_to_pcm after parser_parse2 ret=%d packet_size=%d remaining=%d\n", ret, packet->size, parse_bytes_remaining);
            if (probe_len > 0) {
                lc_trace_buffer(probe_buffer, static_cast<size_t>(probe_len));
            }
        }

        if (ret < 0) {
            LC_LOGE("audio_to_pcm av_parser_parse2 failed ret=%d", ret);
            result = LAGRANGECODEC_ERROR_DECODE_FAILED;
            goto cleanup;
        }

        if (ret == 0 && packet->size <= 0) {
            LC_LOGE("audio_to_pcm parser made no progress remaining=%d", parse_bytes_remaining);
            result = LAGRANGECODEC_ERROR_DECODE_FAILED;
            goto cleanup;
        }

        parse_data += ret;
        parse_bytes_remaining -= ret;
        if (packet->size <= 0) {
            continue;
        }

        LC_TRACE_POINT("PROBE audio_to_pcm before send_packet");
        ret = avcodec_send_packet(decoder_ctx, packet);
        {
            char probe_buffer[128];
            int probe_len = std::snprintf(probe_buffer, sizeof(probe_buffer), "PROBE audio_to_pcm after send_packet ret=%d\n", ret);
            if (probe_len > 0) {
                lc_trace_buffer(probe_buffer, static_cast<size_t>(probe_len));
            }
        }
        av_packet_unref(packet);
        if (ret < 0) {
            LC_LOGE("audio_to_pcm avcodec_send_packet failed ret=%d", ret);
            result = LAGRANGECODEC_ERROR_DECODE_FAILED;
            goto cleanup;
        }

        while (true) {
            ret = avcodec_receive_frame(decoder_ctx, frame);
            {
                char probe_buffer[128];
                int probe_len = std::snprintf(probe_buffer, sizeof(probe_buffer), "PROBE audio_to_pcm after receive_frame ret=%d\n", ret);
                if (probe_len > 0) {
                    lc_trace_buffer(probe_buffer, static_cast<size_t>(probe_len));
                }
            }
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                LC_LOGI("audio_to_pcm receive_frame pause ret=%d", ret);
                break;
            }
            if (ret < 0) {
                LC_LOGE("audio_to_pcm avcodec_receive_frame failed ret=%d", ret);
                result = LAGRANGECODEC_ERROR_DECODE_FAILED;
                goto cleanup;
            }

            LC_LOGI("audio_to_pcm decoded frame nb_samples=%d format=%d channels=%d extended_data=%p", frame->nb_samples, frame->format, frame->channels, frame->extended_data);

            result = emit_converted_frame(frame);
            LC_LOGI("audio_to_pcm emit_converted_frame result=%d", result);
            av_frame_unref(frame);
            if (result != LAGRANGECODEC_OK) {
                goto cleanup;
            }
        }

        av_packet_unref(packet);
    }

#else

    while (true) {
        LC_TRACE_POINT("PROBE audio_to_pcm before av_read_frame");
        ret = av_read_frame(format_context, packet);
        {
            char probe_buffer[128];
            int probe_len = std::snprintf(probe_buffer, sizeof(probe_buffer), "PROBE audio_to_pcm after av_read_frame ret=%d\n", ret);
            if (probe_len > 0) {
                lc_trace_buffer(probe_buffer, static_cast<size_t>(probe_len));
            }
        }
        if (ret != 0) {
            break;
        }

        LC_LOGI("audio_to_pcm av_read_frame stream=%d size=%d", packet->stream_index, packet->size);
        if (packet->stream_index != stream_index) {
            av_packet_unref(packet);
            continue;
        }

        LC_TRACE_POINT("PROBE audio_to_pcm before send_packet");
        ret = avcodec_send_packet(decoder_ctx, packet);
        {
            char probe_buffer[128];
            int probe_len = std::snprintf(probe_buffer, sizeof(probe_buffer), "PROBE audio_to_pcm after send_packet ret=%d\n", ret);
            if (probe_len > 0) {
                lc_trace_buffer(probe_buffer, static_cast<size_t>(probe_len));
            }
        }
        av_packet_unref(packet);
        if (ret < 0) {
            LC_LOGE("audio_to_pcm avcodec_send_packet failed ret=%d", ret);
            result = LAGRANGECODEC_ERROR_DECODE_FAILED;
            goto cleanup;
        }

        while (true) {
            ret = avcodec_receive_frame(decoder_ctx, frame);
            {
                char probe_buffer[128];
                int probe_len = std::snprintf(probe_buffer, sizeof(probe_buffer), "PROBE audio_to_pcm after receive_frame ret=%d\n", ret);
                if (probe_len > 0) {
                    lc_trace_buffer(probe_buffer, static_cast<size_t>(probe_len));
                }
            }
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                LC_LOGI("audio_to_pcm receive_frame pause ret=%d", ret);
                break;
            }
            if (ret < 0) {
                LC_LOGE("audio_to_pcm avcodec_receive_frame failed ret=%d", ret);
                result = LAGRANGECODEC_ERROR_DECODE_FAILED;
                goto cleanup;
            }

            LC_LOGI("audio_to_pcm decoded frame nb_samples=%d format=%d channels=%d extended_data=%p", frame->nb_samples, frame->format, frame->channels, frame->extended_data);

            result = emit_converted_frame(frame);
            LC_LOGI("audio_to_pcm emit_converted_frame result=%d", result);
            av_frame_unref(frame);
            if (result != LAGRANGECODEC_OK) {
                goto cleanup;
            }
        }
    }

#endif

    if (used_android_parser) {
        ret = AVERROR_EOF;
    }

    if (ret != AVERROR_EOF) {
        LC_LOGE("audio_to_pcm av_read_frame finished ret=%d", ret);
        result = LAGRANGECODEC_ERROR_DECODE_FAILED;
        goto cleanup;
    }

    LC_LOGI("audio_to_pcm draining decoder");

    ret = avcodec_send_packet(decoder_ctx, nullptr);
    if (ret < 0 && ret != AVERROR_EOF) {
        LC_LOGE("audio_to_pcm drain send null packet failed ret=%d", ret);
        result = LAGRANGECODEC_ERROR_DECODE_FAILED;
        goto cleanup;
    }

    while (true) {
        ret = avcodec_receive_frame(decoder_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            LC_LOGI("audio_to_pcm drain complete ret=%d", ret);
            break;
        }
        if (ret < 0) {
            LC_LOGE("audio_to_pcm drain receive_frame failed ret=%d", ret);
            result = LAGRANGECODEC_ERROR_DECODE_FAILED;
            goto cleanup;
        }
        LC_LOGI("audio_to_pcm draining decoded frame nb_samples=%d", frame->nb_samples);
        result = emit_converted_frame(frame);
        LC_LOGI("audio_to_pcm drain emit result=%d", result);
        av_frame_unref(frame);
        if (result != LAGRANGECODEC_OK) {
            goto cleanup;
        }
    }

    result = produced_pcm ? LAGRANGECODEC_OK : LAGRANGECODEC_ERROR_DECODE_FAILED;
    LC_LOGI("audio_to_pcm exit result=%d produced_pcm=%d", result, produced_pcm ? 1 : 0);

cleanup:
    LC_TRACE_POINT("TRACE audio_to_pcm:cleanup");
    LC_LOGI("audio_to_pcm cleanup result=%d format_context=%p decoder_ctx=%p packet=%p frame=%p swr=%p", result, format_context, decoder_ctx, packet, frame, swr_context);
    av_parser_close(parser);
    swr_free(&swr_context);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&decoder_ctx);
    destroy_format_context(&format_context);
    LC_LOGI("audio_to_pcm cleanup done result=%d", result);

    return result;
}
