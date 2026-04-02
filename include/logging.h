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
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <unistd.h>

#  define LAGRANGECODEC_LOG_TAG "LagrangeCodec"

inline void lc_android_file_log(const char* buffer, size_t total_len) {
    const char* primary = "/data/local/tmp/lagrangecodec-native.log";
    const char* fallback = "./lagrangecodec-native.log";

    int fd = open(primary, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (fd < 0 && std::strcmp(primary, fallback) != 0) {
        fd = open(fallback, O_CREAT | O_WRONLY | O_APPEND, 0644);
    }

    if (fd >= 0) {
        write(fd, buffer, total_len);
        close(fd);
    }
}

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
    lc_android_file_log(buffer, total_len);
    __android_log_write(priority, LAGRANGECODEC_LOG_TAG, buffer);
}

#  define LC_LOGE(...) lc_android_terminal_log(ANDROID_LOG_ERROR, "ERR", __VA_ARGS__)
#  define LC_LOGI(...) lc_android_terminal_log(ANDROID_LOG_INFO, "INF", __VA_ARGS__)
#  define LC_LOGD(...) lc_android_terminal_log(ANDROID_LOG_DEBUG, "DBG", __VA_ARGS__)

inline void lc_android_trace_point(const char* message) {
    if (!message) {
        return;
    }

    const size_t len = ::strlen(message);
    if (len == 0) {
        return;
    }

    write(STDOUT_FILENO, message, len);
    write(STDOUT_FILENO, "\n", 1);
    lc_android_file_log(message, len);
    lc_android_file_log("\n", 1);
}

#  define LC_TRACE_POINT(message_literal) lc_android_trace_point(message_literal)
#  define LC_TRACE_LITERAL(message_literal) do { \
    static const char kTraceMessage[] = message_literal "\n"; \
    write(STDOUT_FILENO, kTraceMessage, sizeof(kTraceMessage) - 1); \
    lc_android_file_log(kTraceMessage, sizeof(kTraceMessage) - 1); \
  } while (0)
#else
#  include <cstdio>
#  define LC_LOGE(...) std::fprintf(stderr, __VA_ARGS__)
#  define LC_LOGI(...) std::fprintf(stdout, __VA_ARGS__)
#  define LC_LOGD(...) std::fprintf(stdout, __VA_ARGS__)
#  define LC_TRACE_POINT(message_literal) do { std::fputs(message_literal, stdout); std::fputc('\n', stdout); } while (0)
#  define LC_TRACE_LITERAL(message_literal) LC_TRACE_POINT(message_literal)
#endif

#endif // LAGRANGECODEC_LOGGING_H
