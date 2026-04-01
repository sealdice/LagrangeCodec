//
// Internal logging helpers used to avoid stdio globals in Android static bundles.
//

#ifndef LAGRANGECODEC_LOGGING_H
#define LAGRANGECODEC_LOGGING_H

#if defined(__ANDROID__)
#  include <android/log.h>
#  include <cstdarg>
#  include <cstdio>
#  include <cstring>
#  include <unistd.h>

#  define LAGRANGECODEC_LOG_TAG "LagrangeCodec"

inline void lc_android_terminal_log(int priority, const char* level, const char* fmt, ...) {
    char buffer[3072];
    int prefix_len = std::snprintf(
        buffer,
        sizeof(buffer),
        "\n========== LAGRANGECODEC %s =========="
        "\n",
        level
    );
    if (prefix_len < 0) {
        prefix_len = 0;
    }
    if (prefix_len >= static_cast<int>(sizeof(buffer))) {
        prefix_len = static_cast<int>(sizeof(buffer)) - 1;
    }

    va_list args;
    va_start(args, fmt);
    int body_len = std::vsnprintf(buffer + prefix_len, sizeof(buffer) - static_cast<size_t>(prefix_len), fmt, args);
    va_end(args);

    if (body_len < 0) {
        body_len = 0;
    }

    size_t total_len = ::strnlen(buffer, sizeof(buffer));
    if (total_len + 1 < sizeof(buffer) && (total_len == 0 || buffer[total_len - 1] != '\n')) {
        buffer[total_len++] = '\n';
        buffer[total_len] = '\0';
    }

    if (total_len + 40 < sizeof(buffer)) {
        const char* footer = "========== /LAGRANGECODEC =========="
                             "\n";
        const size_t footer_len = std::strlen(footer);
        std::memcpy(buffer + total_len, footer, footer_len + 1);
        total_len += footer_len;
    }

    write(STDOUT_FILENO, buffer, total_len);
    __android_log_write(priority, LAGRANGECODEC_LOG_TAG, buffer);
}

#  define LC_LOGE(...) lc_android_terminal_log(ANDROID_LOG_ERROR, "ERR", __VA_ARGS__)
#  define LC_LOGI(...) lc_android_terminal_log(ANDROID_LOG_INFO, "INF", __VA_ARGS__)
#  define LC_LOGD(...) lc_android_terminal_log(ANDROID_LOG_DEBUG, "DBG", __VA_ARGS__)
#else
#  include <cstdio>
#  define LC_LOGE(...) std::fprintf(stderr, __VA_ARGS__)
#  define LC_LOGI(...) std::fprintf(stdout, __VA_ARGS__)
#  define LC_LOGD(...) std::fprintf(stdout, __VA_ARGS__)
#endif

#endif // LAGRANGECODEC_LOGGING_H
