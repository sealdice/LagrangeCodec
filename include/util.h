//
// Created by pk5ls on 2026/2/8.
//

#ifndef LAGRANGECODEC_UTIL_H
#define LAGRANGECODEC_UTIL_H

#include "logging.h"

#include <cstring>

extern "C" {
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
}

// REMEMBER TO FREE THE BUFFER AFTER USE BY av_free(format_context->pb->buffer);
inline int create_format_context(uint8_t* data, int data_len, AVFormatContext** format_context) {
    AVIOContext* avio_ctx = nullptr; // Create a custom I/O context with the raw audio data buffer
    uint8_t* avio_buffer = nullptr;

    avio_buffer = static_cast<uint8_t*>(av_malloc(data_len)); // Allocate buffer for AVIOContext
    if (!avio_buffer) {
        LC_LOGE("ERROR: failed to allocate memory for AVIOContext\n");
        return -1;
    }
    memcpy(avio_buffer, data, data_len);

    // Allocate the AVIOContext with the custom buffer
    avio_ctx = avio_alloc_context(avio_buffer, data_len, 0, nullptr, nullptr, nullptr, nullptr);
    if (!avio_ctx) {
        LC_LOGE("ERROR: failed to create AVIOContext\n");
        return -1;
    }

    // Create format context and set the I/O context
    *format_context = avformat_alloc_context();
    (*format_context)->pb = avio_ctx;

    return 0;
}

#endif //LAGRANGECODEC_UTIL_H
