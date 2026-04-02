#include "common.h"

#if defined(__ANDROID__)
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static constexpr const char* kProbeLogPath = "/data/local/tmp/lagrangecodec-probe.log";
static constexpr const char* kBuildMarker = "LAGRANGECODEC_BUILD_MARKER_2026_04_02_2f32b59_PLUS";

static void probe_write_literal(const char* literal) {
    if (!literal) {
        return;
    }

    const size_t len = ::strlen(literal);
    if (len == 0) {
        return;
    }

    write(STDOUT_FILENO, literal, len);
    write(STDOUT_FILENO, "\n", 1);

    int fd = open(kProbeLogPath, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (fd >= 0) {
        write(fd, literal, len);
        write(fd, "\n", 1);
        close(fd);
    }
}

extern "C" __attribute__((used, noinline)) void lc_probe_audio_to_pcm_entry(void) {
    probe_write_literal("LC_PROBE audio_to_pcm entry");
}

extern "C" __attribute__((used, noinline)) void lc_probe_video_first_frame_entry(void) {
    probe_write_literal("LC_PROBE video_first_frame entry");
}

extern "C" __attribute__((used, noinline)) void lc_probe_video_get_size_entry(void) {
    probe_write_literal("LC_PROBE video_get_size entry");
}

EXPORT const char* lagrangecodec_build_marker(void) {
    return kBuildMarker;
}

#else

#include <stdio.h>

static constexpr const char* kBuildMarker = "LAGRANGECODEC_BUILD_MARKER_2026_04_02_2f32b59_PLUS";

extern "C" __attribute__((used, noinline)) void lc_probe_audio_to_pcm_entry(void) {
    fputs("LC_PROBE audio_to_pcm entry\n", stdout);
}

extern "C" __attribute__((used, noinline)) void lc_probe_video_first_frame_entry(void) {
    fputs("LC_PROBE video_first_frame entry\n", stdout);
}

extern "C" __attribute__((used, noinline)) void lc_probe_video_get_size_entry(void) {
    fputs("LC_PROBE video_get_size entry\n", stdout);
}

EXPORT const char* lagrangecodec_build_marker(void) {
    return kBuildMarker;
}

#endif
