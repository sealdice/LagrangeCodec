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
};

constexpr int kAvioBufferSize = 4096;

inline int memory_read_packet(void* opaque, uint8_t* buf, int buf_size) {
    auto* input = static_cast<MemoryInputContext*>(opaque);
    if (!input || !buf || buf_size <= 0) {
        return AVERROR(EINVAL);
    }

    if (input->position >= input->size) {
        return AVERROR_EOF;
    }

    const int64_t remaining = input->size - input->position;
    const int bytes_to_copy = static_cast<int>(std::min<int64_t>(remaining, buf_size));
    std::memcpy(buf, input->data + input->position, static_cast<size_t>(bytes_to_copy));
    input->position += bytes_to_copy;
    return bytes_to_copy;
}

inline int64_t memory_seek(void* opaque, int64_t offset, int whence) {
    auto* input = static_cast<MemoryInputContext*>(opaque);
    if (!input) {
        return AVERROR(EINVAL);
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
        return AVERROR(EINVAL);
    }

    input->position = next_position;
    return input->position;
}

inline void destroy_format_context(AVFormatContext** format_context) {
    if (!format_context || !*format_context) {
        return;
    }

    AVFormatContext* context = *format_context;
    AVIOContext* avio_ctx = context->pb;

    avformat_close_input(format_context);
    if (*format_context) {
        avformat_free_context(*format_context);
        *format_context = nullptr;
    }

    if (avio_ctx) {
        auto* input = static_cast<MemoryInputContext*>(avio_ctx->opaque);
        if (input) {
            av_freep(&input->data);
            av_freep(&input);
        }
        av_freep(&avio_ctx->buffer);
        avio_context_free(&avio_ctx);
    }
}

inline int create_format_context(const uint8_t* data, int data_len, AVFormatContext** format_context) {
    if (!format_context || !data || data_len <= 0) {
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

    AVFormatContext* context = avformat_alloc_context();
    if (!context) {
        av_freep(&avio_ctx->buffer);
        avio_context_free(&avio_ctx);
        av_freep(&input->data);
        av_freep(&input);
        LC_LOGE("ERROR: failed to allocate AVFormatContext\n");
        return LAGRANGECODEC_ERROR_ALLOCATION_FAILED;
    }

    context->pb = avio_ctx;
    context->flags |= AVFMT_FLAG_CUSTOM_IO;
    *format_context = context;
    return LAGRANGECODEC_OK;
}

#endif //LAGRANGECODEC_UTIL_H
