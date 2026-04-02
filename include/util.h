//
// Created by pk5ls on 2026/2/8.
//

#ifndef LAGRANGECODEC_UTIL_H
#define LAGRANGECODEC_UTIL_H

#include "common.h"
#include "logging.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

struct MemoryInputContext {
    uint8_t* data;
    int64_t size;
    int64_t position;
    int read_calls;
    int seek_calls;
};

constexpr int kAvioBufferSize = 4096;

inline int memory_read_packet(void* opaque, uint8_t* buf, int buf_size) {
    auto* input = static_cast<MemoryInputContext*>(opaque);
    if (!input || !buf || buf_size <= 0) {
        LC_LOGE("memory_read_packet invalid args opaque=%p buf=%p size=%d", opaque, buf, buf_size);
        return AVERROR(EINVAL);
    }

    input->read_calls += 1;
    if (input->read_calls <= 8) {
        LC_LOGI("memory_read_packet call=%d pos=%lld size=%lld request=%d", input->read_calls, static_cast<long long>(input->position), static_cast<long long>(input->size), buf_size);
    }

    if (input->position >= input->size) {
        LC_LOGI("memory_read_packet eof pos=%lld size=%lld", static_cast<long long>(input->position), static_cast<long long>(input->size));
        return AVERROR_EOF;
    }

    const int64_t remaining = input->size - input->position;
    const int bytes_to_copy = static_cast<int>(std::min<int64_t>(remaining, buf_size));
    std::memcpy(buf, input->data + input->position, static_cast<size_t>(bytes_to_copy));
    input->position += bytes_to_copy;
    if (input->read_calls <= 8) {
        LC_LOGI("memory_read_packet copied=%d new_pos=%lld", bytes_to_copy, static_cast<long long>(input->position));
    }
    return bytes_to_copy;
}

inline int64_t memory_seek(void* opaque, int64_t offset, int whence) {
    auto* input = static_cast<MemoryInputContext*>(opaque);
    if (!input) {
        LC_LOGE("memory_seek invalid opaque");
        return AVERROR(EINVAL);
    }

    input->seek_calls += 1;
    if (input->seek_calls <= 8) {
        LC_LOGI("memory_seek call=%d pos=%lld offset=%lld whence=%d", input->seek_calls, static_cast<long long>(input->position), static_cast<long long>(offset), whence);
    }

    if (whence == AVSEEK_SIZE) {
        return input->size;
    }

    int64_t next_position = 0;
    switch (whence) {
        case SEEK_SET:
            next_position = offset;
            break;
        case SEEK_CUR:
            next_position = input->position + offset;
            break;
        case SEEK_END:
            next_position = input->size + offset;
            break;
        default:
            return AVERROR(EINVAL);
    }

    if (next_position < 0 || next_position > input->size) {
        LC_LOGE("memory_seek rejected next_pos=%lld size=%lld", static_cast<long long>(next_position), static_cast<long long>(input->size));
        return AVERROR(EINVAL);
    }

    input->position = next_position;
    if (input->seek_calls <= 8) {
        LC_LOGI("memory_seek new_pos=%lld", static_cast<long long>(input->position));
    }
    return input->position;
}

inline void destroy_format_context(AVFormatContext** format_context) {
    if (!format_context || !*format_context) {
        LC_LOGI("destroy_format_context skipped null context");
        return;
    }

    AVFormatContext* context = *format_context;
    AVIOContext* avio_ctx = context->pb;
    LC_LOGI("destroy_format_context begin context=%p avio=%p", context, avio_ctx);

    avformat_close_input(format_context);
    if (*format_context) {
        LC_LOGI("destroy_format_context avformat_close_input kept context, freeing manually context=%p", *format_context);
        avformat_free_context(*format_context);
        *format_context = nullptr;
    }

    if (avio_ctx) {
        auto* input = static_cast<MemoryInputContext*>(avio_ctx->opaque);
        LC_LOGI("destroy_format_context freeing avio=%p opaque=%p", avio_ctx, input);
        if (input) {
            av_freep(&input->data);
            av_freep(&input);
        }
        avio_context_free(&avio_ctx);
    }

    LC_LOGI("destroy_format_context end");
}

inline int create_format_context(const uint8_t* data, int data_len, AVFormatContext** format_context) {
    LC_TRACE_POINT("TRACE create_format_context:enter");
    if (!format_context || !data || data_len <= 0) {
        LC_LOGE("create_format_context invalid args format_context=%p data=%p len=%d", format_context, data, data_len);
        return LAGRANGECODEC_ERROR_INVALID_ARGUMENT;
    }

    *format_context = nullptr;

    auto* input = static_cast<MemoryInputContext*>(av_mallocz(sizeof(MemoryInputContext)));
    if (!input) {
        LC_LOGE("ERROR: failed to allocate memory input context\n");
        return LAGRANGECODEC_ERROR_ALLOCATION_FAILED;
    }

    input->data = static_cast<uint8_t*>(av_malloc(static_cast<size_t>(data_len)));
    if (!input->data) {
        av_freep(&input);
        LC_LOGE("ERROR: failed to allocate memory for media copy\n");
        return LAGRANGECODEC_ERROR_ALLOCATION_FAILED;
    }
    std::memcpy(input->data, data, static_cast<size_t>(data_len));
    input->size = data_len;
    input->position = 0;
    input->read_calls = 0;
    input->seek_calls = 0;
    LC_TRACE_POINT("TRACE create_format_context:copied-input");
    LC_LOGI("create_format_context copied input size=%d input=%p", data_len, input);

    uint8_t* avio_buffer = static_cast<uint8_t*>(av_malloc(kAvioBufferSize));
    if (!avio_buffer) {
        av_freep(&input->data);
        av_freep(&input);
        LC_LOGE("ERROR: failed to allocate AVIO buffer\n");
        return LAGRANGECODEC_ERROR_ALLOCATION_FAILED;
    }

    AVIOContext* avio_ctx = avio_alloc_context(
        avio_buffer,
        kAvioBufferSize,
        0,
        input,
        memory_read_packet,
        nullptr,
        memory_seek
    );
    if (!avio_ctx) {
        av_freep(&avio_buffer);
        av_freep(&input->data);
        av_freep(&input);
        LC_LOGE("ERROR: failed to create AVIOContext\n");
        return LAGRANGECODEC_ERROR_ALLOCATION_FAILED;
    }
    LC_TRACE_POINT("TRACE create_format_context:avio-created");

    AVFormatContext* context = avformat_alloc_context();
    if (!context) {
        avio_context_free(&avio_ctx);
        av_freep(&input->data);
        av_freep(&input);
        LC_LOGE("ERROR: failed to allocate AVFormatContext\n");
        return LAGRANGECODEC_ERROR_ALLOCATION_FAILED;
    }

    context->pb = avio_ctx;
    context->flags |= AVFMT_FLAG_CUSTOM_IO;
    avio_ctx->seekable = AVIO_SEEKABLE_NORMAL;
    LC_TRACE_POINT("TRACE create_format_context:success");
    LC_LOGI("create_format_context success context=%p avio=%p opaque=%p", context, avio_ctx, input);
    *format_context = context;
    return LAGRANGECODEC_OK;
}

#endif //LAGRANGECODEC_UTIL_H
