//
// Internal logging helpers used to avoid stdio globals in Android static bundles.
//

#ifndef LAGRANGECODEC_LOGGING_H
#define LAGRANGECODEC_LOGGING_H

#if defined(__ANDROID__)
#  include <android/log.h>
#  define LAGRANGECODEC_LOG_TAG "LagrangeCodec"
#  define LC_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LAGRANGECODEC_LOG_TAG, __VA_ARGS__)
#  define LC_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LAGRANGECODEC_LOG_TAG, __VA_ARGS__)
#else
#  include <cstdio>
#  define LC_LOGE(...) std::fprintf(stderr, __VA_ARGS__)
#  define LC_LOGD(...) std::fprintf(stdout, __VA_ARGS__)
#endif

#endif // LAGRANGECODEC_LOGGING_H
