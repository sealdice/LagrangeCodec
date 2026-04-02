//
// Created by Wenxuan Lin on 2025-02-23.
//

#ifndef COMMON_H
#define COMMON_H

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*cb_codec)(void* userdata, const uint8_t* p, int len);

#ifdef __cplusplus
}
#endif

#define LAGRANGECODEC_OK 0
#define LAGRANGECODEC_ERROR_INVALID_ARGUMENT -1
#define LAGRANGECODEC_ERROR_ALLOCATION_FAILED -2
#define LAGRANGECODEC_ERROR_OPEN_INPUT_FAILED -3
#define LAGRANGECODEC_ERROR_STREAM_INFO_FAILED -4
#define LAGRANGECODEC_ERROR_STREAM_NOT_FOUND -5
#define LAGRANGECODEC_ERROR_CODEC_NOT_FOUND -6
#define LAGRANGECODEC_ERROR_CODEC_OPEN_FAILED -7
#define LAGRANGECODEC_ERROR_DECODE_FAILED -8
#define LAGRANGECODEC_ERROR_CONVERSION_FAILED -9
#define LAGRANGECODEC_ERROR_OUTPUT_FAILED -10

#ifdef _WIN32
#  if defined(LAGRANGECODEC_SHARED_BUILD)
#    define LAGRANGECODEC_API __declspec(dllexport)
#  elif defined(LAGRANGECODEC_SHARED)
#    define LAGRANGECODEC_API __declspec(dllimport)
#  else
#    define LAGRANGECODEC_API
#  endif
#else
#  define LAGRANGECODEC_API
#endif

#define EXPORT extern "C" LAGRANGECODEC_API

#endif //COMMON_H
